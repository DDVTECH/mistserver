#include <algorithm> //for std::find
#include <fstream>
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat
#include <mist/procs.h>
#include <mist/util.h>
#include <mist/downloader.h>
#include <mist/websocket.h>
#include <mist/encode.h>
#include <mist/config.h>
#include "process_livepeertext.h"
#include "../output/output.h"
#include "../input/input.h"

Util::Config co;
Util::Config conf;
std::string api_url;

Socket::Connection C;
HTTP::Websocket * aacWs;

namespace Mist{

  //Source process, takes data from input stream and sends to livepeer
  class ProcessSource : public Output{
  public:
    bool isRecording(){return false;}
    ProcessSource(Socket::Connection &c) : Output(c){
      capa["name"] = "LivepeerText";
      capa["codecs"][0u][1u].append("AAC");
      capa["codecs"][0u][1u].append("opus");
      capa["codecs"][0u][1u].append("MP3");
      wantRequest = false;
      parseData = true;
      initialize();
      if (M && userSelect.size() == 1){
        size_t idx = userSelect.begin()->first;
        std::map<std::string, std::string> custHeaders;
        custHeaders["X-WS-Audio-Codec"] = M.getCodec(idx);
        custHeaders["X-WS-Audio-Channels"] = JSON::Value(M.getChannels(idx)).asString();
        custHeaders["X-WS-Rate"] = JSON::Value(M.getRate(idx)).asString();
        custHeaders["X-WS-BitRate"] = JSON::Value(M.getBps(idx)*8).asString();
        custHeaders["X-WS-Init"] = Encodings::Base64::encode(M.getInit(idx));
        custHeaders["X-WS-Language"] = M.getLang(idx);
        custHeaders["Sec-WebSocket-Protocol"] = "speechtotext.livepeer.com";
        aacWs = new HTTP::Websocket(C, HTTP::URL(api_url), &custHeaders);
        if (C){C.setBlocking(true);}
      }
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
      if (!*aacWs){
        INFO_MSG("Websocket connection was closed");
        myConn.close();
        conf.is_active = false;
        co.is_active = false;
        return;
      }
      char *dataPointer = 0;
      size_t len = 0;
      thisPacket.getString("data", dataPointer, len);

      std::string head;
      if (M.getCodec(thisIdx) == "AAC"){head = TS::getAudioHeader(len, M.getInit(thisIdx));}
      aacWs->sendFrameHead(head.size()+len+8, 2);
      char ts[8];
      Bit::htobll(ts, thisPacket.getTime());
      aacWs->sendFrameData(ts, 8);
      if (head.size()){aacWs->sendFrameData(head.data(), head.size());}
      aacWs->sendFrameData(dataPointer, len);
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
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
      if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
        DTSC::trackValidDefault = opt["target_mask"].asInt();
      }
      preRun();
    };
    virtual bool needsLock(){return false;}
    bool isSingular(){return false;}
  private:
    bool needHeader(){return false;}
    virtual void getNext(size_t ignIdx = INVALID_TRACK_ID){
      thisPacket.null();
      while (!aacWs){Util::sleep(100);}
      if (!*aacWs){
        //Disconnected
        INFO_MSG("Websocket connection was closed");
        conf.is_active = false;
        co.is_active = false;
        return;
      }
      while (true){
        while (!aacWs->readFrame()){Util::sleep(100);}
        INFO_MSG("Received: %.*s", (int)aacWs->data.size(), (char*)(aacWs->data));

        //Attempt JSON decode, check for text/timestamp fields
        JSON::Value tData = JSON::fromString(aacWs->data, aacWs->data.size());
        if (!tData.isMember("text") || !tData["text"].asStringRef().size() || !tData.isMember("timestamp")){continue;}
        //Add track if needed
        size_t idx = M.trackIDToIndex(1, getpid());
        if (idx == INVALID_TRACK_ID){
          idx = meta.addTrack();
          meta.setID(idx, 1);
          meta.setType(idx, "meta");
          meta.setCodec(idx, "subtitle");
        }
        //Return packet with data
        uint64_t time = tData["timestamp"].asInt();
        std::string text = tData["text"].asString();
        thisPacket.genericFill(time, 0, 1, text.data(), text.size(), 0, true);
        return;
      }
    }
    bool checkArguments(){return true;}
    bool readHeader(){return true;}
    bool openStreamSource(){return true;}
    void parseStreamHeader(){}
    virtual bool publishesTracks(){return false;}
  };



  /// check source, sink, source_track, codec, bitrate, flags  and process options.
  bool ProcLivepeerText::CheckConfig(){
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
      api_url = "ws://34.68.242.54:8080/speech2text";
    }else{
      api_url = opt["custom_url"].asStringRef();
    }

    return true;
  }

  void ProcLivepeerText::Run(){

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

  //Check for source track selection, default to maxbps
  std::string audio_select = "maxbps";
  if (Mist::opt.isMember("source_track") && Mist::opt["source_track"].isString() && Mist::opt["source_track"]){
    audio_select = Mist::opt["source_track"].asStringRef();
  }
  conf.getOption("target", true).append("-?audio="+audio_select+"&video=none");

  Mist::ProcessSource::init(&conf);
  conf.is_active = true;
  int devnull = open("/dev/null", O_RDWR);
  Socket::Connection c(devnull, devnull);
  Mist::ProcessSource out(c);
  while (!aacWs && conf.is_active){Util::sleep(200);}
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


  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){
    JSON::Value capa;
    capa["name"] = "LivepeerText";
    capa["desc"] = "Use livepeer to transcode audio to text.";
    capa["codecs"][0u][0u].append("AAC");
    capa["codecs"][0u][0u].append("opus");
    capa["codecs"][0u][0u].append("MP3");

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

    capa["optional"]["sink"]["name"] = "Target stream";
    capa["optional"]["sink"]["help"] = "What stream the encoded track should be added to. Defaults "
                                       "to source stream. May contain variables.";
    capa["optional"]["sink"]["type"] = "string";
    capa["optional"]["sink"]["validate"][0u] = "streamname_with_wildcard_and_variables";

    capa["optional"]["source_track"]["name"] = "Input selection";
    capa["optional"]["source_track"]["help"] =
        "Track selector(s) of the audio portion of the source stream. Defaults to highest bit rate audio track.";
    capa["optional"]["source_track"]["type"] = "track_selector_parameter";
    capa["optional"]["source_track"]["n"] = 1;
    capa["optional"]["source_track"]["default"] = "maxbps";

    capa["optional"]["leastlive"]["name"] = "Start in the past";
    capa["optional"]["leastlive"]["help"] = "Start the transcode as far back in the past as possible, instead of at the most-live point of the stream.";
    capa["optional"]["leastlive"]["type"] = "boolean";
    capa["optional"]["leastlive"]["default"] = false;

    capa["optional"]["custom_url"]["name"] = "Custom API URL";
    capa["optional"]["custom_url"]["help"] = "Alternative API URL path";
    capa["optional"]["custom_url"]["type"] = "string";
    capa["optional"]["custom_url"]["default"] = "ws://34.68.242.54:8080/speech2text";

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
  Mist::ProcLivepeerText Enc;
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }

  tthread::thread source(sourceThread, 0);
  Util::sleep(500);
  tthread::thread sink(sinkThread, 0);

  co.is_active = true;
  while (conf.is_active && co.is_active){Util::sleep(200);}
  co.is_active = false;
  conf.is_active = false;

  sink.join();
  source.join();
  if (aacWs){delete aacWs;}

  INFO_MSG("LivepeerText transcode shutting down: %s", Util::exitReason);
  return 0;
}

