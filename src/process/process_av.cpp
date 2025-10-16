#include "process_av.h"

#include "../input/input.h"
#include "../output/output.h"
#include "process.hpp"

#include <mist/h264.h>
#include <mist/mp4_generic.h>
#include <mist/nal.h>
#include <mist/procs.h>
#include <mist/util.h>

#include <condition_variable>
#include <cstdarg> //for libav log handling
#include <mutex>
#include <ostream>
#include <sys/stat.h> //for stat
#include <sys/types.h> //for stat
#include <thread>
#include <unistd.h> //for stat

// libav headers
extern "C" {
  #include "libavcodec/version.h"
  #include "libavcodec/avcodec.h"
  #include "libavutil/avutil.h"
  #include "libavutil/imgutils.h"         ///< Video frame operations
  #include "libswscale/swscale.h"         ///< Video scaling & converting pixel format
  #include "libswresample/swresample.h"   ///< Audio resampling libs
  #include "libavutil/audio_fifo.h"       ///< Audio resample buffer
  #include "libavutil/channel_layout.h"   ///< Audio channels <> Speaker layouts
//  #include <libavfilter/buffersink.h>
//  #include <libavfilter/buffersrc.h>
}

bool allowHW = true;
bool allowSW = true;

Util::Config co;
Util::Config conf;
const AVCodec *codec_out = 0, *codec_in = 0;      ///< Decoding/encoding codecs for libav
AVPacket* packet_out;                             ///< Buffer for encoded audio/video packets
AVFrame *frameConverted = 0;                      ///< Buffer for encoded audio/video frames
AVFrame *frameInHW = 0;                           ///< Buffer for frames when encoding hardware accelerated
AVFrame *frameDecodeHW = 0;                       ///< Buffer for frames when decoding hardware accelerated
AVAudioFifo *audioBuffer = 0;                     ///< Buffer for encoded audio samples
AVCodecContext *context_out = 0, *context_in = 0; ///< Decoding/encoding contexts
std::deque<uint64_t> frameTimes;                  ///< Frame timestamps
bool frameReady = false;
std::mutex avMutex;                       ///< Lock for reading/writing frameTimes/frameReady
std::condition_variable avCV; ///< Condition variable for reading/writing frameTimes/frameReady
int av_logLevel = AV_LOG_WARNING;
char * scaler = (char*)"None";

//Stat related stuff
JSON::Value pStat;
JSON::Value & pData = pStat["proc_status_update"]["status"];
std::mutex statsMutex;
uint64_t statSinkMs = 0;
uint64_t statSourceMs = 0;
int64_t bootMsOffset = 0;
uint64_t inFpks = 0;                              ///< Framerate to set to the output video track
uint64_t outAudioRate = 0;                        ///< Audio sampling rate to set to the output
uint64_t outAudioChannels = 0;                    ///< Audio channel count to set to the output
uint64_t outAudioDepth = 0;                       ///< Audio bit depth to set to the output
std::string codecIn, codecOut;                    ///< If not raw pixels: names of encoding/decoding codecs
bool isVideo = true;                              ///< Transcoding in video or audio mode
uint64_t totalDecode = 0;
uint64_t totalSinkSleep = 0;
uint64_t totalTransform = 0;
uint64_t totalEncode = 0;
uint64_t totalSourceSleep = 0;

char *inputFrameCount = 0;                        ///< Stats: frames/samples ingested
char *outputFrameCount = 0;                       ///< Stats: frames/samples outputted
Util::ResizeablePointer ptr;                      ///< Buffer for raw pixels / audio samples

namespace Mist{

  class ProcessSink : public Input{
  private:
    size_t trkIdx;
    Util::ResizeablePointer ppsInfo;
    Util::ResizeablePointer spsInfo;

  public:
    ProcessSink(Util::Config *cfg) : Input(cfg){
      trkIdx = INVALID_TRACK_ID;
      capa["name"] = "AV";
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      Util::streamVariables(streamName, opt["source"].asString());
      {
        std::lock_guard<std::mutex> guard(statsMutex);
        pStat["proc_status_update"]["sink"] = streamName;
        pStat["proc_status_update"]["source"] = opt["source"];
      }
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
      if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
        DTSC::trackValidDefault = opt["target_mask"].asInt();
      } else {
        DTSC::trackValidDefault = TRACK_VALID_EXT_HUMAN | TRACK_VALID_EXT_PUSH;
      }
    };

    void setNowMS(uint64_t t){
      if (!userSelect.size()){return;}
      meta.setNowms(userSelect.begin()->first, t);
    }

    ~ProcessSink(){ }

    void parseH264(bool isKey){
      // Get buffer pointers
      const char* bufIt = (char*)packet_out->data;
      uint64_t bufSize = packet_out->size;
      const char *nextPtr;
      const char *pesEnd = (char*)packet_out->data + bufSize;
      uint32_t nalSize = 0;

      // Parse H264-specific data
      nextPtr = nalu::scanAnnexB(bufIt, bufSize);
      if (!nextPtr){
        WARN_MSG("Unable to find AnnexB data in the H264 buffer");
        return;
      }
      thisPacket.null();
      while (nextPtr < pesEnd){
        if (!nextPtr){nextPtr = pesEnd;}
        // Calculate size of NAL unit, removing null bytes from the end
        nalSize = nalu::nalEndPosition(bufIt, nextPtr - bufIt) - bufIt;
        if (nalSize){
          // If we don't have a packet yet, init an empty packet
          if (!thisPacket){
            thisPacket.genericFill(thisTime, 0, 1, 0, 0, 0, isKey);
          }
          // Set PPS/SPS info
          uint8_t typeNal = bufIt[0] & 0x1F;
          if (typeNal == 0x07){
            spsInfo.assign(std::string(bufIt, (nextPtr - bufIt)));
          } else if (typeNal == 0x08){
            ppsInfo.assign(std::string(bufIt, (nextPtr - bufIt)));
          }
          thisPacket.appendNal(bufIt, nalSize);
        }
        if (((nextPtr - bufIt) + 3) >= bufSize){break;}// end of the line
        bufSize -= ((nextPtr - bufIt) + 3); // decrease the total size
        bufIt = nextPtr + 3;
        nextPtr = nalu::scanAnnexB(bufIt, bufSize);
      }
    }

    void parseAV1(bool isKey){
      thisPacket.null();
      // Get buffer pointers
      const char* bufIt = (char*)packet_out->data;
      uint64_t bufSize = packet_out->size;
      thisPacket.genericFill(thisTime, 0, 1, bufIt, bufSize, 0, isKey);
    }

    void parseJPEG(){
      thisPacket.null();
      // Get buffer pointers
      const char* bufIt = (char*)packet_out->data;
      uint64_t bufSize = packet_out->size;
      thisPacket.genericFill(thisTime, 0, 1, bufIt, bufSize, 0, 1);
    }

    /// \brief Outputs buffer as video
    void bufferVideo(){
      // Read from encoded buffers if we have a target codec
      if (codec_out){
        bool isKey = packet_out->flags & AV_PKT_FLAG_KEY;
        if (isKey){
          MEDIUM_MSG("Buffering %iB keyframe @%zums", packet_out->size, thisTime);
        }else{
          VERYHIGH_MSG("Buffering %iB packet @%zums", packet_out->size, thisTime);
        }

        if (codecOut == "AV1"){
          parseAV1(isKey);
        }else if (codecOut == "H264"){
          parseH264(isKey);
        }else if (codecOut == "JPEG"){
          parseJPEG();
        }

        if (!thisPacket){return;}
        // Now that we have SPS and PPS info, init the video track
        setVideoInit();
        if (trkIdx == INVALID_TRACK_ID){return;}
        thisIdx = trkIdx;
        bufferLivePacket(thisPacket);
      }else{
        // Read from raw buffers if we have no target codec
        if (ptr.size()){
          VERYHIGH_MSG("Reading raw frame %" PRIu64 " of %zu bytes", (uint64_t)outputFrameCount, ptr.size());
          // Init video track
          setVideoInit();
          if (trkIdx == INVALID_TRACK_ID){return;}
          thisIdx = trkIdx;
          bufferLivePacket(thisTime, 0, thisIdx, ptr, ptr.size(), 0, true);
        }
      }
    }

    /// \brief Outputs buffer as audio
    void bufferAudio(){
      // Init audio track
      setAudioInit();
      VERYHIGH_MSG("Buffering %iB audio packet @%zums", packet_out->size, thisTime);
      // Set init data
      if (context_out->extradata_size && !M.getInit(thisIdx).size()){
        meta.setInit(thisIdx, (char*)context_out->extradata, context_out->extradata_size);
      }
      // Get pointers to buffer
      const char* ptr = (char*)packet_out->data;
      uint64_t ptrSize = packet_out->size;
      // Buffer packet
      if (trkIdx == INVALID_TRACK_ID){return;}
      thisIdx = trkIdx;
      bufferLivePacket(thisTime, 0, thisIdx, ptr, ptrSize, 0, true);
    }

