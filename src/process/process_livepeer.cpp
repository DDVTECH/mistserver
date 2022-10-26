#include <algorithm> //for std::find
#include <fstream>
#include <mist/timing.h>
#include "process_livepeer.h"
#include <mist/procs.h>
#include <mist/util.h>
#include <mist/downloader.h>
#include <mist/triggers.h>
#include <mist/encode.h>
#include "../input/input.h"
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

tthread::mutex segMutex;
tthread::mutex broadcasterMutex;

//Stat related stuff
JSON::Value pStat;
JSON::Value & pData = pStat["proc_status_update"]["status"];
tthread::mutex statsMutex;
uint64_t statSwitches = 0;
uint64_t statFailN200 = 0;
uint64_t statFailTimeout = 0;
uint64_t statFailParse = 0;
uint64_t statFailOther = 0;
uint64_t statSinkMs = 0;
uint64_t statSourceMs = 0;

std::string api_url;

Util::Config co;
Util::Config conf;

size_t insertTurn = 0;
bool isStuck = false;
size_t sourceIndex = INVALID_TRACK_ID;

namespace Mist{

  void pickRandomBroadcaster(){
    std::string prevBroad = currBroadAddr;
    currBroadAddr.clear();
    std::set<std::string> validAddrs;
    jsonForEach(lpBroad, bCast){
      if (bCast->isMember("address")){
        validAddrs.insert((*bCast)["address"].asStringRef());
      }
    }
    if (validAddrs.size() > 1){validAddrs.erase(prevBroad);}
    if (!validAddrs.size()){
      FAIL_MSG("Could not select a new random broadcaster!");
      /// TODO Finish this function.
    }
    std::set<std::string>::iterator it = validAddrs.begin();
    for (size_t r = rand() % validAddrs.size(); r; --r){++it;}
    currBroadAddr = *it;
  }

