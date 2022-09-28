#include "output_jpg.h"
#include <fstream>
#include <mist/bitfields.h>
#include <mist/mp4_generic.h>
#include <mist/procs.h>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

namespace Mist{
  OutJPG::OutJPG(Socket::Connection &conn) : HTTPOutput(conn){
    HTTP = false;
    cachedir = config->getString("cachedir");
    if (cachedir.size()){
      cachedir += "/MstJPEG" + streamName;
      cachetime = config->getInteger("cachetime");
    }else{
      cachetime = 0;
    }
    if (config->getString("target").size()){
      initialize();
      if (!streamName.size()){
        WARN_MSG("Recording unconnected JPG output to file! Cancelled.");
        conn.close();
        return;
      }
      if (!M){
        INFO_MSG("Stream not available - aborting");
        conn.close();
        return;
      }
      if (!userSelect.size()){
        INFO_MSG("Stream codec not supported - aborting");
        conn.close();
        return;
      }
      // We generate a thumbnail first, then output it if successful
      generate();
      if (!jpg_buffer.str().size()){
        // On failure, report, but do not open the file or write anything
        FAIL_MSG("Could not generate thumbnail for %s", streamName.c_str());
        myConn.close();
        return;
      }
      if (config->getString("target") == "-"){
        INFO_MSG("Outputting %s to stdout in JPG format", streamName.c_str());
      }else{
        if (!connectToFile(config->getString("target"))){
          myConn.close();
          return;
        }
        INFO_MSG("Recording %s to %s in JPG format", streamName.c_str(), config->getString("target").c_str());
      }
      myConn.SendNow(jpg_buffer.str().c_str(), jpg_buffer.str().size());
      myConn.close();
      return;
    }
  }

  /// Pretends the stream is always ready to play - we don't care about waiting times or whatever
  bool OutJPG::isReadyForPlay(){return true;}

  void OutJPG::initialSeek(){
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack == INVALID_TRACK_ID){return;}
    INFO_MSG("Doing initial seek");
    if (M.getLive()){
      liveSeek();
      uint32_t targetKey = M.getKeyIndexForTime(mainTrack, currentTime());
      seek(M.getTimeForKeyIndex(mainTrack, targetKey));
      return;
    }
    // cancel if there are no keys in the main track
    if (!M.getValidTracks().count(mainTrack) || !M.getLastms(mainTrack)){
      WARN_MSG("Aborted vodSeek because no tracks selected");
      return;
    }