    void streamMainLoop(){
      uint64_t statTimer = 0;
      uint64_t startTime = Util::bootSecs();
      Comms::Connections statComm;
      while (config->is_active){
        // Get current frame time
        {
          uint64_t sleepTime = Util::getMicros();
          // Wait for frame/samples to become available
          std::unique_lock<std::mutex> lk(avMutex);
          avCV.wait(lk,[](){return frameReady || !config->is_active;});
          totalSinkSleep += Util::getMicros(sleepTime);
          if (!config->is_active){return;}
          thisTime = frameTimes.front();
          frameTimes.pop_front();
          if (thisTime >= statSinkMs){statSinkMs = thisTime;}
          if (meta && meta.getBootMsOffset() != bootMsOffset){meta.setBootMsOffset(bootMsOffset);}

          // Output current video/audio buffers
          if (isVideo){
            bufferVideo();
          }else{
            bufferAudio();
          }
          // Notify input that we require another frame
          frameReady = false;
        }
        avCV.notify_all();

        if (!userSelect.count(thisIdx)) { userSelect[thisIdx].reload(streamName, thisIdx, COMM_STATUS_ACTSOURCEDNT); }
        if (userSelect[thisIdx].getStatus() & COMM_STATUS_REQDISCONNECT){
          Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
          break;
        }
        if (isSingular() && !bufferActive()){
          Util::logExitReason(ER_SHM_LOST, "Buffer shut down");
          return;
        }

        if (Util::bootSecs() - statTimer > 1){
          // Connect to stats for INPUT detection
          if (!statComm){statComm.reload(streamName, getConnectedBinHost(), JSON::Value(getpid()).asString(), "INPUT:" + capa["name"].asStringRef(), "");}
          if (statComm){
            if (!statComm){
              config->is_active = false;
              Util::logExitReason(ER_CLEAN_CONTROLLER_REQ, "received shutdown request from controller");
              return;
            }
            uint64_t now = Util::bootSecs();
            statComm.setNow(now);
            statComm.setStream(streamName);
            statComm.setTime(now - startTime);
            statComm.setLastSecond(0);
            connStats(statComm);
          }

          statTimer = Util::bootSecs();
          {
            std::lock_guard<std::mutex> guard(statsMutex);
            if (pData["sink_tracks"].size() != userSelect.size()){
              pData["sink_tracks"].null();
              for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
                pData["sink_tracks"].append((uint64_t)it->first);
              }
            }
          }
        }
      }
    }

    /// \brief Sets init data based on the last loaded SPS and PPS data
    void setVideoInit(){
      if (trkIdx != INVALID_TRACK_ID){return;}
      // We're encoding to a target codec
      if (codec_out){
        if (codecOut == "AV1"){
          // Add a single track and init some metadata
          meta.reInit(streamName, false);
          trkIdx = meta.addTrack();
          meta.setType(trkIdx, "video");
          meta.setCodec(trkIdx, codecOut);
          meta.setID(trkIdx, 1);
          meta.setWidth(trkIdx, frameConverted->width);
          meta.setHeight(trkIdx,  frameConverted->height);
          meta.setFpks(trkIdx, inFpks);
          meta.setInit(trkIdx, (char*)context_out->extradata, context_out->extradata_size);
          if (trkIdx != INVALID_TRACK_ID && !userSelect.count(trkIdx)){
            userSelect[trkIdx].reload(streamName, trkIdx, COMM_STATUS_ACTSOURCEDNT);
          }
          INFO_MSG("AV1 track index is %zu", trkIdx);
        } else if (codecOut == "JPEG"){
          meta.reInit(streamName, false);
          trkIdx = meta.addTrack();
          meta.setType(trkIdx, "video");
          meta.setCodec(trkIdx, codecOut);
          meta.setID(trkIdx, 1);
          meta.setWidth(trkIdx, frameConverted->width);
          meta.setHeight(trkIdx,  frameConverted->height);
          meta.setFpks(trkIdx, 0);
          if (trkIdx != INVALID_TRACK_ID && !userSelect.count(trkIdx)){
            userSelect[trkIdx].reload(streamName, trkIdx, COMM_STATUS_ACTSOURCEDNT);
          }
          INFO_MSG("MJPEG track index is %zu", trkIdx);
        }else if (codecOut == "H264"){
          if (!spsInfo.size() || !ppsInfo.size()){return;}
          // First generate needed data
          h264::sequenceParameterSet sps(spsInfo, spsInfo.size());

          MP4::AVCC avccBox;
          avccBox.setVersion(1);
          avccBox.setProfile(spsInfo[1]);
          avccBox.setCompatibleProfiles(spsInfo[2]);
          avccBox.setLevel(spsInfo[3]);
          avccBox.setSPSCount(1);
          avccBox.setSPS(spsInfo, spsInfo.size());
          avccBox.setPPSCount(1);
          avccBox.setPPS(ppsInfo, ppsInfo.size());

          // Add a single track and init some metadata
          meta.reInit(streamName, false);
          trkIdx = meta.addTrack();
          meta.setType(trkIdx, "video");
          meta.setCodec(trkIdx, "H264");
          meta.setID(trkIdx, 1);
          if (avccBox.payloadSize()){meta.setInit(trkIdx, avccBox.payload(), avccBox.payloadSize());}
          meta.setWidth(trkIdx, sps.chars.width);
          meta.setHeight(trkIdx, sps.chars.height);
          meta.setFpks(trkIdx, inFpks);
          if (trkIdx != INVALID_TRACK_ID && !userSelect.count(trkIdx)){
            userSelect[trkIdx].reload(streamName, trkIdx, COMM_STATUS_ACTSOURCEDNT);
          }
          INFO_MSG("H264 track index is %zu", trkIdx);
        }
      }else{
        // Add a single track and init some metadata
        meta.reInit(streamName, false);
        size_t staticSize = Util::pixfmtToSize(codecOut, frameConverted->width, frameConverted->height);
        if (staticSize){
          //Known static frame sizes: raw track mode
          trkIdx = meta.addTrack(0, 0, 0, 0, true, staticSize);
        }else{
          // Other cases: standard track mode
          trkIdx = meta.addTrack();
        }
        meta.markUpdated(trkIdx);
        meta.setType(trkIdx, "video");
        meta.setCodec(trkIdx, codecOut);
        meta.setID(trkIdx, 1);
        meta.setWidth(trkIdx, frameConverted->width);
        meta.setHeight(trkIdx,  frameConverted->height);
        meta.setFpks(trkIdx, inFpks);
        if (trkIdx != INVALID_TRACK_ID && !userSelect.count(trkIdx)){
          userSelect[trkIdx].reload(streamName, trkIdx, COMM_STATUS_ACTSOURCEDNT);
        }
        INFO_MSG("%s track index is %zu", codecOut.c_str(), trkIdx);
      }
    }

    void setAudioInit(){
      if (trkIdx != INVALID_TRACK_ID){return;}

      // Add a single track and init some metadata
      meta.reInit(streamName, false);
      trkIdx = meta.addTrack();
      meta.setType(trkIdx, "audio");
      meta.setCodec(trkIdx, codecOut);
      meta.setID(trkIdx, 1);
      meta.setRate(trkIdx, outAudioRate);
      meta.setChannels(trkIdx, outAudioChannels);
      meta.setSize(trkIdx, outAudioDepth);

      if (trkIdx != INVALID_TRACK_ID && !userSelect.count(trkIdx)){
        userSelect[trkIdx].reload(streamName, trkIdx, COMM_STATUS_ACTSOURCEDNT);
      }
      INFO_MSG("%s track index is %zu", codecOut.c_str(), trkIdx);
    }

    bool checkArguments(){return true;}
    bool needHeader(){return false;}
    bool readHeader(){return true;}
    bool openStreamSource(){return true;}
    void parseStreamHeader(){}
    bool needsLock(){return false;}
    bool isSingular(){return false;}
    virtual bool publishesTracks(){return false;}
    void connStats(Comms::Connections &statComm){}
  };

  ProcessSink * sinkClass = 0;

  class ProcessSource : public Output{
  protected:
    inline virtual bool keepGoing(){return config->is_active;}
  private:
    SwsContext *convertCtx; ///< Convert input to target-codec-compatible YUV420 format
    SwrContext *resampleContext; ///< Resample audio formats
    enum AVPixelFormat pixelFormat;
    enum AVPixelFormat softFormat;
    enum AVPixelFormat softDecodeFormat;
    AVFrame *frame_RAW;     ///< Hold decoded frames/samples
    int64_t pts;
    int resampledSampleCount;
    AVBufferRef *hw_decode_ctx;
    AVPixelFormat hw_decode_fmt;
    AVPixelFormat hw_decode_sw_fmt;
    uint64_t skippedFrames; //< Amount of frames since last JPEG image

    // Filter vars
//    AVFilterContext *buffersink_ctx;
//    AVFilterContext *buffersrc_ctx;
//    AVFilterGraph *filter_graph;

    /// \brief Prints error codes from LibAV
    void printError(std::string preamble, int code){
      char err[128];
      av_strerror(code, err, sizeof(err));
      ERROR_MSG("%s: `%s` (%i)", preamble.c_str(), err, code);
    }
  public:
    bool isRecording(){return false;}

    ProcessSource(Socket::Connection & c) : Output(c) {
      meta.ignorePid(getpid());
      targetParams["keeptimes"] = true;
      realTime = 0;
      convertCtx = NULL;
      resampleContext = NULL;
      initialize();
      wantRequest = false;
      parseData = true;
      pixelFormat = AV_PIX_FMT_NONE;
      frame_RAW = NULL;
      pts = 0;
      resampledSampleCount = 0;
      softFormat = AV_PIX_FMT_NONE;
      softDecodeFormat = AV_PIX_FMT_NONE;
      hw_decode_ctx = 0;
      skippedFrames = 99999; //< Init high so that it does not skip the first keyframe
    }

    ~ProcessSource(){
      if (convertCtx){
        sws_freeContext(convertCtx);
      }
      if (resampleContext){
        swr_free(&resampleContext);
      }
    }

    static void init(Util::Config *cfg){
      Output::init(cfg);
      capa["name"] = "AV";
      // Track selection
      if (isVideo){
        capa["codecs"][0u][0u].append("YUYV");
        capa["codecs"][0u][0u].append("UYVY");
        capa["codecs"][0u][0u].append("NV12");
        capa["codecs"][0u][0u].append("H264");
        capa["codecs"][0u][0u].append("AV1");
        capa["codecs"][0u][0u].append("JPEG");
      }else{
        capa["codecs"][0u][0u].append("PCM");
        capa["codecs"][0u][0u].append("opus");
        capa["codecs"][0u][0u].append("AAC");
      }
      cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                              "\"stream\",\"help\":\"The name of the stream "
                                              "that this connector will transmit.\"}"));
      cfg->addBasicConnectorOptions(capa);
    }

    virtual bool onFinish(){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        if (userSelect.size()){
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            INFO_MSG("Unmasking source track %zu" PRIu64, it->first);
            meta.validateTrack(it->first, TRACK_VALID_ALL);
          }
        }
      }
      return Output::onFinish();
    }

    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
        meta.validateTrack(trackId, TRACK_VALID_ALL);
      }
      Output::dropTrack(trackId, reason, probablyBad);
    }

    void sendHeader(){
      if (opt["source_mask"].asBool()){
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
          if (ti->first == INVALID_TRACK_ID){continue;}
          INFO_MSG("Masking source track %zu", ti->first);
          meta.validateTrack(ti->first, meta.trackValid(ti->first) & ~(TRACK_VALID_EXT_HUMAN | TRACK_VALID_EXT_PUSH));
        }
      }
      realTime = 0;
      Output::sendHeader();
    };

    void connStats(uint64_t now, Comms::Connections &statComm){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second){it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus());}
      }
    }

    /// \brief Tries to open a given encoder. On success immediately configures it
    bool tryEncoder(std::string encoder, AVHWDeviceType hwDev, AVPixelFormat pixFmt, AVPixelFormat softFmt){
      av_logLevel = AV_LOG_DEBUG;
      if (encoder.size()){
        codec_out = avcodec_find_encoder_by_name(encoder.c_str());
      }else{
        if (codecOut == "AV1"){
          codec_out = avcodec_find_encoder(AV_CODEC_ID_AV1);
        }else if (codecOut == "H264"){
          codec_out = avcodec_find_encoder(AV_CODEC_ID_H264);
        }else if (codecOut == "JPEG"){
          codec_out = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
        }
      }
      AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_out);
      if (!tmpCtx) {
        ERROR_MSG("Could not allocate %s %s encode context", encoder.c_str(), codecOut.c_str());
        av_logLevel = AV_LOG_WARNING;
        return false;
      }

      uint64_t targetFPKS = M.getFpks(getMainSelectedTrack());
      if (!targetFPKS){
        WARN_MSG("No FPS set, assuming 60 FPS for the encoder.");
        targetFPKS = 60000;
      }
      uint64_t reqWidth = M.getWidth(getMainSelectedTrack());
      uint64_t reqHeight = M.getHeight(getMainSelectedTrack());
      if (opt.isMember("resolution") && opt["resolution"]){
        reqWidth = strtol(opt["resolution"].asString().substr(0, opt["resolution"].asString().find("x")).c_str(), NULL, 0);
        reqHeight = strtol(opt["resolution"].asString().substr(opt["resolution"].asString().find("x") + 1).c_str(), NULL, 0);
      }
      tmpCtx->bit_rate = Mist::opt["bitrate"].asInt();
      tmpCtx->rc_max_rate = 1.20 * Mist::opt["bitrate"].asInt();
      tmpCtx->rc_min_rate = 0;
      tmpCtx->rc_buffer_size = 2 * Mist::opt["bitrate"].asInt();
      tmpCtx->time_base.num = 1000;
      tmpCtx->time_base.den = targetFPKS;
      tmpCtx->codec_type = AVMEDIA_TYPE_VIDEO;
      tmpCtx->pix_fmt = pixFmt;
      tmpCtx->height = reqHeight;
      tmpCtx->width = reqWidth;
      tmpCtx->qmin = 20;
      tmpCtx->qmax = 51;
      tmpCtx->framerate = (AVRational){(int)targetFPKS, 1000};
      if (codecOut == "AV1"){
#if defined(LIBAVCODEC_VERSION_MAJOR) && LIBAVCODEC_VERSION_MAJOR >= 61
        tmpCtx->profile = AV_PROFILE_AV1_MAIN;
#elif defined(FF_PROFILE_AV1_MAIN)
        tmpCtx->profile = FF_PROFILE_AV1_MAIN;
#endif
      }else if (codecOut == "H264"){
#if defined(LIBAVCODEC_VERSION_MAJOR) && LIBAVCODEC_VERSION_MAJOR >= 61
        tmpCtx->profile = AV_PROFILE_H264_HIGH;
#elif defined(FF_PROFILE_H264_HIGH)
        tmpCtx->profile = FF_PROFILE_H264_HIGH;
#endif
      }
      tmpCtx->gop_size = Mist::opt["gopsize"].asInt();
      tmpCtx->max_b_frames = 0;
      tmpCtx->has_b_frames = false;
      tmpCtx->refs = 2;
      tmpCtx->slices = 0;
      tmpCtx->codec_id = codec_out->id;
      tmpCtx->compression_level = 4;
      tmpCtx->flags &= ~AV_CODEC_FLAG_CLOSED_GOP;

      AVBufferRef *hw_device_ctx = 0;
      if (hwDev != AV_HWDEVICE_TYPE_NONE){
        tmpCtx->refs = 0;
        av_hwdevice_ctx_create(&hw_device_ctx, hwDev, 0, 0, 0);

        if (!hw_device_ctx){
          INFO_MSG("Could not open %s %s hardware acceleration", encoder.c_str(), codecOut.c_str());
          avcodec_free_context(&tmpCtx);
          av_logLevel = AV_LOG_WARNING;
          softFormat = AV_PIX_FMT_NONE;
          return false;
        }

        INFO_MSG("Creating hw frame context");
        AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);

        AVHWFramesContext *frames_ctx; 
        frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
        frames_ctx->initial_pool_size = 1;
        frames_ctx->format    = pixFmt;
        frames_ctx->sw_format = softFmt;
        softFormat = softFmt;
        frames_ctx->width     = reqWidth;
        frames_ctx->height    = reqHeight;

        av_hwframe_ctx_init(hw_frames_ref);
        tmpCtx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);

        INFO_MSG("Creating hw frame memory");

        frameInHW = av_frame_alloc();
        av_hwframe_get_buffer(tmpCtx->hw_frames_ctx, frameInHW, 0);
      }

      INFO_MSG("Initing codec...");
      int ret;
      if (hwDev == AV_HWDEVICE_TYPE_CUDA){
        AVDictionary *avDict = NULL;
        if (codecOut == "H264" && Mist::opt["tune"].asString() == "zerolatency"){
          av_dict_set(&avDict, "preset", "ll", 0);
          av_dict_set(&avDict, "tune", "ull", 0);
        }
        if (Mist::opt["tune"].asString() == "zerolatency-lq"){
          av_dict_set(&avDict, "preset", "llhp", 0);
          av_dict_set(&avDict, "tune", "ull", 0);
        }
        if (Mist::opt["tune"].asString() == "zerolatency-hq"){
          av_dict_set(&avDict, "preset", "llhq", 0);
          av_dict_set(&avDict, "tune", "ull", 0);
        }
        av_dict_set(&avDict, "rc", "cbr", 0);
        if (codecOut == "H264"){
          av_dict_set(&avDict, "profile", "high", 0);
        }
        ret = avcodec_open2(tmpCtx, codec_out, &avDict);
      }else if (hwDev == AV_HWDEVICE_TYPE_QSV){
        AVDictionary *avDict = NULL;
        if (codecOut == "H264"){
          av_dict_set(&avDict, "preset", "medium", 0);
        }
        av_dict_set(&avDict, "look_ahead", "0", 0);
        ret = avcodec_open2(tmpCtx, codec_out, &avDict);
      }else{
        AVDictionary *avDict = NULL;
        if (codecOut == "H264"){
          if (Mist::opt["tune"] == "zerolatency-lq"){Mist::opt["tune"] = "zerolatency";}
          if (Mist::opt["tune"] == "zerolatency-hq"){Mist::opt["tune"] = "zerolatency";}
          av_dict_set(&avDict, "tune", Mist::opt["tune"].asString().c_str(), 0);
          av_dict_set(&avDict, "preset", Mist::opt["preset"].asString().c_str(), 0);
        }else if (codecOut =="AV1"){
          tmpCtx->thread_count = 8;
        }
        ret = avcodec_open2(tmpCtx, codec_out, &avDict);
      }

      if (ret < 0) {
        if (hw_device_ctx){av_buffer_unref(&hw_device_ctx);}
        if (frameInHW){av_frame_free(&frameInHW);}
        avcodec_free_context(&tmpCtx);
        printError("Could not open " + codecIn + " codec context", ret);
        av_logLevel = AV_LOG_WARNING;
        softFormat = AV_PIX_FMT_NONE;
        return false;
      }
      context_out = tmpCtx;

      av_logLevel = AV_LOG_WARNING;
      return true;
    }

    /// \brief Tries various encoders for video transcoding
    void allocateVideoEncoder(){
      // Prepare target codec encoder
      if (!context_out && (codecOut == "H264" || codecOut == "AV1" || codecOut == "JPEG")) {
        INFO_MSG("Initting %s encoder", codecOut.c_str());
        if(codecOut == "H264"){
          // if (!allowHW || !tryEncoder("h264_qsv", AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_YUV420P)){
            if (!allowHW || !tryEncoder("h264_nvenc", AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_YUV420P)){
              INFO_MSG("Falling back to software encoder.");
              if (!allowSW || !tryEncoder("", AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE)){
                ERROR_MSG("Could not allocate H264 context");
                exit(1);
              }
            }
          // }
        }else if(codecOut == "AV1"){
          // if (!allowHW || !tryEncoder("av1_qsv", AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_YUV420P)){
            if (!allowHW || !tryEncoder("av1_nvenc", AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_YUV420P)){
              INFO_MSG("Falling back to software encoder.");
              if (!allowSW || !tryEncoder("", AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE)){
                ERROR_MSG("Could not allocate AV1 context");
                exit(1);
              }
            }
          // }
        }else if(codecOut == "JPEG"){
          // Default to a software transcode
          if (!allowSW || !tryEncoder("", AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_NONE)){
            // if (!allowHW || !tryEncoder("mjpeg_qsv", AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_YUVJ420P)){
              if (!allowHW || !tryEncoder("mjpeg_nvenc", AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_YUVJ420P)){
                  ERROR_MSG("Could not allocate AV1 context");
                  exit(1);
              }
            // }
          }


        }
        INFO_MSG("%s encoder initted", codecOut.c_str());
      }

      if (!inFpks){
        inFpks = M.getFpks(thisIdx);
      }
    }

    void allocateAudioEncoder(){
      if (codec_out && !context_out) {
        AVDictionary *avDict = NULL;
        INFO_MSG("Allocating %s encoder", codecOut.c_str());
        AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_out);
        if (!tmpCtx) {
          ERROR_MSG("Could not allocate %s context", codecOut.c_str());
          exit(1);
        }

        tmpCtx->bit_rate = Mist::opt["bitrate"].asInt();
        tmpCtx->time_base = (AVRational){1, (int)M.getRate(getMainSelectedTrack())};
        tmpCtx->codec_type = AVMEDIA_TYPE_AUDIO;
        tmpCtx->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;

        // Retrieve audio bit depth
        uint16_t depth = M.getSize(getMainSelectedTrack());
        if (depth == 16){
          tmpCtx->sample_fmt = AV_SAMPLE_FMT_S16;
        }else if (depth == 32){
          tmpCtx->sample_fmt = AV_SAMPLE_FMT_S32;
        }

        // Retrieve audio sample rate
        tmpCtx->sample_rate = M.getRate(getMainSelectedTrack());

        // Retrieve audio channel count / layout
        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          tmpCtx->channels = M.getChannels(getMainSelectedTrack());
          outAudioChannels = tmpCtx->channels;
        #else
          tmpCtx->ch_layout.nb_channels = M.getChannels(getMainSelectedTrack());
          outAudioChannels = tmpCtx->ch_layout.nb_channels;
        #endif

        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          if (tmpCtx->channels == 1){
            tmpCtx->channel_layout = AV_CH_LAYOUT_MONO;
          }else if (tmpCtx->channels == 2){
            tmpCtx->channel_layout = AV_CH_LAYOUT_STEREO;
          }else if (tmpCtx->channels == 8){
            tmpCtx->channel_layout = AV_CH_LAYOUT_7POINT1;
          }else if (tmpCtx->channels == 16){
            tmpCtx->channel_layout = AV_CH_LAYOUT_HEXADECAGONAL;
            if (codecOut == "opus"){
              av_dict_set_int(&avDict, "mapping_family", 255, 0);
            }else if (codecOut == "AAC"){
              WARN_MSG("AAC supports a max of 8 channels, dropping the last 8 channels...");
              outAudioChannels = 8;
              tmpCtx->channel_layout = AV_CH_LAYOUT_7POINT1;
            }
          }
        #else
          if (tmpCtx->ch_layout.nb_channels == 1){
            tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
          }else if (tmpCtx->ch_layout.nb_channels == 2){
            tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
          }else if (tmpCtx->ch_layout.nb_channels == 8){
            tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_7POINT1;
          }else if (tmpCtx->ch_layout.nb_channels == 16){
            tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_HEXADECAGONAL;
            if (codecOut == "opus"){
              av_dict_set_int(&avDict, "mapping_family", 255, 0);
            }else if (codecOut == "AAC"){
              WARN_MSG("AAC supports a max of 8 channels, dropping the last 8 channels...");
              outAudioChannels = 8;
              tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_7POINT1;
            }
          }
        #endif

        // Codec-specific overrides
        if (codecOut == "AAC"){
          tmpCtx->sample_fmt = AV_SAMPLE_FMT_FLTP;
#if defined(LIBAVCODEC_VERSION_MAJOR) && LIBAVCODEC_VERSION_MAJOR >= 61
          tmpCtx->profile = AV_PROFILE_AAC_MAIN;
#elif defined(FF_PROFILE_AAC_MAIN)
          tmpCtx->profile = FF_PROFILE_AAC_MAIN;
#endif
          depth = 16;
        }else if (codecOut == "opus"){
          tmpCtx->sample_fmt = AV_SAMPLE_FMT_S16;
          tmpCtx->sample_rate = 48000;
          tmpCtx->time_base = (AVRational){1, 48000};
          if (tmpCtx->bit_rate < 500){
            WARN_MSG("Opus does not support a bitrate of %lu, clipping to 500", tmpCtx->bit_rate);
            tmpCtx->bit_rate = 500;
          } else if (tmpCtx->bit_rate > 256000) {
            WARN_MSG("Opus does not support a bitrate of %lu, clipping to 128000", tmpCtx->bit_rate);
            tmpCtx->bit_rate = 128000;
          }
          depth = 16;
        }else if (codecOut == "PCM"){
          tmpCtx->sample_fmt = AV_SAMPLE_FMT_S32;
          tmpCtx->sample_rate = 48000;
          tmpCtx->time_base = (AVRational){1, 48000};
          depth = 32;
        }else{
          avcodec_free_context(&tmpCtx);
          ERROR_MSG("Unsupported audio codec %s.", codecOut.c_str());
          return;
        }

        // Set globals so the output can set meta track info correctly
        outAudioRate = tmpCtx->sample_rate;
        outAudioDepth = depth;

        int ret = avcodec_open2(tmpCtx, codec_out, &avDict);
        if (ret < 0) {
          avcodec_free_context(&tmpCtx);
          printError("Could not open " + codecOut + " codec context", ret);
          return;
        }
        context_out = tmpCtx;
        INFO_MSG("%s encoder allocated", codecOut.c_str());
      }
    }

    /// \brief Tries to open a given encoder. On success immediately configures it
    bool tryDecoder(std::string decoder, AVHWDeviceType hwDev, AVPixelFormat pixFmt, AVPixelFormat softFmt){
      av_logLevel = AV_LOG_DEBUG;
       if (decoder.size()){
         codec_in = avcodec_find_decoder_by_name(decoder.c_str());
      }else{
        if (codecIn == "AV1"){
          codec_in = avcodec_find_decoder(AV_CODEC_ID_AV1);
        }else if (codecIn == "JPEG"){
          codec_in = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
        }else if (codecIn == "H264"){
          codec_in = avcodec_find_decoder(AV_CODEC_ID_H264);
        }else{
          codec_in = avcodec_find_decoder(AV_CODEC_ID_H264);
        }
      }
      AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_in);
      if (!tmpCtx) {
        ERROR_MSG("Could not allocate %s %s decode context", decoder.c_str(), codecIn.c_str());
        av_logLevel = AV_LOG_WARNING;
        return false;
      }

      std::string init = M.getInit(thisIdx);
      tmpCtx->extradata = (unsigned char *)malloc(init.size());
      tmpCtx->extradata_size = init.size();
      memcpy(tmpCtx->extradata, init.data(), init.size());

      uint64_t h = M.getHeight(thisIdx);
      uint64_t w = M.getWidth(thisIdx);
      tmpCtx->pix_fmt = pixFmt;
      tmpCtx->height = h;
      tmpCtx->width = w;

      if (hwDev != AV_HWDEVICE_TYPE_NONE){
        tmpCtx->refs = 0;
        av_hwdevice_ctx_create(&hw_decode_ctx, hwDev, 0, 0, 0);

        if (!hw_decode_ctx){
          INFO_MSG("Could not open %s %s hardware decode acceleration", decoder.c_str(), codecIn.c_str());
          avcodec_free_context(&tmpCtx);
          av_logLevel = AV_LOG_WARNING;
          softDecodeFormat = AV_PIX_FMT_NONE;
          return false;
        }

        INFO_MSG("Creating hw frame context");
        AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_decode_ctx);

        AVHWFramesContext *frames_ctx; 
        frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
        frames_ctx->initial_pool_size = 1;
        frames_ctx->format    = pixFmt;
        frames_ctx->sw_format = softFmt;
        softDecodeFormat = softFmt;
        frames_ctx->width     = w;
        frames_ctx->height    = h;
        hw_decode_fmt         = pixFmt;
        hw_decode_sw_fmt      = softFmt;

        av_hwframe_ctx_init(hw_frames_ref);
        tmpCtx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);

        INFO_MSG("Creating hw frame memory");

        frameDecodeHW = av_frame_alloc();
        av_hwframe_get_buffer(tmpCtx->hw_frames_ctx, frameDecodeHW, 0);
      }

      INFO_MSG("Initing codec...");
      int ret = avcodec_open2(tmpCtx, codec_in, 0);

      if (ret < 0) {
        if (hw_decode_ctx){av_buffer_unref(&hw_decode_ctx);}
        if (frameDecodeHW){av_frame_free(&frameDecodeHW);}
        avcodec_free_context(&tmpCtx);
        printError("Could not open " + codecOut + " codec context", ret);
        av_logLevel = AV_LOG_WARNING;
        softDecodeFormat = AV_PIX_FMT_NONE;
        return false;
      }
      context_in = tmpCtx;

      av_logLevel = AV_LOG_WARNING;
      return true;
    }

    /// \brief Init some flags and variables based on the input track
    bool configVideoDecoder(){
      // We only need to do this once
      if (pixelFormat == AV_PIX_FMT_NONE && !codec_in){
        codecIn = M.getCodec(thisIdx);
        // Only set pixel formats for RAW inputs
        if (codecIn == "YUYV"){
          pixelFormat = AV_PIX_FMT_YUYV422;
        }else if(codecIn == "UYVY"){
          pixelFormat = AV_PIX_FMT_UYVY422;
        }else if(codecIn == "NV12"){
          pixelFormat = AV_PIX_FMT_NV12;
        }else if(codecIn == "H264"){
          if (!allowHW || !tryDecoder("h264_cuvid", AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_YUV420P)){
            // if (!allowHW || !tryDecoder("h264_qsv", AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_YUV420P)){
              if (!allowSW){
                INFO_MSG("Disallowing software decode, aborting!");
                return false;
              }
              INFO_MSG("Falling back to software decoding");
              tryDecoder("", AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE);
            // }
          }
        }else if(codecIn == "AV1"){
          if (!allowHW || !tryDecoder("av1_cuvid", AV_HWDEVICE_TYPE_CUDA, AV_PIX_FMT_CUDA, AV_PIX_FMT_YUV420P)){
            // if (!allowHW || !tryDecoder("av1_qsv", AV_HWDEVICE_TYPE_QSV, AV_PIX_FMT_QSV, AV_PIX_FMT_YUV420P)){
              if (!allowSW){
                INFO_MSG("Disallowing software decode, aborting!");
                return false;
              }
              INFO_MSG("Falling back to software decoding");
              tryDecoder("", AV_HWDEVICE_TYPE_NONE, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE);
            // }
          }
        }else if(codecIn == "JPEG"){
          // Init MJPEG decoder
          if (!codec_in){
            codec_in = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
            AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_in);
            if (!tmpCtx) {
              ERROR_MSG("Could not allocate MJPEG decode context");
              return false;
            }
            int ret = avcodec_open2(tmpCtx, codec_in, 0);
            if (ret < 0) {
              avcodec_free_context(&tmpCtx);
              printError("Could not open MJPEG decode context", ret);
              return false;
            }
            context_in = tmpCtx;
            INFO_MSG("MJPEG decoder allocated!");
          }
        }else{
          ERROR_MSG("Unknown input codec %s", codecIn.c_str());
          return false;
        }
      }
      return true;
    }

    bool configAudioDecoder(){
      if (!codec_in){
        codecIn = M.getCodec(thisIdx);
        // NOTE: PCM has a 'codec' in LibAV for decoding and encoding which is used for encoding/decoding
        if(codecIn == "PCM"){
          uint16_t depth = M.getSize(getMainSelectedTrack());
          if (depth == 16){
            codec_in = avcodec_find_decoder(AV_CODEC_ID_PCM_S16BE);
          }else if (depth == 32){
            codec_in = avcodec_find_decoder(AV_CODEC_ID_PCM_S32BE);
          }

          AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_in);
          if (!tmpCtx) {
            ERROR_MSG("Could not allocate PCM decode context");
            return false;
          }

          #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
            tmpCtx->channels = M.getChannels(getMainSelectedTrack());
          #else
            tmpCtx->ch_layout.nb_channels = M.getChannels(getMainSelectedTrack());
          #endif

          #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
            if (tmpCtx->channels == 1){
              tmpCtx->channel_layout = AV_CH_LAYOUT_MONO;
            }else if (tmpCtx->channels == 2){
              tmpCtx->channel_layout = AV_CH_LAYOUT_STEREO;
            }else if (tmpCtx->channels == 8){
              tmpCtx->channel_layout = AV_CH_LAYOUT_7POINT1;
            }else if (tmpCtx->channels == 16){
              tmpCtx->channel_layout = AV_CH_LAYOUT_HEXADECAGONAL;
            }
          #else
            if (tmpCtx->ch_layout.nb_channels == 1){
              tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_MONO;
            }else if (tmpCtx->ch_layout.nb_channels == 2){
              tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_STEREO;
            }else if (tmpCtx->ch_layout.nb_channels == 8){
              tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_7POINT1;
            }else if (tmpCtx->ch_layout.nb_channels == 16){
              tmpCtx->ch_layout = AV_CHANNEL_LAYOUT_HEXADECAGONAL;
            }
          #endif

          tmpCtx->sample_rate = M.getRate(getMainSelectedTrack());
          if (depth == 16){
            tmpCtx->sample_fmt = AV_SAMPLE_FMT_S16;
          }else if (depth == 32){
            tmpCtx->sample_fmt = AV_SAMPLE_FMT_S32;
          }

          int ret = avcodec_open2(tmpCtx, codec_in, 0);
          if (ret < 0) {
            avcodec_free_context(&tmpCtx);
            printError("Could not open PCM decode context", ret);
            return false;
          }

          context_in = tmpCtx;
          INFO_MSG("PCM decoder allocated!");
        }else if(codecIn == "opus"){
          codec_in = avcodec_find_decoder(AV_CODEC_ID_OPUS);
          AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_in);
          if (!tmpCtx) {
            ERROR_MSG("Could not allocate opus decode context");
            return false;
          }
          std::string init = M.getInit(thisIdx);
          if (init.size()){
            tmpCtx->extradata = (unsigned char *)malloc(init.size());
            tmpCtx->extradata_size = init.size();
            memcpy(tmpCtx->extradata, init.data(), init.size());
          }
          int ret = avcodec_open2(tmpCtx, codec_in, 0);
          if (ret < 0) {
            avcodec_free_context(&tmpCtx);
            printError("Could not open opus decode context", ret);
            return false;
          }
          context_in = tmpCtx;
          INFO_MSG("Opus decoder allocated!");
        }else if(codecIn == "AAC"){
          codec_in = avcodec_find_decoder(AV_CODEC_ID_AAC);
          AVCodecContext * tmpCtx = avcodec_alloc_context3(codec_in);
          if (!tmpCtx) {
            ERROR_MSG("Could not allocate AAC decode context");
            return false;
          }
          std::string init = M.getInit(thisIdx);
          tmpCtx->extradata = (unsigned char *)malloc(init.size());
          tmpCtx->extradata_size = init.size();
          memcpy(tmpCtx->extradata, init.data(), init.size());

          int ret = avcodec_open2(tmpCtx, codec_in, 0);
          if (ret < 0) {
            avcodec_free_context(&tmpCtx);
            printError("Could not open AAC decode context", ret);
            return false;
          }
          context_in = tmpCtx;
          INFO_MSG("AAC decoder allocated!");
        }else{
          ERROR_MSG("Unknown input codec %s", codecIn.c_str());
          return false;
        }
      }
      return true;
    }

    /// \brief Decode frame using the configured decoding context
    bool decodeFrame(char *dataPointer, size_t dataLen){
      // Allocate packet
      AVPacket *packet_in = av_packet_alloc();
      av_new_packet(packet_in, dataLen);
      // Copy buffer to packet
      memcpy(packet_in->data, dataPointer, dataLen);
      // Send packet to decoding context
      int ret = avcodec_send_packet(context_in, packet_in);
      av_packet_free(&packet_in);
      if (ret < 0) {
        printError("Error sending a packet for decoding", ret);
        return false;
      }
      // Retrieve RAW packet from decoding context
      if (frameDecodeHW){
        ret = avcodec_receive_frame(context_in, frameDecodeHW);
        if (frameDecodeHW->format == AV_PIX_FMT_NV12){
          pixelFormat = AV_PIX_FMT_NV12;
          frame_RAW = frameDecodeHW;
        }else{
          pixelFormat = softDecodeFormat;
        }
        // Allocate RAW frame
        if (!frame_RAW){
          if (!(frame_RAW = av_frame_alloc())){
            ERROR_MSG("Unable to allocate raw frame");
            return false;
          }
          frame_RAW->width  = frameDecodeHW->width;
          frame_RAW->height = frameDecodeHW->height;
          frame_RAW->format = pixelFormat;
          // Allocate buffer
          int ret = av_frame_get_buffer(frame_RAW, 0);
          if (ret < 0){
            av_frame_free(&frame_RAW);
            frame_RAW = 0;
            printError("Could not allocate RAW buffer", ret);
            return false;
          }
          av_image_fill_linesizes(frame_RAW->linesize, pixelFormat, frame_RAW->width);
          INFO_MSG("Raw video frame allocated for hardware download!");
        }
      }else{
        // Allocate RAW frame
        if (!frame_RAW){
          if (!(frame_RAW = av_frame_alloc())){
            ERROR_MSG("Unable to allocate raw frame");
            return false;
          }
        }
        ret = avcodec_receive_frame(context_in, frame_RAW);
        // Set the pixel format to whatever the decoder detected
        pixelFormat = (AVPixelFormat)frame_RAW->format;
      }
      if (ret < 0) {
        printError("Error during decoding", ret);
        return false;
      }
      return true;
    }

    /// \brief Decode video frame, given packet data
    bool decodeVideoFrame(char *dataPointer, size_t dataLen){
      ++inputFrameCount;

      // Generic case with a allocated decoding context
      if (context_in){
        if(decodeFrame(dataPointer, dataLen)){return true;}
        return false;
      }

      // RAW input, we have to allocate and config the RAW frame manually
      if (!frame_RAW){
        if (!(frame_RAW = av_frame_alloc())){
          ERROR_MSG("Unable to open RAW frame");
          return false;
        }
        // Config based on the target codec.
        // NOTE: this assumes that we're transcoding from RAW->non-RAW
        // NOTE: this needs adjustment if we ever add a variable output resolution
        frame_RAW->width  = context_out->width;
        frame_RAW->height = context_out->height;
        frame_RAW->format = pixelFormat;

        // Allocate buffer
        int ret = av_frame_get_buffer(frame_RAW, 0);
        if (ret < 0){
          av_frame_free(&frame_RAW);
          frame_RAW = 0;
          printError("Could not allocate RAW buffer", ret);
          return false;
        }
        av_image_fill_linesizes(frame_RAW->linesize, pixelFormat, frame_RAW->width);
        INFO_MSG("Raw video frame allocated!");
      }

      // Load pixel data from DTSC packet into buffer
      av_image_fill_pointers(frame_RAW->data, pixelFormat, frame_RAW->height, (uint8_t *)dataPointer, frame_RAW->linesize);
      return true;
    }

    /// \brief Transforms the RAW video buffer to have required output properties
    bool transformVideoFrame(){


      AVPixelFormat convertToPixFmt;
      if (codecOut == "H264"){
        if (softFormat == AV_PIX_FMT_NONE){
          convertToPixFmt = context_out->pix_fmt;
        }else{
          convertToPixFmt = softFormat;
        }
      }else if (codecOut == "AV1"){
        if (softFormat == AV_PIX_FMT_NONE){
          convertToPixFmt = context_out->pix_fmt;
        }else{
          convertToPixFmt = softFormat;
        }
      }else if (codecOut == "JPEG"){
        if (softFormat == AV_PIX_FMT_NONE){
          convertToPixFmt = context_out->pix_fmt;
        }else{
          convertToPixFmt = softFormat;
        }
      }else if (codecOut == "YUYV"){
        convertToPixFmt = AV_PIX_FMT_YUYV422;
      }else if (codecOut == "UYVY"){
        convertToPixFmt = AV_PIX_FMT_UYVY422;
      }

      if (frameDecodeHW && frameDecodeHW != frame_RAW && (!frameInHW || softDecodeFormat != convertToPixFmt )){
        int ret = av_hwframe_transfer_data(frame_RAW, frameDecodeHW, 0);
        if (ret){
          INFO_MSG("Transferring from %s to %s",av_get_pix_fmt_name((enum AVPixelFormat)frameDecodeHW->format),av_get_pix_fmt_name((enum AVPixelFormat)frame_RAW->format));
          printError("Unable to download frame from the hardware", ret);
          return false;
        }
      }



      // Create context to convert the pixel format
      // NOTE: this is done here, rather than in the allocate and config
      //        functions, as frame_RAW is initted during decoding 
      if (convertToPixFmt == AV_PIX_FMT_VAAPI){

/*
        const char *filters_descr = "vaapi_npp";

        char args[512];
        int ret = 0;
        const AVFilter *buffersrc  = avfilter_get_by_name("buffer");
        const AVFilter *buffersink = avfilter_get_by_name("buffersink");
        AVFilterInOut *outputs = avfilter_inout_alloc();
        AVFilterInOut *inputs  = avfilter_inout_alloc();
        AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
        enum AVPixelFormat pix_fmts[] = { AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE };
     
        filter_graph = avfilter_graph_alloc();
        if (!outputs || !inputs || !filter_graph) {
            ret = AVERROR(ENOMEM);
            goto end;
        }
     
        // buffer video source: the decoded frames from the decoder will be inserted here.
        snprintf(args, sizeof(args),
                "video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
                dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
                time_base.num, time_base.den,
                dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);
     
        ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in",
                                           args, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer source\n");
            goto end;
        }
     
        // buffer video sink: to terminate the filter chain.
        ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
                                           NULL, NULL, filter_graph);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot create buffer sink\n");
            goto end;
        }
     
        ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
                                  AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
        if (ret < 0) {
            av_log(NULL, AV_LOG_ERROR, "Cannot set output pixel format\n");
            goto end;
        }
     
     
        outputs->name       = av_strdup("in");
        outputs->filter_ctx = buffersrc_ctx;
        outputs->pad_idx    = 0;
        outputs->next       = NULL;
     
        inputs->name       = av_strdup("out");
        inputs->filter_ctx = buffersink_ctx;
        inputs->pad_idx    = 0;
        inputs->next       = NULL;
     
        if ((ret = avfilter_graph_parse_ptr(filter_graph, filters_descr,
                                        &inputs, &outputs, NULL)) < 0)
            goto end;
     
        if ((ret = avfilter_graph_config(filter_graph, NULL)) < 0)
            goto end;
     
    end:
        avfilter_inout_free(&inputs);
        avfilter_inout_free(&outputs);
     
        return ret;




        // push the decoded frame into the filtergraph
        if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
            av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
            break;
        }

        // pull filtered frames from the filtergraph
        while (1) {
            ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                break;
            if (ret < 0)
                goto end;
            display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
            av_frame_unref(filt_frame);
        }
        av_frame_unref(frame);


*/


        INFO_MSG("Not converting VAAPI frame");
        frameConverted = frame_RAW;
      }else{
        // No conversion needed? Do nothing.
        if (frame_RAW->format == convertToPixFmt && !opt.isMember("resolution")){
          frameConverted = frame_RAW;
          return true;
        }

        // Init scaling context if needed
        if (!convertCtx){
          INFO_MSG("Allocating scaling context for %s -> %s...", av_get_pix_fmt_name((enum AVPixelFormat)frame_RAW->format), av_get_pix_fmt_name(convertToPixFmt));
          uint64_t reqWidth = frame_RAW->width;
          uint64_t reqHeight = frame_RAW->height;
          if (opt.isMember("resolution") && opt["resolution"]){
            reqWidth = strtol(opt["resolution"].asString().substr(0, opt["resolution"].asString().find("x")).c_str(), NULL, 0);
            reqHeight = strtol(opt["resolution"].asString().substr(opt["resolution"].asString().find("x") + 1).c_str(), NULL, 0);\
          }
          convertCtx = sws_getContext(frame_RAW->width, frame_RAW->height, (enum AVPixelFormat)frame_RAW->format, reqWidth, reqHeight, convertToPixFmt, SWS_FAST_BILINEAR | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND, NULL, NULL, NULL);
          if (!convertCtx) {
            FAIL_MSG("Could not allocate scaling context");
            return false;
          }
          static char scaleTxt[500];
          snprintf(scaleTxt, 500, "%s -> %s", av_get_pix_fmt_name((enum AVPixelFormat)frame_RAW->format), av_get_pix_fmt_name(convertToPixFmt));
          scaler = scaleTxt;
        }
        // Create frame to hold the result of the scaling context
        if (!frameConverted || frameConverted == frame_RAW){
          frameConverted = av_frame_alloc();
          if (!frameConverted) {
            FAIL_MSG("Could not allocate scaling video frame");
            return false;
          }
          uint64_t reqWidth = frame_RAW->width;
          uint64_t reqHeight = frame_RAW->height;
          if (opt.isMember("resolution") && opt["resolution"]){
            reqWidth = strtol(opt["resolution"].asString().substr(0, opt["resolution"].asString().find("x")).c_str(), NULL, 0);
            reqHeight = strtol(opt["resolution"].asString().substr(opt["resolution"].asString().find("x") + 1).c_str(), NULL, 0);
          }
          frameConverted->format = convertToPixFmt;
          frameConverted->width  = reqWidth;
          frameConverted->height = reqHeight;
          frameConverted->flags |= AV_CODEC_FLAG_QSCALE;
          frameConverted->quality = FF_QP2LAMBDA * Mist::opt["quality"].asInt();;
          int ret = av_image_alloc(frameConverted->data, frameConverted->linesize, frameConverted->width, frameConverted->height, convertToPixFmt, 32);
          if (ret < 0) {
            av_frame_free(&frameConverted);
            frameConverted = 0;
            FAIL_MSG("Could not allocate converted frame buffer");
            return false;
          }
          INFO_MSG("Allocated converted frame buffer");
        }
        // Convert RAW frame to a target codec compatible pixel format
        sws_scale(convertCtx, (const uint8_t * const *)frame_RAW->data, frame_RAW->linesize, 0, frame_RAW->height, frameConverted->data, frameConverted->linesize);
      }
      return true;
    }

    /// \brief Transforms the RAW audio buffer to have required output properties
    bool transformAudioFrame(){
      // Create context to resample audio
      // NOTE: this is done here, rather than in the allocate and config
      //        functions, as frame_RAW is initted during decoding 
      if (!resampleContext){
        INFO_MSG("Allocating resampling context...");
        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          resampleContext = swr_alloc_set_opts(NULL,
                                              context_out->channel_layout,
                                              context_out->sample_fmt,
                                              context_out->sample_rate,
                                              frame_RAW->channel_layout,
                                              (AVSampleFormat)frame_RAW->format,
                                              frame_RAW->sample_rate,
                                              0, NULL);
        #else
          swr_alloc_set_opts2(&resampleContext,
                              &context_out->ch_layout,
                              context_out->sample_fmt,
                              context_out->sample_rate,
                              &frame_RAW->ch_layout,
                              (AVSampleFormat)frame_RAW->format,
                              frame_RAW->sample_rate,
                              0, NULL);
        #endif

        if (!resampleContext) {
          FAIL_MSG("Could not allocate resampling context");
          return false;
        }
        int ret = swr_init(resampleContext);
        if (ret < 0) { 
          FAIL_MSG("Could not open resample context");
          swr_free(&resampleContext);
          return false;
        }
        INFO_MSG("Allocated resampling context!");
      }

      // Create frame to hold converted samples
      if (!frameConverted){
        INFO_MSG("Allocating converted frame...");
        frameConverted = av_frame_alloc();
        if (!frameConverted) {
          FAIL_MSG("Could not allocate audio resampling frame");
          return false;
        }
        // If we have a target codec, we need to transform to something compatible with the output
        frameConverted->sample_rate = context_out->sample_rate;
        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          frameConverted->channels = context_out->channels;
          frameConverted->channel_layout = context_out->channel_layout;
        #else
          frameConverted->ch_layout.nb_channels = context_out->ch_layout.nb_channels;
          frameConverted->ch_layout = context_out->ch_layout;
        #endif
        frameConverted->format = context_out->sample_fmt;
        // Since PCM has no concept of a frame, retain input frame size
        if (codecOut == "PCM"){
          frameConverted->nb_samples = frame_RAW->nb_samples;
        }else{
          frameConverted->nb_samples = context_out->frame_size;
        }
        resampledSampleCount = swr_get_out_samples(resampleContext, frame_RAW->nb_samples) * 3 + frame_RAW->nb_samples;
        frameConverted->nb_samples = resampledSampleCount;

        // Allocate buffers
        int ret = av_frame_get_buffer(frameConverted, 0);
        if (ret < 0) {
          printError("Could not allocate output frame sample buffer", ret);
          av_frame_free(&frameConverted);
          return false;
        }else{
          INFO_MSG("Allocated converted frame!");
        }
      }

      // Allocate local audio sample buffer
      static uint8_t **converted_input_samples = NULL;
      int ret;
      if (!converted_input_samples){
        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          ret = av_samples_alloc_array_and_samples(&converted_input_samples, NULL, context_out->channels,
                                                  resampledSampleCount, context_out->sample_fmt, 0);
        #else
          ret = av_samples_alloc_array_and_samples(&converted_input_samples, NULL, context_out->ch_layout.nb_channels,
                                                  resampledSampleCount, context_out->sample_fmt, 0);
        #endif
        if (ret < 0) {
          printError("Could not allocate converted input samples", ret);
          return false;
        }
      }

      // Resample audio
      int samplesConverted = swr_convert(resampleContext, converted_input_samples, resampledSampleCount,
                        (const uint8_t**)frame_RAW->extended_data, frame_RAW->nb_samples);
      if (samplesConverted < 0) {
        printError("Could not convert input samples", ret);
        return false;
      }

      // Allocate resampling audio buffer
      if (!audioBuffer){
        INFO_MSG("Allocating audio sample buffer...");
        #if (LIBAVUTIL_VERSION_MAJOR < 57 || (LIBAVUTIL_VERSION_MAJOR == 57 && LIBAVUTIL_VERSION_MINOR < 24))
          audioBuffer = av_audio_fifo_alloc(context_out->sample_fmt, context_out->channels, resampledSampleCount);
        #else
          audioBuffer = av_audio_fifo_alloc(context_out->sample_fmt, context_out->ch_layout.nb_channels, resampledSampleCount);
        #endif
        if (!audioBuffer){
          FAIL_MSG("Could not allocate audio buffer");
          return false;
        }
        INFO_MSG("Allocated audio buffer");
      }

      // Copy local buffer over to audio sample buffer
      if (av_audio_fifo_write(audioBuffer, (void **)converted_input_samples, samplesConverted) < samplesConverted) {
        printError("Could not write data to audio sample buffer", ret);
        return false;
      }
      return true;
    }

    /// @brief Takes raw video buffer and encode it to create an output packet
    void encodeVideo(){
      // Encode to target codec. Force P frame to prevent keyframe-only outputs from appearing
      int ret;
      frameConverted->pict_type = AV_PICTURE_TYPE_P;
      frameConverted->pts++;
      {
        // Encode frame
        if (frameDecodeHW && frameInHW && frameDecodeHW != frame_RAW && softDecodeFormat == softFormat){
          ret = av_hwframe_transfer_data(frameInHW, frameDecodeHW, 0);
          if (ret){
            printError("Unable to transfer frame between the decoder and encoder", ret);
            return;
          }
          ret = avcodec_send_frame(context_out, frameInHW);
        }else{
          if (frameInHW){
            ret = av_hwframe_transfer_data(frameInHW, frameConverted, 0);
            if (ret){
              printError("Unable to upload frame to the hardware", ret);
              return;
            }
            ret = avcodec_send_frame(context_out, frameInHW);
          }else{
            ret = avcodec_send_frame(context_out, frameConverted);
          }
        }
        if (!ret){
        }
      }
      if (ret < 0){printError("Unable to send frame to the encoder", ret);}

      {
        uint64_t sleepTime = Util::getMicros();
        std::unique_lock<std::mutex> lk(avMutex);
        avCV.wait(lk,[](){return !frameReady || !config->is_active;});
        totalSourceSleep += Util::getMicros(sleepTime);
        frameTimes.push_back(thisTime);
        ++outputFrameCount;
        // Retrieve encoded packet
        ret = avcodec_receive_packet(context_out, packet_out);
        if (ret < 0){
          return;
        }
        frameReady = true;
      }
      avCV.notify_all();
    }

    /// @brief Takes raw audio buffer and encode it to create an output packet
    void encodeAudio(){
      int ret;
      int reqSize = 0;
      if (context_out->frame_size){
        reqSize = context_out->frame_size;
      }else{
        reqSize = context_out->time_base.den / 50;
      }
      frameConverted->nb_samples = reqSize;
      // Check if we have enough samples to fill a frame
      if (av_audio_fifo_size(audioBuffer) < reqSize){
        HIGH_MSG("Encoder requires more audio samples...");
        return;
      }
      while (av_audio_fifo_size(audioBuffer) >= reqSize){
        // Buffer audio samples into frame
        if (av_audio_fifo_read(audioBuffer, (void **)frameConverted->data, reqSize) < reqSize) {
          FAIL_MSG("Could not read data from audio buffer");
          return;
        }
        // Update PTS
        frameConverted->pts = pts;
        pts += reqSize;
        // Encode frame
        ret = avcodec_send_frame(context_out, frameConverted);
        if (!ret){
        }
        if (ret < 0) {
          printError("Unable to send frame to the encoder", ret);
          return;
        }
        {
          uint64_t sleepTime = Util::getMicros();
          std::unique_lock<std::mutex> lk(avMutex);
          avCV.wait(lk,[](){return !frameReady || !config->is_active;});
          totalSourceSleep += Util::getMicros(sleepTime);

          // Retrieve encoded packet
          ret = avcodec_receive_packet(context_out, packet_out);
          if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            HIGH_MSG("Encoder requires more input samples...");
            return;
          }else if (ret < 0) {
            printError("Unable to encode", ret);
            return;
          }

          frameTimes.push_back(sendPacketTime + (packet_out->pts * 1000 * context_out->time_base.num) / context_out->time_base.den);
          outputFrameCount = (char*)packet_out->pts;
          
          frameReady = true;
        }
        avCV.notify_all();
      }
    }

    /// @brief Sends RAW video frames to the output using `ptr` rather than going through LibAV's contexts
    void sendRawVideo(){
      int sizeNeeded = av_image_get_buffer_size((AVPixelFormat)frameConverted->format, frameConverted->width, frameConverted->height, 32);
      ptr.allocate(sizeNeeded);
      ptr.truncate(0);
      int bytes = av_image_copy_to_buffer((uint8_t*)(char*)ptr, ptr.rsize(), frameConverted->data, frameConverted->linesize, (AVPixelFormat)frameConverted->format, frameConverted->width, frameConverted->height, 32);
      if (bytes > 0){
        {
          uint64_t sleepTime = Util::getMicros();
          std::unique_lock<std::mutex> lk(avMutex);
          avCV.wait(lk,[](){return !frameReady || !config->is_active;});
          totalSourceSleep += Util::getMicros(sleepTime);
          // Adjust ptr size to how many bytes were actually written
          ptr.append(0, bytes);
          frameTimes.push_back(thisTime);
          ++outputFrameCount;
          frameReady = true;
        }
        avCV.notify_all();
      }
    }

    void sendNext(){
      // Wait for the other side to process the last frame that was ready for buffering
      if (!config->is_active){return;}

      {
        std::lock_guard<std::mutex> guard(statsMutex);
        if (pData["source_tracks"].size() != userSelect.size()){
          pData["source_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["source_tracks"].append((uint64_t)it->first);
          }
        }
        if (codecIn.size() && pData["source_codec"].asStringRef() != codecIn){pData["source_codec"] = codecIn;}
      }

      if (thisTime > statSourceMs){statSourceMs = thisTime;}

      // Keyframe only mode for MJPEG output
      if (codecOut == "JPEG"){
        ++skippedFrames;
        if(!thisPacket.getFlag("keyframe") || skippedFrames < Mist::opt["gopsize"].asInt()){
          sinkClass->setNowMS(thisTime);
          return;
        }
        skippedFrames = 0;
      }

      needsLookAhead = 0;
      maxSkipAhead = 0;
      realTime = 0;
      if (!sendFirst){
        sendPacketTime = thisTime;
        bootMsOffset = M.getBootMsOffset();
        sendFirst = true;
      }

      // Retrieve packet buffer pointers
      size_t dataLen = 0;
      char *dataPointer = 0;
      thisPacket.getString("data", dataPointer, dataLen);

      // Allocate encoding/decoding contexts and decode the input if not RAW
      if (isVideo){
        if (frame_RAW && frame_RAW->width && frame_RAW->height){
          // If resolution changes, destruct FRAME_RAW and SCALE
          uint64_t inWidth = M.getWidth(getMainSelectedTrack());
          uint64_t inHeight = M.getHeight(getMainSelectedTrack());
          if (inWidth != frame_RAW->width || inHeight != frame_RAW->height){
            WARN_MSG("Input resolution changed from %ix%i to %lux%lu. Reallocating contexts...", frame_RAW->width, frame_RAW->height, inWidth, inHeight);
            // Make sure a new RAW frame and scaling context get allocated
            if (frame_RAW != frameDecodeHW){av_frame_free(&frame_RAW);}
            frame_RAW = 0;
            sws_freeContext(convertCtx);
            convertCtx = NULL;
            context_in->height = inHeight;
            context_in->width = inWidth;
            // Realloc HW frame
            if (frameDecodeHW){
              AVBufferRef *oldBuffer = context_in->hw_frames_ctx;

              INFO_MSG("Creating hw frame context");
              AVBufferRef *newBuffer = av_hwframe_ctx_alloc(hw_decode_ctx);

              AVHWFramesContext *frames_ctx;
              frames_ctx = (AVHWFramesContext *)(newBuffer->data);
              frames_ctx->initial_pool_size = 1;
              frames_ctx->format    = hw_decode_fmt;
              frames_ctx->sw_format = hw_decode_sw_fmt;
              frames_ctx->width     = inWidth;
              frames_ctx->height    = inHeight;

              av_hwframe_ctx_init(newBuffer);
              context_in->hw_frames_ctx = av_buffer_ref(newBuffer);
              av_buffer_unref(&oldBuffer);

              INFO_MSG("Creating hw frame memory");

              av_frame_free(&frameDecodeHW);
              frameDecodeHW = av_frame_alloc();
              av_hwframe_get_buffer(context_in->hw_frames_ctx, frameDecodeHW, 0);
            }
          }
        }
        allocateVideoEncoder();
        if (!configVideoDecoder()){ return; }
        uint64_t startTime = Util::getMicros();
        if (!decodeVideoFrame(dataPointer, dataLen)){ return; }
        uint64_t decodeTime = Util::getMicros();
        if(!transformVideoFrame()){ return; }
        uint64_t transformTime = Util::getMicros();
        totalDecode += decodeTime - startTime;
        totalTransform += transformTime - decodeTime;
      }else{
        if (!configAudioDecoder()){ return; }
        // Since PCM has a 'codec' in LibAV, handle all decoding using the generic function
        if (!decodeFrame(dataPointer, dataLen)){ return; }
        inputFrameCount += frame_RAW->nb_samples;
        if (sendPacketTime + (((size_t)inputFrameCount)*1000)/M.getRate(thisIdx) < thisTime){
          sendPacketTime += thisTime - (sendPacketTime + (((size_t)inputFrameCount)*1000)/M.getRate(thisIdx));
        }
        allocateAudioEncoder();
        if(!transformAudioFrame()){ return; }
      }

      // If the output is RAW, immediately send it to the output using `ptr` rather than going through LibAV's contexts
      if (!context_out && isVideo){
        sendRawVideo();
        return;
      }

      // If the output was not RAW, encode the RAW buffers
      if (isVideo){
        uint64_t startTime = Util::getMicros();
        encodeVideo();
        totalEncode += Util::getMicros(startTime);
      }else{
        encodeAudio();
      }
    }
  };

  /// check source, sink, source_track, codec, bitrate, flags  and process options.
  bool ProcAV::CheckConfig(){
    // Check generic configuration variables
    if (!opt.isMember("source") || !opt["source"] || !opt["source"].isString()){
      FAIL_MSG("invalid source in config!");
      return false;
    }

    if (!opt.isMember("sink") || !opt["sink"] || !opt["sink"].isString()){
      INFO_MSG("No sink explicitly set, using source as sink");
    }

    return true;
  }

  void ProcAV::Run(){
    uint64_t lastProcUpdate = Util::bootSecs();
    {
      std::lock_guard<std::mutex> guard(statsMutex);
      pStat["proc_status_update"]["id"] = getpid();
      pStat["proc_status_update"]["proc"] = "AV";
    }
    uint64_t startTime = Util::bootSecs();
    uint64_t encPrevTime = 0;
    uint64_t encPrevCount = 0;
    while (conf.is_active && co.is_active){
      Util::sleep(200);
      if (lastProcUpdate + 5 <= Util::bootSecs()){
        std::lock_guard<std::mutex> guard(statsMutex);
        pData["active_seconds"] = (Util::bootSecs() - startTime);
        pData["ainfo"]["sourceTime"] = statSourceMs;
        pData["ainfo"]["sinkTime"] = statSinkMs;
        pData["ainfo"]["inFrames"] = (uint64_t)inputFrameCount;
        pData["ainfo"]["outFrames"] = (uint64_t)outputFrameCount;
        if ((uint64_t)inputFrameCount){
          pData["ainfo"]["decodeTime"] = totalDecode / (uint64_t)inputFrameCount / 1000;
          pData["ainfo"]["transformTime"] = totalTransform / (uint64_t)inputFrameCount / 1000;
          pData["ainfo"]["sinkSleepTime"] = totalSinkSleep / (uint64_t)inputFrameCount / 1000;
          pData["ainfo"]["sourceSleepTime"] = totalSourceSleep / (uint64_t)inputFrameCount / 1000;
        }
        if ((uint64_t)outputFrameCount > encPrevCount){
          pData["ainfo"]["encodeTime"] = (totalEncode - encPrevTime) / (((uint64_t)outputFrameCount)-encPrevCount) / 1000;
          encPrevTime = totalEncode;
          encPrevCount = (uint64_t)outputFrameCount;
        }
        if (codec_out){
          pData["ainfo"]["encoder"] = codec_out->long_name;
        }else{
          pData["ainfo"]["encoder"] = "None (raw)";
        }
        if (codec_in){
          pData["ainfo"]["decoder"] = codec_in->long_name;
        }else{
          pData["ainfo"]["decoder"] = "None (raw)";
        }
        pData["ainfo"]["scaler"] = scaler;
        Util::sendUDPApi(pStat);
        lastProcUpdate = Util::bootSecs();
      }
    }
  }
}// namespace Mist