  //Source process, takes data from input stream and sends to livepeer
  class ProcessSource : public TSOutput{
  public:
    bool isRecording(){return false;}
    bool isReadyForPlay(){
      if (!TSOutput::isReadyForPlay()){return false;}
      size_t mTrk = getMainSelectedTrack();
      if (mTrk == INVALID_TRACK_ID || M.getType(mTrk) != "video"){
        HIGH_MSG("NOT READY (non-video main track)");
        return false;
      }
      return true;
    }
    ProcessSource(Socket::Connection &c) : TSOutput(c){
      capa["name"] = "Livepeer";
      capa["codecs"][0u][0u].append("+H264");
      capa["codecs"][0u][0u].append("+HEVC");
      capa["codecs"][0u][0u].append("+MPEG2");
      capa["codecs"][0u][1u].append("+AAC");
      realTime = 0;
      wantRequest = false;
      parseData = true;
      currPreSeg = 0;
    };
    virtual bool onFinish(){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        if (userSelect.size()){
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            INFO_MSG("Unmasking source track %zu" PRIu64, it->first);
            meta.validateTrack(it->first, TRACK_VALID_ALL);
          }
        }
      }
      return TSOutput::onFinish();
    }
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
        meta.validateTrack(trackId, TRACK_VALID_ALL);
      }
      TSOutput::dropTrack(trackId, reason, probablyBad);
    }
    size_t currPreSeg;
    void sendTS(const char *tsData, size_t len = 188){
      if (!presegs[currPreSeg].data.size()){
        presegs[currPreSeg].time = thisPacket.getTime();
      }
      presegs[currPreSeg].data.append(tsData, len);
    };
    virtual void initialSeek(){
      if (!meta){return;}
      if (opt.isMember("source_mask") && !opt["source_mask"].isNull() && opt["source_mask"].asString() != ""){
        uint64_t sourceMask = opt["source_mask"].asInt();
        if (userSelect.size()){
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            INFO_MSG("Masking source track %zu to %" PRIu64, it->first, sourceMask);
            meta.validateTrack(it->first, sourceMask);
          }
        }
      }
      if (!meta.getLive() || opt["leastlive"].asBool()){
        INFO_MSG("Seeking to earliest point in stream");
        seek(0);
        return;
      }
      Output::initialSeek();
    }
    void sendNext(){
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        if (pData["source_tracks"].size() != userSelect.size()){
          pData["source_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["source_tracks"].append((uint64_t)it->first);
          }
        }
      }
      if (thisTime > statSourceMs){statSourceMs = thisTime;}
      if (thisPacket.getFlag("keyframe") && M.trackLoaded(thisIdx) && M.getType(thisIdx) == "video" && (thisTime - presegs[currPreSeg].time) >= 1000){
        sourceIndex = getMainSelectedTrack();
        if (presegs[currPreSeg].data.size() > 187){
          presegs[currPreSeg].keyNo = keyCount;
          presegs[currPreSeg].width = M.getWidth(thisIdx);
          presegs[currPreSeg].height = M.getHeight(thisIdx);
          presegs[currPreSeg].segDuration = thisTime - presegs[currPreSeg].time;
          presegs[currPreSeg].fullyRead = false;
          presegs[currPreSeg].fullyWritten = true;
          currPreSeg = (currPreSeg+1) % PRESEG_COUNT;
        }
        while (!presegs[currPreSeg].fullyRead && conf.is_active){Util::sleep(100);}
        presegs[currPreSeg].data.assign(0, 0);
        needsLookAhead = 0;
        maxSkipAhead = 0;
        packCounter = 0;
        ++keyCount;
        sendFirst = true;
      }
      TSOutput::sendNext();
    }
  };

  //sink, takes data from livepeer and ingests
  class ProcessSink : public Input{
  public:
    ProcessSink(Util::Config *cfg) : Input(cfg){
      capa["name"] = "Livepeer";
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      Util::streamVariables(streamName, opt["source"].asString());
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        pStat["proc_status_update"]["sink"] = streamName;
        pStat["proc_status_update"]["source"] = opt["source"];
      }
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
      if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
        DTSC::trackValidDefault = opt["target_mask"].asInt();
      }
      preRun();
    };
    virtual bool needsLock(){return false;}
    bool isSingular(){return false;}
  private:
    std::map<std::string, readySegment>::iterator segIt;
    bool needHeader(){return false;}
    virtual void getNext(size_t idx = INVALID_TRACK_ID){
      thisPacket.null();
      int64_t timeOffset = 0;
      uint64_t trackId = 0;
      {
        tthread::lock_guard<tthread::mutex> guard(statsMutex);
        if (pData["sink_tracks"].size() != userSelect.size()){
          pData["sink_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["sink_tracks"].append((uint64_t)it->first);
          }
        }
      }
      while (!thisPacket && conf.is_active){
        {
          tthread::lock_guard<tthread::mutex> guard(segMutex);
          std::string oRend;
          uint64_t lastPacket = 0xFFFFFFFFFFFFFFFFull;
          for (segIt = segs.begin(); segIt != segs.end(); ++segIt){
            if (isStuck){
              WARN_MSG("Considering %s: T%" PRIu64 ", fullyWritten: %s, fullyRead: %s", segIt->first.c_str(), segIt->second.lastPacket, segIt->second.fullyWritten?"Y":"N", segIt->second.fullyRead?"Y":"N");
            }
            if (!segIt->second.fullyWritten){continue;}
            if (segIt->second.lastPacket > lastPacket){continue;}
            oRend = segIt->first;
            lastPacket = segIt->second.lastPacket;
          }
          if (oRend.size()){
            if (isStuck){WARN_MSG("Picked %s!", oRend.c_str());}
            readySegment & S = segs[oRend];
            while (!S.S.hasPacket() && S.byteOffset <= S.data.size() - 188){
              S.S.parse(S.data + S.byteOffset, 0);
              S.byteOffset += 188;
              if (S.byteOffset > S.data.size() - 188){S.S.finish();}
            }
            if (S.S.hasPacket()){
              S.S.getEarliestPacket(thisPacket);
              if (!S.offsetCalcd){
                S.timeOffset = S.time - thisPacket.getTime();
                HIGH_MSG("First timestamp of %s at time %" PRIu64 " is %" PRIu64 ", adjusting by %" PRId64, oRend.c_str(), S.time, thisPacket.getTime(), S.timeOffset);
                S.offsetCalcd = true;
              }
              timeOffset = S.timeOffset;
              if (thisPacket){
                S.lastPacket = thisPacket.getTime() + timeOffset;
                if (S.lastPacket >= statSinkMs){statSinkMs = S.lastPacket;}
              }
              trackId = (S.ID << 16) + thisPacket.getTrackId();
              thisIdx = M.trackIDToIndex(trackId, getpid());
              if (thisIdx == INVALID_TRACK_ID || !M.getCodec(thisIdx).size()){
                INFO_MSG("Initializing track %zi as %" PRIu64 " for playlist %zu", thisPacket.getTrackId(), trackId, S.ID);
                S.S.initializeMetadata(meta, thisPacket.getTrackId(), trackId);
                thisIdx = M.trackIDToIndex(trackId, getpid());
                meta.setSourceTrack(thisIdx, sourceIndex);
                if (M.getType(thisIdx) == "audio"){
                  meta.validateTrack(thisIdx, 0);
                }
              }
            }
            if (S.byteOffset >= S.data.size() && !S.S.hasPacket()){
              S.fullyWritten = false;
              S.fullyRead = true;
            }
          }
        }
        if (!thisPacket){
          Util::sleep(25);
          if (userSelect.size() && userSelect.begin()->second.getStatus() == COMM_STATUS_REQDISCONNECT){
            Util::logExitReason(ER_CLEAN_LIVE_BUFFER_REQ, "buffer requested shutdown");
            return;
          }
        }
      }

      if (thisPacket){
        char *data = thisPacket.getData();
        //overwrite trackID
        Bit::htobl(data + 8, trackId);
        //overwrite packettime
        thisTime = thisPacket.getTime() + timeOffset;
        Bit::htobll(data + 12, thisTime);
      }
    }
    bool checkArguments(){return true;}
    bool readHeader(){return true;}
    bool openStreamSource(){return true;}
    void parseStreamHeader(){}
    virtual bool publishesTracks(){return false;}
  };



}// namespace Mist



void sinkThread(void *){
  Mist::ProcessSink in(&co);
  co.activate();
  co.is_active = true;
  INFO_MSG("Running sink thread...");
  in.run();
  INFO_MSG("Sink thread shutting down");
  conf.is_active = false;
  co.is_active = false;
}

