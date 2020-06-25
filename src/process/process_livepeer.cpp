#include <algorithm> //for std::find
#include <fstream>
#include "process_livepeer.h"
#include <mist/procs.h>
#include <mist/util.h>
#include <mist/downloader.h>
#include "../input/input.h"
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

tthread::mutex segMutex;

Util::Config co;
Util::Config conf;

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
    HTTP::Downloader upper;
    uint64_t segTime;
    bool isRecording(){return false;}
    ProcessSource(Socket::Connection &c) : TSOutput(c){
      capa["name"] = "Livepeer";
      capa["codecs"][0u][0u].append("+H264");
      capa["codecs"][0u][0u].append("+HEVC");
      capa["codecs"][0u][0u].append("+MPEG2");
      realTime = 0;
      wantRequest = false;
      parseData = true;
      upper.setHeader("Authorization", "Bearer "+opt["access_token"].asStringRef());
    };
    Util::ResizeablePointer tsPck;
    void sendTS(const char *tsData, size_t len = 188){
      tsPck.append(tsData, len);
    };
    virtual void initialSeek(){
      if (!meta){return;}
      if (opt["masksource"].asBool()){
        size_t mainTrack = getMainSelectedTrack();
        INFO_MSG("Masking source track %zu", mainTrack);
        meta.validateTrack(mainTrack, meta.trackValid(mainTrack) & ~(TRACK_VALID_EXT_HUMAN | TRACK_VALID_EXT_PUSH));
      }
      if (!meta.getLive() || opt["leastlive"].asBool()){
        INFO_MSG("Seeking to earliest point in stream");
        seek(0);
        return;
      }
      Output::initialSeek();
    }
    ///Inserts a part into the queue of parts to parse
    void insertPart(const std::string & rendition, void * ptr, size_t len){
      while (conf.is_active){
        {
          tthread::lock_guard<tthread::mutex> guard(segMutex);
          if (segs[rendition].fullyRead){
            HIGH_MSG("Inserting %zi bytes of %s", len, rendition.c_str());
            segs[rendition].set(segTime, ptr, len);
            return;
          }
        }
        INFO_MSG("Waiting for %s to finish parsing current part...", rendition.c_str());
        Util::sleep(500);
      }
    }
    ///Parses a multipart response
    void parseMultipart(){
      std::string cType = upper.getHeader("Content-Type");
      std::string bound;
      if (cType.find("boundary=") != std::string::npos){
        bound = "--"+cType.substr(cType.find("boundary=")+9);
      }
      if (!bound.size()){
        FAIL_MSG("Could not parse boundary string from Content-Type header!");
        return;
      }
      const std::string & d = upper.const_data();
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
            insertPart(partHeaders["Rendition-Name"], (void*)(d.data()+headEnd+4), nextPos-headEnd-6);
          }
        }
      }
    }
    void sendNext(){
      if (thisPacket.getFlag("keyframe") && (thisPacket.getTime() - segTime) >= 1000){
        if (Mist::queueClear){
          //Request to clear the queue! Do so, and wait for a new broadcaster to be picked.
          {
            tthread::lock_guard<tthread::mutex> guard(segMutex);
            segs.clear();
          }
          doingSetup = false;
          //Sleep while we're still being asked to clear
          while (queueClear && conf.is_active){
            Util::sleep(100);
          }
          if (!conf.is_active){return;}
        }
        if (tsPck.size() > 187){
          size_t attempts = 0;
          bool retry = false;
          do{
            retry = false;
            HTTP::URL target(currBroadAddr+"/live/"+lpID+"/"+JSON::Value(keyCount).asString()+".ts");
            upper.setHeader("Accept", "multipart/mixed");
            uint64_t segDuration = thisPacket.getTime() - segTime;
            upper.setHeader("Content-Duration", JSON::Value(segDuration).asString());
            if (upper.post(target, tsPck, tsPck.size())){
              if (upper.getStatusCode() == 200){
                HIGH_MSG("Uploaded %zu bytes to %s", tsPck.size(), target.getUrl().c_str());
                if (upper.getHeader("Content-Type").substr(0, 10) == "multipart/"){
                  parseMultipart();
                }else{
                  FAIL_MSG("Non-multipart response received - this version only works with multipart!");
                }
              }else{
                attempts++;
                WARN_MSG("Failed to upload %zu bytes to %s: %" PRIu32 " %s", tsPck.size(), target.getUrl().c_str(), upper.getStatusCode(), upper.getStatusText().c_str());
                if ((attempts % 3) == 3){
                  Util::sleep(250);
                  retry = true;
                }else{
                  if (attempts > 12){
                    Util::logExitReason("too many upload failures");
                    conf.is_active = false;
                    return;
                  }
                  if (!conf.is_active){return;}
                  FAIL_MSG("Failed to upload segment %s several times, picking new broadcaster", target.getUrl().c_str());
                  pickRandomBroadcaster();
                  if (!currBroadAddr.size()){
                    Util::logExitReason("no Livepeer broadcasters available");
                    conf.is_active = false;
                    return;
                  }else{
                    WARN_MSG("Switched to broadcaster: %s", currBroadAddr.c_str());
                    retry = true;
                  }
                }
              }
            }else{
              if (!conf.is_active){return;}
              FAIL_MSG("Failed to upload segment %s, picking new broadcaster", target.getUrl().c_str());
              pickRandomBroadcaster();
              if (!currBroadAddr.size()){
                Util::logExitReason("no Livepeer broadcasters available");
                conf.is_active = false;
                return;
              }else{
                WARN_MSG("Switched to broadcaster: %s", currBroadAddr.c_str());
                retry = true;
              }
            }
          }while(retry);
        }
        tsPck.assign(0, 0);
        extraKeepAway = 0;
        needsLookAhead = 0;
        maxSkipAhead = 0;
        packCounter = 0;
        segTime = thisPacket.getTime();
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
      Util::streamName = opt["source"].asString() + "→" + streamName;
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
      while (!thisPacket && conf.is_active){
        {
          tthread::lock_guard<tthread::mutex> guard(segMutex);
          std::string oRend;
          uint64_t lastPacket = segs.begin()->second.lastPacket;
          for (segIt = segs.begin(); segIt != segs.end(); ++segIt){
            if (segIt->second.lastPacket > lastPacket){continue;}
            if (!segIt->second.fullyWritten){continue;}
            if (segIt->second.byteOffset >= segIt->second.data.size()){continue;}
            oRend = segIt->first;
            lastPacket = segIt->second.lastPacket;
          }
          if (oRend.size()){
            readySegment & S = segs[oRend];
            while (!S.S.hasPacket() && S.byteOffset <= S.data.size() - 188){
              S.S.parse(S.data + S.byteOffset, 0);
              S.byteOffset += 188;
            }
            if (S.S.hasPacket()){
              S.S.getEarliestPacket(thisPacket);
              if (!S.offsetCalcd){
                S.timeOffset = S.time - thisPacket.getTime();
                S.offsetCalcd = true;
              }
              timeOffset = S.timeOffset;
              trackId = (S.ID << 16) + thisPacket.getTrackId();
              size_t idx = M.trackIDToIndex(trackId, getpid());
              if (idx == INVALID_TRACK_ID || !M.getCodec(idx).size()){
                INFO_MSG("Initializing track %zi (index %zi) as %" PRIu64 " for playlist %" PRIu64, thisPacket.getTrackId(), idx, trackId, S.ID);
                S.S.initializeMetadata(meta, thisPacket.getTrackId(), trackId);
              }
            }
            if (S.byteOffset >= S.data.size() && !S.S.hasPacket()){
              S.fullyWritten = false;
              S.fullyRead = true;
            }
          }
        }
        if (!thisPacket){Util::sleep(25);}
      }

      if (thisPacket){
        char *data = thisPacket.getData();
        //overwrite trackID
        Bit::htobl(data + 8, trackId);
        //overwrite packettime
        Bit::htobll(data + 12, thisPacket.getTime() + timeOffset);
      }
    }
    bool checkArguments(){return true;}
    bool readHeader(){return true;}
    bool openStreamSource(){return true;}
    void parseStreamHeader(){}
    virtual bool publishesTracks(){return false;}
  };



  /// check source, sink, source_track, codec, bitrate, flags  and process options.
  bool ProcLivepeer::CheckConfig(){
    srand(getpid());
    // Check generic configuration variables
    if (!opt.isMember("source") || !opt["source"] || !opt["source"].isString()){
      FAIL_MSG("invalid source in config!");
      return false;
    }

    if (!opt.isMember("sink") || !opt["sink"] || !opt["sink"].isString()){
      INFO_MSG("No sink explicitly set, using source as sink");
    }
    if (!opt.isMember("custom_url") || !opt["custom_url"] || !opt["custom_url"].isString()){
      api_url = "https://livepeer.live/api";
    }else{
      api_url = opt["custom_url"].asStringRef();
    }

    return true;
  }

  void ProcLivepeer::Run(){

    HTTP::Downloader dl;
    dl.setHeader("Authorization", "Bearer "+opt["access_token"].asStringRef());
    //Get broadcaster list, pick first valid address
    if (!dl.get(HTTP::URL(api_url+"/broadcaster"))){
      Util::logExitReason("Livepeer API responded negatively to request for broadcaster list");
      return;
    }
    lpBroad = JSON::fromString(dl.data());
    if (!lpBroad || !lpBroad.isArray()){
      Util::logExitReason("No Livepeer broadcasters available");
      return;
    }
    pickRandomBroadcaster();
    if (!currBroadAddr.size()){
      Util::logExitReason("No Livepeer broadcasters available");
      return;
    }
    INFO_MSG("Using broadcaster: %s", currBroadAddr.c_str());

    //make transcode request
    JSON::Value pl;
    pl["name"] = "Mist Transcode";
    pl["profiles"] = opt["target_profiles"];
    dl.setHeader("Content-Type", "application/json");
    dl.setHeader("Authorization", "Bearer "+opt["access_token"].asStringRef());
    if (!dl.post(HTTP::URL(api_url+"/stream"), pl.toString())){
      Util::logExitReason("Livepeer API responded negatively to encode request");
      return;
    }
    lpEnc = JSON::fromString(dl.data());
    if (!lpEnc){
      Util::logExitReason("Livepeer API did not respond with JSON");
      return;
    }
    if (!lpEnc.isMember("id")){
      Util::logExitReason("Livepeer API did not respond with a valid ID: %s", dl.data().data());
      return;
    }
    lpID = lpEnc["id"].asStringRef();

    INFO_MSG("Livepeer transcode ID: %s", lpID.c_str());
    doingSetup = false;
    while (conf.is_active && co.is_active){Util::sleep(200);}
    INFO_MSG("Closing process clean");
  }
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
  conf.addOption("target", opt);
  conf.getOption("streamname", true).append(Mist::opt["source"].c_str());
  conf.getOption("target", true).append("-?audio=none&video=maxbps");
  if (Mist::opt.isMember("source_track")){
    conf.getOption("target", true).append("-?audio=none&video=" + Mist::opt["source_track"].asString());
  }
  Mist::ProcessSource::init(&conf);
  conf.is_active = true;
  int devnull = open("/dev/null", O_RDWR);
  Socket::Connection c(devnull, devnull);
  Mist::ProcessSource out(c);
  while (Mist::doingSetup && conf.is_active){Util::sleep(200);}
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
    opt["value"].append(0);
    config.addOption("json", opt);
  }

  capa["codecs"][0u][0u].append("H264");

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "Livepeer";
    capa["desc"] = "Use livepeer to transcode video.";

    capa["optional"]["masksource"]["name"] = "Make source track unavailable for users";
    capa["optional"]["masksource"]["help"] = "If enabled, makes the source track internal-only, so that external users and pushes cannot access it.";
    capa["optional"]["masksource"]["type"] = "boolean";
    capa["optional"]["masksource"]["default"] = false;

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] = "What stream the encoded track should be added to. Defaults "
                                       "to source stream. May contain variables.";
    capa["optional"]["sink"]["type"] = "string";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    capa["optional"]["source_track"]["name"] = "Input selection";
    capa["optional"]["source_track"]["help"] =
        "Track ID, codec or language of the source stream to encode.";
    capa["optional"]["source_track"]["type"] = "track_selector_parameter";
    capa["optional"]["source_track"]["n"] = 1;
    capa["optional"]["source_track"]["default"] = "automatic";

    capa["required"]["access_token"]["name"] = "Access token";
    capa["required"]["access_token"]["help"] = "Your livepeer access token";
    capa["required"]["access_token"]["type"] = "string";

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
    JSON::Value &grp = capa["required"]["target_profiles"]["required"];
    grp["name"]["name"] = "Name";
    grp["name"]["help"] = "Name for the profle. Must be unique within this transcode.";
    grp["name"]["type"] = "str";
    grp["fps"]["name"] = "Framerate";
    grp["fps"]["help"] = "Framerate of the output";
    grp["fps"]["unit"] = "frames per second";
    grp["fps"]["type"] = "int";
    grp["gop"]["name"] = "Keyframe interval / GOP size";
    grp["gop"]["help"] = "Interval of keyframes / duration of GOPs for the transcode. Empty string means to match input (= the default), 'intra' means to send only key frames. Otherwise, fractional seconds between keyframes.";
    grp["gop"]["unit"] = "seconds";
    grp["gop"]["type"] = "str";
    grp["width"]["name"] = "Width";
    grp["width"]["help"] = "Width in pixels of the output";
    grp["width"]["unit"] = "px";
    grp["width"]["type"] = "int";
    grp["height"]["name"] = "Height";
    grp["height"]["help"] = "Height in pixels of the output";
    grp["height"]["unit"] = "px";
    grp["height"]["type"] = "int";
    grp["bitrate"]["name"] = "Bitrate";
    grp["bitrate"]["help"] = "Target bit rate of the output";
    grp["bitrate"]["unit"] = "bits per second";
    grp["bitrate"]["type"] = "int";

    capa["optional"]["track_inhibit"]["name"] = "Track inhibitor(s)";
    capa["optional"]["track_inhibit"]["help"] =
        "What tracks to use as inhibitors. If this track selector is able to select a track, the "
        "process does not start. Defaults to none.";
    capa["optional"]["track_inhibit"]["type"] = "string";
    capa["optional"]["track_inhibit"]["validate"][0u] = "track_selector";
    capa["optional"]["track_inhibit"]["default"] = "audio=none&video=none&subtitle=none";

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
  Mist::ProcLivepeer Enc;
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }

  // stream which connects to input
  tthread::thread source(sourceThread, 0);
  Util::sleep(500);

  // needs to pass through encoder to outputEBML
  tthread::thread sink(sinkThread, 0);

  co.is_active = true;

  // run process
  Enc.Run();

  co.is_active = false;
  conf.is_active = false;

  sink.join();
  source.join();

  INFO_MSG("Livepeer transcode shutting down: %s", Util::exitReason);
  return 0;
}