void sinkThread(){
  Util::nameThread("sinkThread");
  Mist::ProcessSink in(&co);
  Mist::sinkClass = &in;
  co.getOption("output", true).append("-");
  MEDIUM_MSG("Running sink thread...");
  in.run();
  INFO_MSG("Stop sink thread...");
  conf.is_active = false;
  avCV.notify_all();
}

void sourceThread(){
  Util::nameThread("sourceThread");
  Mist::ProcessSource::init(&conf);
  conf.getOption("streamname", true).append(Mist::opt["source"].c_str());
  JSON::Value opt;
  opt["arg"] = "string";
  opt["default"] = "";
  opt["arg_num"] = 1;
  conf.addOption("target", opt);
  std::string video_select = "maxbps";
  if (Mist::opt.isMember("source_track") && Mist::opt["source_track"].isString() && Mist::opt["source_track"]){
    video_select = Mist::opt["source_track"].asStringRef();
  }
  conf.getOption("target", true).append("-");
  Socket::Connection S;
  Mist::ProcessSource out(S);
  MEDIUM_MSG("Running source thread...");
  out.run();
  INFO_MSG("Stop source thread...");
  co.is_active = false;
  avCV.notify_all();
}

/// \brief Custom log function for LibAV-related messages
/// \param level: Log level according to LibAV. Does not print if higher than global `av_logLevel`
void logcallback(void *ptr, int level, const char *fmt, va_list vl){
  if (level > av_logLevel){return;}
  static int print_prefix = 1;
  char line[1024];
  av_log_format_line(ptr, level, fmt, vl, line, sizeof(line), &print_prefix);
  std::string ll(line);
  if (ll.find("specified frame type") != std::string::npos || ll.find("rc buffer underflow") != std::string::npos){
    HIGH_MSG("LibAV: %s", line);
  }else{
    INFO_MSG("LibAV: %s", line);
  }
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  Util::Config::binaryType = Util::PROCESS;
  JSON::Value capa;
  context_out = NULL;
  av_log_set_callback(logcallback);

  {
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "-";
    opt["arg_num"] = 1;
    opt["help"] = "JSON configuration, or - (default) to read from stdin";
    config.addOption("configuration", opt);
    opt.null();
    opt["long"] = "json";
    opt["short"] = "j";
    opt["help"] = "Output connector info in JSON format, then exit.";
    opt["value"].append(0);
    config.addOption("json", opt);
  }

  capa["codecs"][0u][0u].append("YUYV");
  capa["codecs"][0u][0u].append("NV12");
  capa["codecs"][0u][0u].append("UYVY");
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][0u].append("AV1");
  capa["codecs"][0u][0u].append("JPEG");
  capa["codecs"][0u][0u].append("PCM");
  capa["codecs"][0u][0u].append("opus");
  capa["codecs"][0u][0u].append("AAC");

  capa["ainfo"]["sinkTime"]["name"] = "Sink timestamp";
  capa["ainfo"]["sourceTime"]["name"] = "Source timestamp";
  capa["ainfo"]["child_pid"]["name"] = "Child process PID";
  capa["ainfo"]["cmd"]["name"] = "Child process command";
  capa["ainfo"]["inFrames"]["name"] = "Frames ingested";
  capa["ainfo"]["outFrames"]["name"] = "Frames outputted";
  capa["ainfo"]["decodeTime"]["name"] = "Decode time";
  capa["ainfo"]["sinkSleepTime"]["name"] = "Sink sleep time";
  capa["ainfo"]["sourceSleepTime"]["name"] = "Source sleep time";
  capa["ainfo"]["transformTime"]["name"] = "Transform time";
  capa["ainfo"]["encodeTime"]["name"] = "Encode time";
  capa["ainfo"]["decodeTime"]["unit"] = "ms / frame";
  capa["ainfo"]["transformTime"]["unit"] = "ms / frame";
  capa["ainfo"]["encodeTime"]["unit"] = "ms / frame";
  capa["ainfo"]["sourceSleepTime"]["unit"] = "ms / frame";
  capa["ainfo"]["sinkSleepTime"]["unit"] = "ms / frame";
  capa["ainfo"]["decoder"]["name"] = "Decoder";
  capa["ainfo"]["encoder"]["name"] = "Encoder";
  capa["ainfo"]["scaler"]["name"] = "Scaler";

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "AV";
    capa["hrn"] = "Encoder: libav (ffmpeg library)";
    capa["desc"] = "Generic video encoder that directly integrates with the ffmpeg library rather than calling a binary and transmuxing twice";
    addGenericProcessOptions(capa);

    capa["optional"]["source_mask"]["name"] = "Source track mask";
    capa["optional"]["source_mask"]["help"] = "What internal processes should have access to the source track(s)";
    capa["optional"]["source_mask"]["type"] = "select";
    capa["optional"]["source_mask"]["select"][0u][0u] = "";
    capa["optional"]["source_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["source_mask"]["select"][1u][0u] = 255;
    capa["optional"]["source_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["source_mask"]["select"][2u][0u] = 4;
    capa["optional"]["source_mask"]["select"][2u][1u] = "Processing tasks (not viewers, not pushes)";
    capa["optional"]["source_mask"]["select"][3u][0u] = 6;
    capa["optional"]["source_mask"]["select"][3u][1u] = "Processing and pushing tasks (not viewers)";
    capa["optional"]["source_mask"]["select"][4u][0u] = 5;
    capa["optional"]["source_mask"]["select"][4u][1u] = "Processing and viewer tasks (not pushes)";
    capa["optional"]["source_mask"]["default"] = "";

    capa["optional"]["target_mask"]["name"] = "Output track mask";
    capa["optional"]["target_mask"]["help"] = "What internal processes should have access to the ouput track(s)";
    capa["optional"]["target_mask"]["type"] = "select";
    capa["optional"]["target_mask"]["select"][0u][0u] = "";
    capa["optional"]["target_mask"]["select"][0u][1u] = "Keep original value";
    capa["optional"]["target_mask"]["select"][1u][0u] = 255;
    capa["optional"]["target_mask"]["select"][1u][1u] = "Everything";
    capa["optional"]["target_mask"]["select"][2u][0u] = 1;
    capa["optional"]["target_mask"]["select"][2u][1u] = "Viewer tasks (not processing, not pushes)";
    capa["optional"]["target_mask"]["select"][3u][0u] = 2;
    capa["optional"]["target_mask"]["select"][3u][1u] = "Pushing tasks (not processing, not viewers)";
    capa["optional"]["target_mask"]["select"][4u][0u] = 4;
    capa["optional"]["target_mask"]["select"][4u][1u] = "Processing tasks (not pushes, not viewers)";
    capa["optional"]["target_mask"]["select"][5u][0u] = 3;
    capa["optional"]["target_mask"]["select"][5u][1u] = "Viewer and pushing tasks (not processing)";
    capa["optional"]["target_mask"]["select"][6u][0u] = 5;
    capa["optional"]["target_mask"]["select"][6u][1u] = "Viewer and processing tasks (not pushes)";
    capa["optional"]["target_mask"]["select"][7u][0u] = 6;
    capa["optional"]["target_mask"]["select"][7u][1u] = "Pushing and processing tasks (not viewers)";
    capa["optional"]["target_mask"]["select"][8u][0u] = 0;
    capa["optional"]["target_mask"]["select"][8u][1u] = "Nothing";
    capa["optional"]["target_mask"]["default"] = "3";

    capa["optional"]["exit_unmask"]["name"] = "Undo masks on process exit/fail";
    capa["optional"]["exit_unmask"]["help"] = "If/when the process exits or fails, the masks for input tracks will be reset to defaults. (NOT to previous value, but to defaults!)";
    capa["optional"]["exit_unmask"]["default"] = false;

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] = "What stream the encoded track should be added to. Defaults "
                                       "to source stream. May contain variables.";
    capa["optional"]["sink"]["type"] = "string";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    capa["optional"]["track_select"]["name"] = "Source selector(s)";
    capa["optional"]["track_select"]["help"] =
        "What tracks to select for the input. Defaults to audio=all&video=all.";
    capa["optional"]["track_select"]["type"] = "string";
    capa["optional"]["track_select"]["validate"][0u] = "track_selector";
    capa["optional"]["track_select"]["default"] = "audio=all&video=all";

    capa["optional"]["gopsize"]["name"] = "GOP Size";
    capa["optional"]["gopsize"]["help"] = "Amount of frames before a new keyframe is sent. When outputting JPEG, this is the minimum amount of frames between images.";
    capa["optional"]["gopsize"]["type"] = "uint";
    capa["optional"]["gopsize"]["default"] = 40;

    capa["required"]["codec"]["name"] = "Target codec";
    capa["required"]["codec"]["help"] = "Wanted output codec";
    capa["required"]["codec"]["type"] = "select";
    capa["required"]["codec"]["select"][0u][0u] = "H264";
    capa["required"]["codec"]["select"][0u][1u] = "H264";
    capa["required"]["codec"]["select"][1u][0u] = "AV1";
    capa["required"]["codec"]["select"][1u][1u] = "AV1";
    capa["required"]["codec"]["select"][2u][0u] = "JPEG";
    capa["required"]["codec"]["select"][2u][1u] = "JPEG";
    capa["required"]["codec"]["select"][3u][0u] = "YUYV";
    capa["required"]["codec"]["select"][3u][1u] = "YUYV: Raw YUV 4:2:2 pixels";
    capa["required"]["codec"]["select"][4u][0u] = "UYVY";
    capa["required"]["codec"]["select"][4u][1u] = "UYVY: Raw YUV 4:2:2 pixels";
    capa["required"]["codec"]["select"][5u][0u] = "PCM";
    capa["required"]["codec"]["select"][5u][1u] = "PCM";
    capa["required"]["codec"]["select"][6u][0u] = "opus";
    capa["required"]["codec"]["select"][6u][1u] = "opus";
    capa["required"]["codec"]["select"][7u][0u] = "AAC";
    capa["required"]["codec"]["select"][7u][1u] = "AAC";

    capa["optional"]["accel"]["name"] = "Hardware acceleration";
    capa["optional"]["accel"]["help"] = "Control whether hardware acceleration is used or not";
    capa["optional"]["accel"]["type"] = "select";
    capa["optional"]["accel"]["select"][0u][0u] = "";
    capa["optional"]["accel"]["select"][0u][1u] = "Automatic (attempt acceleration, fallback to software)";
    capa["optional"]["accel"]["select"][1u][0u] = "hw";
    capa["optional"]["accel"]["select"][1u][1u] = "Force enabled (abort if acceleration fails)";
    capa["optional"]["accel"]["select"][2u][0u] = "sw";
    capa["optional"]["accel"]["select"][2u][1u] = "Software-only (do not attempt acceleration)";
    capa["optional"]["accel"]["default"] = "";

    capa["optional"]["tune"]["name"] = "Encode tuning";
    capa["optional"]["tune"]["help"] = "Set the encode tuning";
    capa["optional"]["tune"]["type"] = "select";
    capa["optional"]["tune"]["select"][0u][0u] = "zerolatency";
    capa["optional"]["tune"]["select"][0u][1u] = "Low latency (default)";
    capa["optional"]["tune"]["select"][1u][0u] = "zerolatency-lq";
    capa["optional"]["tune"]["select"][1u][1u] = "Low latency (high speed / low quality)";
    capa["optional"]["tune"]["select"][2u][0u] = "zerolatency-hq";
    capa["optional"]["tune"]["select"][2u][1u] = "Low latency (low speed / high quality)";
    capa["optional"]["tune"]["select"][3u][0u] = "animation";
    capa["optional"]["tune"]["select"][3u][1u] = "Cartoon-like content";
    capa["optional"]["tune"]["select"][4u][0u] = "film";
    capa["optional"]["tune"]["select"][4u][1u] = "Movie-like content";
    capa["optional"]["tune"]["select"][5u][0u] = "";
    capa["optional"]["tune"]["select"][5u][1u] = "No tuning (generic)";
    capa["optional"]["tune"]["default"] = "zerolatency";
    capa["optional"]["tune"]["sort"] = "ccc";

    capa["optional"]["preset"]["name"] = "Transcode preset";
    capa["optional"]["preset"]["help"] = "Preset for encoding speed and compression ratio";
    capa["optional"]["preset"]["type"] = "select";
    capa["optional"]["preset"]["select"][0u][0u] = "ultrafast";
    capa["optional"]["preset"]["select"][0u][1u] = "ultrafast";
    capa["optional"]["preset"]["select"][1u][0u] = "superfast";
    capa["optional"]["preset"]["select"][1u][1u] = "superfast";
    capa["optional"]["preset"]["select"][2u][0u] = "veryfast";
    capa["optional"]["preset"]["select"][2u][1u] = "veryfast";
    capa["optional"]["preset"]["select"][3u][0u] = "faster";
    capa["optional"]["preset"]["select"][3u][1u] = "faster";
    capa["optional"]["preset"]["select"][4u][0u] = "fast";
    capa["optional"]["preset"]["select"][4u][1u] = "fast";
    capa["optional"]["preset"]["select"][5u][0u] = "medium";
    capa["optional"]["preset"]["select"][5u][1u] = "medium";
    capa["optional"]["preset"]["select"][6u][0u] = "slow";
    capa["optional"]["preset"]["select"][6u][1u] = "slow";
    capa["optional"]["preset"]["select"][7u][0u] = "slower";
    capa["optional"]["preset"]["select"][7u][1u] = "slower";
    capa["optional"]["preset"]["select"][8u][0u] = "veryslow";
    capa["optional"]["preset"]["select"][8u][1u] = "veryslow";
    capa["optional"]["preset"]["default"] = "faster";
    capa["optional"]["preset"]["sort"] = "ccb";

    capa["optional"]["bitrate"]["name"] = "Bitrate";
    capa["optional"]["bitrate"]["help"] = "Set the target bitrate in bits per second";
    capa["optional"]["bitrate"]["type"] = "uint";
    capa["optional"]["bitrate"]["default"] = 2000000;
    capa["optional"]["bitrate"]["unit"][0u][0u] = "1";
    capa["optional"]["bitrate"]["unit"][0u][1u] = "bit/s";
    capa["optional"]["bitrate"]["unit"][1u][0u] = "1000";
    capa["optional"]["bitrate"]["unit"][1u][1u] = "kbit/s";
    capa["optional"]["bitrate"]["unit"][2u][0u] = "1000000";
    capa["optional"]["bitrate"]["unit"][2u][1u] = "Mbit/s";
    capa["optional"]["bitrate"]["unit"][3u][0u] = "1000000000";
    capa["optional"]["bitrate"]["unit"][3u][1u] = "Gbit/s";


    capa["optional"]["resolution"]["name"] = "resolution";
    capa["optional"]["resolution"]["help"] = "Resolution of the output stream, e.g. 1920x1080";
    capa["optional"]["resolution"]["type"] = "str";
    capa["optional"]["resolution"]["default"] = "keep source resolution";
    capa["optional"]["resolution"]["sort"] = "aca";
    capa["optional"]["resolution"]["dependent"]["x-LSP-kind"] = "video";

    capa["optional"]["quality"]["name"] = "Quality";
    capa["optional"]["quality"]["help"] = "Level of compression similar to the `qscale` option in FFMPEG. Takes on a value between 1-31. A lower value provides a better quality output";
    capa["optional"]["quality"]["type"] = "int";
    capa["optional"]["quality"]["default"] = 20;
    capa["optional"]["quality"]["min"] = 1;
    capa["optional"]["quality"]["max"] = 31;

    std::cout << capa.toString() << std::endl;
    return -1;
  }

  Util::redirectLogsIfNeeded();

  // read configuration
  if (config.getString("configuration") != "-"){
    Mist::opt = JSON::fromString(config.getString("configuration"));
  }else{
    std::string json, line;
    INFO_MSG("Reading configuration from standard input");
    while (std::getline(std::cin, line)){json.append(line);}
    Mist::opt = JSON::fromString(json.c_str());
  }

  if (!Mist::opt.isMember("gopsize") || !Mist::opt["gopsize"].asInt()){
    Mist::opt["gopsize"] = 40;
  }

  if (!Mist::opt.isMember("bitrate") || !Mist::opt["bitrate"].asInt()){
    Mist::opt["bitrate"] = 2000000;
  }

  if (!Mist::opt.isMember("quality") || !Mist::opt["quality"].asInt()){
    Mist::opt["quality"] = 20;
  }

  if (Mist::opt.isMember("accel") && Mist::opt["accel"].isString()){
    if (Mist::opt["accel"].asStringRef() == "hw"){
      allowSW = false;
    }
    if (Mist::opt["accel"].asStringRef() == "sw"){
      allowHW = false;
    }
  }

  if (!Mist::opt.isMember("codec") || !Mist::opt["codec"] || !Mist::opt["codec"].isString() || Mist::opt["codec"].asStringRef() == "H264"){
    isVideo = true;
    codecOut = "H264";
    codec_out = 0;
  }else if (Mist::opt["codec"].asStringRef() == "AV1"){
    isVideo = true;
    codecOut = "AV1";
    codec_out = 0;
  }else if (Mist::opt["codec"].asStringRef() == "JPEG"){
    isVideo = true;
    codecOut = "JPEG";
    codec_out = 0;
  }else if (Mist::opt["codec"].asStringRef() == "YUYV"){
    isVideo = true;
    codecOut = "YUYV";
    codec_out = 0;
  }else if (Mist::opt["codec"].asStringRef() == "UYVY"){
    isVideo = true;
    codecOut = "UYVY";
    codec_out = 0;
  }else if (Mist::opt["codec"].asStringRef() == "PCM"){
    isVideo = false;
    codecOut = "PCM";
    codec_out = avcodec_find_encoder(AV_CODEC_ID_PCM_S32BE);
  }else if (Mist::opt["codec"].asStringRef() == "opus"){
    isVideo = false;
    codecOut = "opus";
    codec_out = avcodec_find_encoder(AV_CODEC_ID_OPUS);
  }else if (Mist::opt["codec"].asStringRef() == "AAC"){
    isVideo = false;
    codecOut = "AAC";
    codec_out = avcodec_find_encoder(AV_CODEC_ID_AAC);
  }else{
    FAIL_MSG("Unknown codec: %s", Mist::opt["codec"].asStringRef().c_str());
    return 1;
  }

  // check config for generic options
  Mist::ProcAV Enc;
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }

  // Allocate packet
  packet_out = av_packet_alloc();

  co.is_active = true;
  conf.is_active = true;

  // stream which connects to input
  std::thread source(sourceThread);

  // needs to pass through encoder to outputEBML
  std::thread sink(sinkThread);

  // run process
  Enc.Run();

  co.is_active = false;
  conf.is_active = false;
  avCV.notify_all();

  source.join();
  HIGH_MSG("source thread joined");

  sink.join();
  HIGH_MSG("sink thread joined");

  if (context_out){avcodec_free_context(&context_out);}
  av_packet_free(&packet_out);

  return 0;
}