void sourceThread(void *){
  conf.addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream "
                                                  "that this connector will transmit.\"}"));
  JSON::Value opt;
  opt["arg"] = "string";
  opt["default"] = "";
  opt["arg_num"] = 1;
  opt["help"] = "Target filename to store EBML file as, or - for stdout.";
  //Check for audio selection, default to none
  std::string audio_select = "none";
  if (Mist::opt.isMember("audio_select") && Mist::opt["audio_select"].isString() && Mist::opt["audio_select"]){
    audio_select = Mist::opt["audio_select"].asStringRef();
  }
  //Check for source track selection, default to maxbps
  std::string video_select = "maxbps";
  if (Mist::opt.isMember("source_track") && Mist::opt["source_track"].isString() && Mist::opt["source_track"]){
    video_select = Mist::opt["source_track"].asStringRef();
  }
  conf.addOption("target", opt);
  conf.getOption("streamname", true).append(Mist::opt["source"].c_str());
  conf.getOption("target", true).append("-?audio="+audio_select+"&video="+video_select);
  Mist::ProcessSource::init(&conf);
  conf.is_active = true;
  int devnull = open("/dev/null", O_RDWR);
  Socket::Connection c(devnull, devnull);
  Mist::ProcessSource out(c);
  if (conf.is_active){
    INFO_MSG("Running source thread...");
    out.run();
    INFO_MSG("Stopping source thread...");
  }else{
    INFO_MSG("Aborting source thread...");
  }
  conf.is_active = false;
  co.is_active = false;
  close(devnull);
}

///Inserts a part into the queue of parts to parse
void insertPart(const Mist::preparedSegment & mySeg, const std::string & rendition, void * ptr, size_t len){
  uint64_t waitTime = Util::bootMS();
  uint64_t lastAlert = waitTime;
  while (conf.is_active){
    {
      tthread::lock_guard<tthread::mutex> guard(segMutex);
      if (Mist::segs[rendition].fullyRead){
        HIGH_MSG("Inserting %zi bytes of %s, originally for time %" PRIu64, len, rendition.c_str(), mySeg.time);
        Mist::segs[rendition].set(mySeg.time, ptr, len);
        return;
      }
    }
    uint64_t currMs = Util::bootMS();
    isStuck = false;
    if (currMs-waitTime > 5000 && currMs-lastAlert > 1000){
      lastAlert = currMs;
      INFO_MSG("Waiting for %s to finish parsing current part (%" PRIu64 "ms)...", rendition.c_str(), currMs-waitTime);
      isStuck = true;
    }
    Util::sleep(100);
  }
}

///Parses a multipart response
void parseMultipart(const Mist::preparedSegment & mySeg, const std::string & cType, const std::string & d){
  std::string bound;
  if (cType.find("boundary=") != std::string::npos){
    bound = "--"+cType.substr(cType.find("boundary=")+9);
  }
  if (!bound.size()){
    FAIL_MSG("Could not parse boundary string from Content-Type header!");
    return;
  }
  size_t startPos = 0;
  size_t nextPos = d.find(bound, startPos);
  //While there is at least one boundary to be found
  while (nextPos != std::string::npos){
    startPos = nextPos+bound.size()+2;
    nextPos = d.find(bound, startPos);
    if (nextPos != std::string::npos){
      //We have a start and end position, looking good so far...
      size_t headEnd = d.find("\r\n\r\n", startPos);
      if (headEnd == std::string::npos || headEnd > nextPos){
        FAIL_MSG("Could not find end of headers for multi-part part; skipping to next part");
        continue;
      }
      //Alright, we know where our headers and data are. Parse the headers
      std::map<std::string, std::string> partHeaders;
      size_t headPtr = startPos;
      size_t nextNL = d.find("\r\n", headPtr);
      while (nextNL != std::string::npos && nextNL <= headEnd){
        size_t col = d.find(":", headPtr);
        if (col != std::string::npos && col < nextNL){
          partHeaders[d.substr(headPtr, col-headPtr)] = d.substr(col+2, nextNL-col-2);
        }
        headPtr = nextNL+2;
        nextNL = d.find("\r\n", headPtr);
      }
      for (std::map<std::string, std::string>::iterator it = partHeaders.begin(); it != partHeaders.end(); ++it){
        VERYHIGH_MSG("Header %s = %s", it->first.c_str(), it->second.c_str());
      }
      VERYHIGH_MSG("Body has length %zi", nextPos-headEnd-6);
      std::string preType = partHeaders["Content-Type"].substr(0, 10);
      Util::stringToLower(preType);
      if (preType == "video/mp2t"){
        insertPart(mySeg, partHeaders["Rendition-Name"], (void*)(d.data()+headEnd+4), nextPos-headEnd-6);
      }
    }
  }
}

void segmentRejectedTrigger(Mist::preparedSegment & mySeg, const std::string & bc1, const std::string & bc2){
  if (Triggers::shouldTrigger("LIVEPEER_SEGMENT_REJECTED", Util::streamName)){
    FAIL_MSG("Segment could not be transcoded, skipping to next and submitting for analysis");
    JSON::Value trackInfo;
    trackInfo["width"] = mySeg.width;
    trackInfo["height"] = mySeg.height;
    trackInfo["duration"] = mySeg.segDuration;
    std::string payload = Mist::opt.toString()+"\n"+Encodings::Base64::encode(std::string(mySeg.data, mySeg.data.size()))+"\n"+trackInfo.toString()+"\n"+bc1+"\n"+bc2;
    Triggers::doTrigger("LIVEPEER_SEGMENT_REJECTED", payload, Util::streamName);
  }else{
    FAIL_MSG("Segment could not be transcoded, skipping to next");
  }
  mySeg.fullyWritten = false;
  mySeg.fullyRead = true;
  insertTurn = (insertTurn + 1) % PRESEG_COUNT;
}