    uint64_t seekPos = M.getFirstms(mainTrack) + (M.getLastms(mainTrack) - M.getFirstms(mainTrack)) / 2;
    bool didSeek = false;
    size_t retries = 10;
    while (!didSeek && --retries){
      MEDIUM_MSG("VoD seek to %" PRIu64 "ms", seekPos);
      uint32_t targetKey = M.getKeyIndexForTime(mainTrack, seekPos);
      didSeek = seek(M.getTimeForKeyIndex(mainTrack, targetKey));
      if (!didSeek){
        selectDefaultTracks();
        mainTrack = getMainSelectedTrack();
      }
      seekPos = M.getFirstms(mainTrack) + (M.getLastms(mainTrack) - M.getFirstms(mainTrack)) * (((double)retries)/10.0);
    }
    if (!didSeek){
      onFail("Could not seek to location for image");
    }
  }

  void OutJPG::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "JPG";
    capa["desc"] = "Allows getting a representative key frame as JPG image. Requires ffmpeg (with "
                   "h264 decoding and jpeg encoding) to be "
                   "installed in the PATH.";
    capa["url_rel"] = "/$.jpg";
    capa["url_match"] = "/$.jpg";
    capa["codecs"][0u][0u].append("H264");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/image/jpeg";
    capa["methods"][0u]["hrn"] = "JPEG";
    capa["methods"][0u]["priority"] = 0;
    config->addStandardPushCapabilities(capa);
    capa["push_urls"].append("/*.jpg");

    capa["optional"]["cachedir"]["name"] = "Cache directory";
    capa["optional"]["cachedir"]["help"] =
        "Location to store cached images, preferably in RAM somewhere";
    capa["optional"]["cachedir"]["option"] = "--cachedir";
    capa["optional"]["cachedir"]["short"] = "D";
    capa["optional"]["cachedir"]["default"] = "/tmp";
    capa["optional"]["cachedir"]["type"] = "string";
    capa["optional"]["cachetime"]["name"] = "Cache time";
    capa["optional"]["cachetime"]["help"] =
        "Duration in seconds to wait before refreshing cached images. Does not apply to VoD "
        "streams (VoD is cached infinitely)";
    capa["optional"]["cachetime"]["option"] = "--cachetime";
    capa["optional"]["cachetime"]["short"] = "T";
    capa["optional"]["cachetime"]["default"] = 30;
    capa["optional"]["cachetime"]["type"] = "uint";
    capa["optional"]["ffopts"]["name"] = "Ffmpeg arguments";
    capa["optional"]["ffopts"]["help"] =
        "Extra arguments to use when generating the jpg file through ffmpeg";
    capa["optional"]["ffopts"]["option"] = "--ffopts";
    capa["optional"]["ffopts"]["short"] = "F";
    capa["optional"]["ffopts"]["default"] = "-qscale:v 4";
    capa["optional"]["ffopts"]["type"] = "string";
    cfg->addOptionsFromCapabilities(capa);

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store JPG file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  void OutJPG::onHTTP(){
    std::string method = H.method;
    H.clearHeader("Range");
    H.clearHeader("Icy-MetaData");
    H.clearHeader("User-Agent");
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "image/jpeg");
      H.protocol = "HTTP/1.1";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    initialize();
    if (!userSelect.size()){
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.body.clear();
      H.SendResponse("200", "Unprocessable: not H264", myConn);
#include "noh264.h"
      myConn.SendNow(noh264, noh264_len);
      myConn.close();
      return;
    }
    H.SetHeader("Content-Type", "image/jpeg");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    H.StartResponse(H, myConn);
    HTTP = true;
    generate();
    if (!jpg_buffer.str().size()){
      NoFFMPEG();
    }else{
      H.Chunkify(jpg_buffer.str().c_str(), jpg_buffer.str().size(), myConn);
      if (cachedir.size()){
        std::ofstream cachefile;
        cachefile.open(cachedir.c_str());
        cachefile << jpg_buffer.str();
        cachefile.close();
      }
    }
    H.Chunkify("", 0, myConn);
    H.Clean();
    HTTP = false;
  }

  void OutJPG::NoFFMPEG(){
    FAIL_MSG("Could not start ffmpeg! Is it installed on the system?");
#include "noffmpeg.h"
    if (HTTP){
      H.Chunkify(noffmpeg, noffmpeg_len, myConn);
    }else{
      myConn.SendNow(noffmpeg, noffmpeg_len);
    }
  }

  void OutJPG::generate(){
    // If we're caching, check if the cache hasn't expired yet...
    if (cachedir.size() && cachetime){
      struct stat statData;
      if (stat(cachedir.c_str(), &statData) != -1){
        if (Util::epoch() - statData.st_mtime <= cachetime || M.getVod()){
          std::ifstream cachefile;
          cachefile.open(cachedir.c_str());
          char buffer[8 * 1024];
          while (cachefile.good() && myConn){
            cachefile.read(buffer, 8 * 1024);
            uint32_t s = cachefile.gcount();
            if (HTTP){
              H.Chunkify(buffer, s, myConn);
            }else{
              myConn.SendNow(buffer, s);
            }
          }
          cachefile.close();
          return;
        }
      }
    }

    initialSeek();
    size_t mainTrack = getMainSelectedTrack();
    if (mainTrack == INVALID_TRACK_ID){
      FAIL_MSG("Could not select valid track");
      return;
    }

    int fin = -1, fout = -1, ferr = 2;
    pid_t ffmpeg = -1;
    // Start ffmpeg quietly if we're < MEDIUM debug level
    char ffcmd[256];
    ffcmd[255] = 0; // ensure there is an ending null byte
    snprintf(ffcmd, 255, "ffmpeg %s -f h264 -i - %s -vframes 1 -f mjpeg -",
             (Util::printDebugLevel >= DLVL_MEDIUM ? "" : "-v quiet"),
             config->getString("ffopts").c_str());

    HIGH_MSG("Starting JPG command: %s", ffcmd);
    char *args[128];
    uint8_t argCnt = 0;
    char *startCh = 0;
    for (char *i = ffcmd; i - ffcmd < 256; ++i){
      if (!*i){
        if (startCh){args[argCnt++] = startCh;}
        break;
      }
      if (*i == ' '){
        if (startCh){
          args[argCnt++] = startCh;
          startCh = 0;
          *i = 0;
        }
      }else{
        if (!startCh){startCh = i;}
      }
    }
    args[argCnt] = 0;

    ffmpeg = Util::Procs::StartPiped(args, &fin, &fout, &ferr);
    if (ffmpeg < 2){
      Socket::Connection failure(fin, fout);
      failure.close();
      NoFFMPEG();
      return;
    }
    VERYHIGH_MSG("Started ffmpeg, PID %" PRIu64 ", pipe %" PRIu32 "/%" PRIu32, (uint64_t)ffmpeg,
                 (uint32_t)fin, (uint32_t)fout);
    Socket::Connection ffconn(fin, -1);

    // Send H264 init data in Annex B format
    MP4::AVCC avccbox;
    avccbox.setPayload(M.getInit(mainTrack));
    ffconn.SendNow(avccbox.asAnnexB());
    INSANE_MSG("Sent init data to ffmpeg...");

    if (ffconn && prepareNext() && thisPacket){
      uint64_t keytime = thisPacket.getTime();
      do{
        char *p = 0;
        size_t l = 0;
        uint32_t o = 0;
        thisPacket.getString("data", p, l);
        // Send all NAL units in the key frame, in Annex B format
        while (o + 4 < l){
          // get NAL unit size
          uint32_t s = Bit::btohl(p + o);
          // make sure we don't go out of bounds of packet
          if (o + s + 4 > l){break;}
          // Send H264 Annex B start code
          ffconn.SendNow("\000\000\000\001", 4);
          // Send NAL unit
          ffconn.SendNow(p + o + 4, s);
          INSANE_MSG("Sent h264 %" PRIu32 "b NAL unit to ffmpeg (time: %" PRIu64 ")...", s,
                     thisPacket.getTime());
          // Skip to next NAL unit
          o += s + 4;
        }
        INSANE_MSG("Sent whole packet, checking next...");
      }while (ffconn && prepareNext() && thisPacket && thisPacket.getTime() == keytime);
    }
    ffconn.close();
    // Output ffmpeg result data to socket
    jpg_buffer.clear();
    Socket::Connection ffout(-1, fout);
    while (myConn && ffout && (ffout.spool() || ffout.Received().size())){
      while (myConn && ffout.Received().size()){
        jpg_buffer << ffout.Received().get();
        ffout.Received().get().clear();
      }
    }
    ffout.close();
  }
}// namespace Mist