void uploadThread(void * num){
  size_t myNum = (size_t)num;
  Mist::preparedSegment & mySeg = Mist::presegs[myNum];
  HTTP::Downloader upper;
  bool was422 = false;
  std::string prevURL;
  while (conf.is_active){
    while (conf.is_active && !mySeg.fullyWritten){Util::sleep(100);}
    if (!conf.is_active){return;}//Exit early on shutdown
    size_t attempts = 0;
    do{
      HTTP::URL target;
      {
        tthread::lock_guard<tthread::mutex> guard(broadcasterMutex);
        target = HTTP::URL(Mist::currBroadAddr+"/live/"+Mist::lpID+"/"+JSON::Value(mySeg.keyNo).asString()+".ts");
      }
      upper.dataTimeout = mySeg.segDuration/1000 + 2;
      upper.retryCount = 2;
      upper.setHeader("Accept", "multipart/mixed");
      upper.setHeader("Content-Duration", JSON::Value(mySeg.segDuration).asString());
      upper.setHeader("Content-Resolution", JSON::Value(mySeg.width).asString()+"x"+JSON::Value(mySeg.height).asString());

      // If the Livepeer API Key hasn't been set then we send the configuration as an HTTP header rather than pushing to the API
      if (!Mist::opt.isMember("access_token") || !Mist::opt["access_token"] || !Mist::opt["access_token"].isString()){
        JSON::Value tc;
        tc["profiles"] = Mist::opt["target_profiles"];
        upper.setHeader("Livepeer-Transcode-Configuration", tc.toString());
      }

      uint64_t uplTime = Util::getMicros();
      if (upper.post(target, mySeg.data, mySeg.data.size())){
        uplTime = Util::getMicros(uplTime);
        if (upper.getStatusCode() == 200){
          MEDIUM_MSG("Uploaded %zu bytes (time %" PRIu64 "-%" PRIu64 " = %" PRIu64 " ms) to %s in %.2f ms", mySeg.data.size(), mySeg.time, mySeg.time+mySeg.segDuration, mySeg.segDuration, target.getUrl().c_str(), uplTime/1000.0);
          was422 = false;
          prevURL.clear();
          mySeg.fullyWritten = false;
          mySeg.fullyRead = true;
          //Wait your turn
          while (myNum != insertTurn && conf.is_active){Util::sleep(100);}
          if (!conf.is_active){return;}//Exit early on shutdown
          if (upper.getHeader("Content-Type").substr(0, 10) == "multipart/"){
            parseMultipart(mySeg, upper.getHeader("Content-Type"), upper.const_data());
          }else{
            ++statFailParse;
            FAIL_MSG("Non-multipart response received - this version only works with multipart!");
          }
          insertTurn = (insertTurn + 1) % PRESEG_COUNT;
          break;//Success: no need to retry
        }else if (upper.getStatusCode() == 422){
          //segment rejected by broadcaster node; try a different broadcaster at most once and keep track
          ++statFailN200;
          WARN_MSG("Rejected upload of %zu bytes to %s after %.2f ms: %" PRIu32 " %s", mySeg.data.size(), target.getUrl().c_str(), uplTime/1000.0, upper.getStatusCode(), upper.getStatusText().c_str());
          if (was422){
            //second error in a row, fire off LIVEPEER_SEGMENT_REJECTED trigger
            segmentRejectedTrigger(mySeg, prevURL, target.getUrl());
            was422 = false;
            prevURL.clear();
            break;
          }else{
            prevURL = target.getUrl();
            was422 = true;
          }
        }else{
          //Failure due to non-200/422 status code
          ++statFailN200;
          WARN_MSG("Failed to upload %zu bytes to %s in %.2f ms: %" PRIu32 " %s", mySeg.data.size(), target.getUrl().c_str(), uplTime/1000.0, upper.getStatusCode(), upper.getStatusText().c_str());
        }
      }else{
        //other failures and aborted uploads
        if (!conf.is_active){return;}//Exit early on shutdown
        uplTime = Util::getMicros(uplTime);
        ++statFailTimeout;
        WARN_MSG("Failed to upload %zu bytes to %s in %.2f ms", mySeg.data.size(), target.getUrl().c_str(), uplTime/1000.0);
      }
      //Error handling
      attempts++;
      Util::sleep(100);//Rate-limit retries
      if (attempts > 4){
        Util::logExitReason(ER_FORMAT_SPECIFIC, "too many upload failures");
        conf.is_active = false;
        return;
      }
      bool switchSuccess = false;
      {
        tthread::lock_guard<tthread::mutex> guard(broadcasterMutex);
        std::string prevBroadAddr = Mist::currBroadAddr;
        Mist::pickRandomBroadcaster();
        if (!Mist::currBroadAddr.size()){
          FAIL_MSG("Cannot switch to new broadcaster: none available");
          Util::logExitReason(ER_FORMAT_SPECIFIC, "no Livepeer broadcasters available");
          conf.is_active = false;
          return;
        }
        if (Mist::currBroadAddr != prevBroadAddr){
          ++statSwitches;
          switchSuccess = true;
          WARN_MSG("Switched to new broadcaster: %s", Mist::currBroadAddr.c_str());
        }else{
          WARN_MSG("Cannot switch broadcaster; only a single option is available");
        }
      }
      if (!switchSuccess && was422){
        //no switch possible, fire off LIVEPEER_SEGMENT_REJECTED trigger
        segmentRejectedTrigger(mySeg, prevURL, "N/A");
        was422 = false;
        prevURL.clear();
        break;
      }
    }while(conf.is_active);
  }
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  JSON::Value capa;

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
    opt["value"].append(false);
    config.addOption("json", opt);
    opt.null();
    opt["long"] = "kickoff";
    opt["short"] = "K";
    opt["help"] = "Kick off source if not already active";
    opt["value"].append(false);
    config.addOption("kickoff", opt);
  }

  capa["codecs"][0u][0u].append("H264");

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "Livepeer";
    capa["desc"] = "Use livepeer to transcode video.";

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
    capa["optional"]["target_mask"]["default"] = "";

    capa["optional"]["exit_unmask"]["name"] = "Undo masks on process exit/fail";
    capa["optional"]["exit_unmask"]["help"] = "If/when the process exits or fails, the masks for input tracks will be reset to defaults. (NOT to previous value, but to defaults!)";
    capa["optional"]["exit_unmask"]["default"] = false;

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] = "What stream the encoded track should be added to. Defaults "
                                       "to source stream. May contain variables.";
    capa["optional"]["sink"]["type"] = "string";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    capa["optional"]["source_track"]["name"] = "Input selection";
    capa["optional"]["source_track"]["help"] =
        "Track selector(s) of the video portion of the source stream. Defaults to highest bit rate video track.";
    capa["optional"]["source_track"]["type"] = "track_selector_parameter";
    capa["optional"]["source_track"]["n"] = 1;
    capa["optional"]["source_track"]["default"] = "maxbps";

    capa["optional"]["audio_select"]["name"] = "Audio streams";
    capa["optional"]["audio_select"]["help"] =
        "Track selector(s) for the audio portion of the source stream. Defaults to 'none' so no audio is passed at all.";
    capa["optional"]["audio_select"]["type"] = "track_selector_parameter";
    capa["optional"]["audio_select"]["default"] = "none";

    capa["optional"]["access_token"]["name"] = "Access token";
    capa["optional"]["access_token"]["help"] = "Your livepeer access token";
    capa["optional"]["access_token"]["type"] = "string";

    capa["optional"]["hardcoded_broadcasters"]["name"] = "Hardcoded Broadcasters";
    capa["optional"]["hardcoded_broadcasters"]["help"] = "Use hardcoded broadcasters, rather than using Livepeer's gateway.";
    capa["optional"]["hardcoded_broadcasters"]["type"] = "string";

    capa["optional"]["leastlive"]["name"] = "Start in the past";
    capa["optional"]["leastlive"]["help"] = "Start the transcode as far back in the past as possible, instead of at the most-live point of the stream.";
    capa["optional"]["leastlive"]["type"] = "boolean";
    capa["optional"]["leastlive"]["default"] = false;


    capa["optional"]["custom_url"]["name"] = "Custom API URL";
    capa["optional"]["custom_url"]["help"] = "Alternative API URL path";
    capa["optional"]["custom_url"]["type"] = "string";
    capa["optional"]["custom_url"]["default"] = "https://livepeer.live/api";


    capa["required"]["target_profiles"]["name"] = "Profiles";
    capa["required"]["target_profiles"]["type"] = "sublist";
    capa["required"]["target_profiles"]["itemLabel"] = "profile";
    capa["required"]["target_profiles"]["help"] = "Tracks to transcode the source into";
    capa["required"]["target_profiles"]["sort"] = "n";
    {
      JSON::Value &grp = capa["required"]["target_profiles"]["required"];
      grp["name"]["name"] = "Name";
      grp["name"]["help"] = "Name for the profile. Must be unique within this transcode.";
      grp["name"]["type"] = "str";
      grp["name"]["n"] = 0;
      grp["bitrate"]["name"] = "Bitrate";
      grp["bitrate"]["help"] = "Target bit rate of the output";
      grp["bitrate"]["unit"] = "bits per second";
      grp["bitrate"]["type"] = "int";
      grp["bitrate"]["n"] = 1;
    }{
      JSON::Value &grp = capa["required"]["target_profiles"]["optional"];
      grp["width"]["name"] = "Width";
      grp["width"]["help"] = "Width in pixels of the output. Defaults to match aspect with height, or source width if both are default.";
      grp["width"]["unit"] = "px";
      grp["width"]["type"] = "int";
      grp["width"]["n"] = 2;
      grp["height"]["name"] = "Height";
      grp["height"]["help"] = "Height in pixels of the output. Defaults to match aspect with width, or source height if both are default. If only height is given and the source height is greater than the source width, width and height will swap and do what you most likely wanted to do (e.g. follow your config in portrait mode instead of landscape mode).";
      grp["height"]["unit"] = "px";
      grp["height"]["type"] = "int";
      grp["height"]["n"] = 3;
      grp["fps"]["name"] = "Framerate";
      grp["fps"]["help"] = "Framerate of the output. Zero means to match the input (= the default).";
      grp["fps"]["unit"] = "frames per second";
      grp["fps"]["default"] = 0;
      grp["fps"]["type"] = "int";
      grp["fps"]["n"] = 4;
      grp["gop"]["name"] = "Keyframe interval / GOP size";
      grp["gop"]["help"] = "Interval of keyframes / duration of GOPs for the transcode. \"0.0\" means to match input (= the default), 'intra' means to send only key frames. Otherwise, fractional seconds between keyframes.";
      grp["gop"]["unit"] = "seconds";
      grp["gop"]["default"] = "0.0";
      grp["gop"]["type"] = "str";
      grp["gop"]["n"] = 5;

      grp["profile"]["name"] = "H264 Profile";
      grp["profile"]["help"] = "Profile to use. Defaults to \"High\".";
      grp["profile"]["type"] = "select";
      grp["profile"]["select"][0u][0u] = "H264High";
      grp["profile"]["select"][0u][1u] = "High";
      grp["profile"]["select"][1u][0u] = "H264Baseline";
      grp["profile"]["select"][1u][1u] = "Baseline";
      grp["profile"]["select"][2u][0u] = "H264Main";
      grp["profile"]["select"][2u][1u] = "Main";
      grp["profile"]["select"][3u][0u] = "H264ConstrainedHigh";
      grp["profile"]["select"][3u][1u] = "High, without b-frames";
      grp["profile"]["default"] = "H264High";

      grp["track_inhibit"]["name"] = "Track inhibitor(s)";
      grp["track_inhibit"]["help"] =
          "What tracks to use as inhibitors. If this track selector is able to select a track, the profile is not used. Only verified on initial boot of the process and then never again. Defaults to none.";
      grp["track_inhibit"]["type"] = "string";
      grp["track_inhibit"]["validate"][0u] = "track_selector";
      grp["track_inhibit"]["default"] = "audio=none&video=none&subtitle=none";
    }

    capa["optional"]["track_inhibit"]["name"] = "Track inhibitor(s)";
    capa["optional"]["track_inhibit"]["help"] =
        "What tracks to use as inhibitors. If this track selector is able to select a track, the "
        "process does not start. Defaults to none.";
    capa["optional"]["track_inhibit"]["type"] = "string";
    capa["optional"]["track_inhibit"]["validate"][0u] = "track_selector";
    capa["optional"]["track_inhibit"]["default"] = "audio=none&video=none&subtitle=none";

    capa["optional"]["debug"]["name"] = "Debug level";
    capa["optional"]["debug"]["help"] = "The debug level at which messages need to be printed.";
    capa["optional"]["debug"]["type"] = "debug";

    capa["ainfo"]["lp_id"]["name"] = "Livepeer transcode ID";
    capa["ainfo"]["switches"]["name"] = "Broadcaster switches since start";
    capa["ainfo"]["fail_non200"]["name"] = "Failures due to non-200 response codes";
    capa["ainfo"]["fail_timeout"]["name"] = "Failures due to timeout";
    capa["ainfo"]["fail_parse"]["name"] = "Failures due to parse errors in TS response data";
    capa["ainfo"]["fail_other"]["name"] = "Failures due to other reasons";
    capa["ainfo"]["bc"]["name"] = "Currently used broadcaster";
    capa["ainfo"]["sinkTime"]["name"] = "Sink timestamp";
    capa["ainfo"]["sourceTime"]["name"] = "Source timestamp";
    capa["ainfo"]["percent_done"]["name"] = "Percentage for VoD transcodes";


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

  // check config for generic options
  srand(getpid());
  // Check generic configuration variables
  if (!Mist::opt.isMember("source") || !Mist::opt["source"] || !Mist::opt["source"].isString()){
    FAIL_MSG("Missing or blank source in config!");
    return 1;
  }

  if (!Mist::opt.isMember("sink") || !Mist::opt["sink"] || !Mist::opt["sink"].isString()){
    INFO_MSG("No sink explicitly set, using source as sink");
  }
  if (!Mist::opt.isMember("custom_url") || !Mist::opt["custom_url"] || !Mist::opt["custom_url"].isString()){
    api_url = "https://livepeer.live/api";
  }else{
    api_url = Mist::opt["custom_url"].asStringRef();
  }


  {
    //Ensure stream name is set in all threads
    std::string streamName = Mist::opt["sink"].asString();
    if (!streamName.size()){streamName = Mist::opt["source"].asString();}
    Util::streamVariables(streamName, Mist::opt["source"].asString());
    Util::setStreamName(Mist::opt["source"].asString() + "→" + streamName);
  }


  const std::string & srcStrm = Mist::opt["source"].asStringRef();
  if (config.getBool("kickoff")){
    if (!Util::startInput(srcStrm, "")){
      FAIL_MSG("Could not connector and/or start source stream!");
      return 1;
    }
    uint8_t streamStat = Util::getStreamStatus(srcStrm);
    size_t sleeps = 0;
    while (++sleeps < 2400 && streamStat != STRMSTAT_OFF && streamStat != STRMSTAT_READY){
      if (sleeps >= 16 && (sleeps % 4) == 0){
        INFO_MSG("Waiting for stream to boot... (" PRETTY_PRINT_TIME " / " PRETTY_PRINT_TIME ")", PRETTY_ARG_TIME(sleeps/4), PRETTY_ARG_TIME(2400/4));
      }
      Util::sleep(250);
      streamStat = Util::getStreamStatus(srcStrm);
    }
    if (streamStat != STRMSTAT_READY){
      FAIL_MSG("Stream not available!");
      return 1;
    }
  }

  //connect to source metadata
  DTSC::Meta M(srcStrm, false);

  //find source video track
  std::map<std::string, std::string> targetParams;
  targetParams["video"] = "maxbps";
  JSON::Value sourceCapa;
  sourceCapa["name"] = "Livepeer";
  sourceCapa["codecs"][0u][0u].append("+H264");
  sourceCapa["codecs"][0u][0u].append("+HEVC");
  sourceCapa["codecs"][0u][0u].append("+MPEG2");
  if (Mist::opt.isMember("source_track") && Mist::opt["source_track"].isString() && Mist::opt["source_track"]){
    targetParams["video"] = Mist::opt["source_track"].asStringRef();
  }
  size_t sourceIdx = INVALID_TRACK_ID;
  size_t sleeps = 0;
  while (++sleeps < 60 && (sourceIdx == INVALID_TRACK_ID || !M.getWidth(sourceIdx) || !M.getHeight(sourceIdx))){
    M.reloadReplacedPagesIfNeeded();
    std::set<size_t> vidTrack = Util::wouldSelect(M, targetParams, sourceCapa);
    sourceIdx = vidTrack.size() ? (*(vidTrack.begin())) : INVALID_TRACK_ID;
    if (sourceIdx == INVALID_TRACK_ID || !M.getWidth(sourceIdx) || !M.getHeight(sourceIdx)){
      Util::sleep(250);
    }
  }
  if (sourceIdx == INVALID_TRACK_ID || !M.getWidth(sourceIdx) || !M.getHeight(sourceIdx)){
    FAIL_MSG("No valid source track!");
    return 1;
  }

  //build transcode request
  JSON::Value pl;
  pl["name"] = Mist::opt["source"];
  pl["profiles"] = Mist::opt["target_profiles"];
  jsonForEach(pl["profiles"], prof){
    if (!prof->isMember("gop")){(*prof)["gop"] = "0.0";}
    //no or automatic framerate? default to source rate, if set, or 25 otherwise
    if (!prof->isMember("fps") || (*prof)["fps"].asDouble() == 0.0){
      (*prof)["fps"] = M.getFpks(sourceIdx);
      if (!(*prof)["fps"].asInt()){(*prof)["fps"] = 25000;}
      (*prof)["fpsDen"] = 1000;
    }
    if (!prof->isMember("profile")){(*prof)["profile"] = "H264High";}
    if ((!prof->isMember("height") || !(*prof)["height"].asInt()) && (!prof->isMember("width") || !(*prof)["width"].asInt())){
      //no width and no height
      (*prof)["width"] = M.getWidth(sourceIdx);
      (*prof)["height"] = M.getHeight(sourceIdx);
    }
    if (!prof->isMember("width") || !(*prof)["width"].asInt()){
      //no width, but we have height
      //first, check if our source is in portrait mode, if so, we assume they meant width instead of height
      if (M.getWidth(sourceIdx) < M.getHeight(sourceIdx)){
        //portrait mode
        uint32_t heightSetting = (*prof)["height"].asInt();
        (*prof)["width"] = heightSetting;
        (*prof)["height"] = M.getHeight(sourceIdx) * heightSetting / M.getWidth(sourceIdx);
      }else{
        //landscape mode
        uint32_t heightSetting = (*prof)["height"].asInt();
        (*prof)["width"] = M.getWidth(sourceIdx) * heightSetting / M.getHeight(sourceIdx);
      }
    }
    if (!prof->isMember("height") || !(*prof)["height"].asInt()){
      //no height, but we have width
      //No portrait/landscape check, as per documentation
      uint32_t widthSetting = (*prof)["width"].asInt();
      (*prof)["height"] = M.getHeight(sourceIdx) * widthSetting / M.getWidth(sourceIdx);
    }
    //force width/height to multiples of 16
    (*prof)["width"] = ((*prof)["width"].asInt() / 16) * 16;
    (*prof)["height"] = ((*prof)["height"].asInt() / 16) * 16;
    
    if (prof->isMember("track_inhibit")){
      std::set<size_t> wouldSelect = Util::wouldSelect(
          M, std::string("audio=none&video=none&subtitle=none&") + (*prof)["track_inhibit"].asStringRef());
      if (wouldSelect.size()){
        if (prof->isMember("name")){
          INFO_MSG("Removing profile because track inhibitor matches: %s", (*prof)["name"].asStringRef().c_str());
        }else{
          INFO_MSG("Removing profile because track inhibitor matches: %s", prof->toString().c_str());
        }
        prof.remove();
        continue;
      }else{
        prof->removeMember("track_inhibit");
      }
    }
    INFO_MSG("Profile parsed: %s", prof->toString().c_str());
  }
 
  //Connect to livepeer API
  HTTP::Downloader dl;
  if (!Mist::opt.isMember("access_token") || !Mist::opt["access_token"] || !Mist::opt["access_token"].isString()){
    dl.setHeader("Authorization", "Bearer "+Mist::opt["access_token"].asStringRef());
  }

  if (Mist::opt.isMember("hardcoded_broadcasters") && Mist::opt["hardcoded_broadcasters"] && Mist::opt["hardcoded_broadcasters"].isString()){
    Mist::lpBroad = JSON::fromString(Mist::opt["hardcoded_broadcasters"].asStringRef());
  } else {
    //Get broadcaster list, pick first valid address
    if (!dl.get(HTTP::URL(api_url+"/broadcaster"))){
      FAIL_MSG("Livepeer API responded negatively to request for broadcaster list");
      return 1;
    }
    Mist::lpBroad = JSON::fromString(dl.data());
  }
  if (!Mist::lpBroad || !Mist::lpBroad.isArray()){
    FAIL_MSG("No Livepeer broadcasters available");
    return 1;
  }
  Mist::pickRandomBroadcaster();
  if (!Mist::currBroadAddr.size()){
  FAIL_MSG("No Livepeer broadcasters available");
    return 1;
  }
  INFO_MSG("Using broadcaster: %s", Mist::currBroadAddr.c_str());
  if (Mist::opt.isMember("access_token") && Mist::opt["access_token"] && Mist::opt["access_token"].isString()){
    //send transcode request
    dl.setHeader("Content-Type", "application/json");
    dl.setHeader("Authorization", "Bearer "+Mist::opt["access_token"].asStringRef());
    if (!dl.post(HTTP::URL(api_url+"/stream"), pl.toString())){
      FAIL_MSG("Livepeer API responded negatively to encode request");
      return 1;
    }
    Mist::lpEnc = JSON::fromString(dl.data());
    if (!Mist::lpEnc){
      FAIL_MSG("Livepeer API did not respond with JSON");
      return 1;
    }
    if (!Mist::lpEnc.isMember("id")){
      FAIL_MSG("Livepeer API did not respond with a valid ID: %s", dl.data().data());
      return 1;
    }
    Mist::lpID = Mist::lpEnc["id"].asStringRef();
  } else {
    // We don't want to use the same manifest ids for multiple proceses on the same stream
    // name, so we append a random string to the upload URL.
    Mist::lpID = Mist::opt["source"].asStringRef() + "-" + Util::generateRandomString(8);
  }

  INFO_MSG("Livepeer transcode ID: %s", Mist::lpID.c_str());
  uint64_t lastProcUpdate = Util::bootSecs();
  pStat["proc_status_update"]["id"] = getpid();
  pStat["proc_status_update"]["proc"] = "Livepeer";
  pData["ainfo"]["lp_id"] = Mist::lpID;
  uint64_t startTime = Util::bootSecs();

  //Here be threads.

  //Source thread, from Mist to LP.
  tthread::thread source(sourceThread, 0);
  while (!conf.is_active && Util::bootSecs() < lastProcUpdate + 5){Util::sleep(50);}
  if (!conf.is_active){WARN_MSG("Timeout waiting for source thread to boot!");}
  lastProcUpdate = Util::bootSecs();

  //Sink thread, from LP to Mist
  tthread::thread sink(sinkThread, 0);
  while (!co.is_active && Util::bootSecs() < lastProcUpdate + 5){Util::sleep(50);}
  if (!co.is_active){WARN_MSG("Timeout waiting for sink thread to boot!");}
  lastProcUpdate = Util::bootSecs();

  // These threads upload prepared segments
  tthread::thread uploader0(uploadThread, (void*)0);
  tthread::thread uploader1(uploadThread, (void*)1);

  while (conf.is_active && co.is_active){
    Util::sleep(200);
    if (lastProcUpdate + 5 <= Util::bootSecs()){
      tthread::lock_guard<tthread::mutex> guard(statsMutex);
      pData["active_seconds"] = (Util::bootSecs() - startTime);
      pData["ainfo"]["switches"] = statSwitches;
      pData["ainfo"]["fail_non200"] = statFailN200;
      pData["ainfo"]["fail_timeout"] = statFailTimeout;
      pData["ainfo"]["fail_parse"] = statFailParse;
      pData["ainfo"]["fail_other"] = statFailOther;
      pData["ainfo"]["sourceTime"] = statSourceMs;
      pData["ainfo"]["sinkTime"] = statSinkMs;
      M.reloadReplacedPagesIfNeeded();
      if (M.getVod()){
        uint64_t start = M.getFirstms(sourceIdx);
        uint64_t end = M.getLastms(sourceIdx);
        pData["ainfo"]["percent_done"] = 100 * (statSinkMs - start) / (end - start);
      }
      {
        tthread::lock_guard<tthread::mutex> guard(broadcasterMutex);
        pData["ainfo"]["bc"] = Mist::currBroadAddr;
      }
      Socket::UDPConnection uSock;
      uSock.SetDestination(UDP_API_HOST, UDP_API_PORT);
      uSock.SendNow(pStat.toString());
      lastProcUpdate = Util::bootSecs();
    }
  }
  INFO_MSG("Clean shutdown; joining threads");

  co.is_active = false;
  conf.is_active = false;

  sink.join();
  source.join();
  uploader0.join();
  uploader1.join();

  INFO_MSG("Shutdown reason: %s", Util::exitReason);
  return 0;
}

