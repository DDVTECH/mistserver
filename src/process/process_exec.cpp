#include "process_exec.h"
#include "process.hpp"
#include <mist/procs.h>
#include <mist/util.h>
#include <thread>
#include <mutex>
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat
#include <condition_variable>
#include <mutex>

int pipein = -1, pipeout = -1;

std::condition_variable xCV;
std::mutex xMutex;

Util::Config co;
Util::Config conf;

//Stat related stuff
JSON::Value pStat;
JSON::Value & pData = pStat["proc_status_update"]["status"];
std::mutex statsMutex;
uint64_t statSinkMs = 0;
uint64_t statSourceMs = 0;
int64_t bootMsOffset = 0;

namespace Mist{

  class ProcessSink : public InputEBML{
  public:
    ProcessSink(Util::Config *cfg) : InputEBML(cfg){
      capa["name"] = "MKVExec";
    };
    void getNext(size_t idx = INVALID_TRACK_ID){
      {
        std::lock_guard<std::mutex> guard(statsMutex);
        if (pData["sink_tracks"].size() != userSelect.size()){
          pData["sink_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["sink_tracks"].append((uint64_t)it->first);
          }
        }
      }
      static bool recurse = false;
      if (recurse){return InputEBML::getNext(idx);}
      recurse = true;
      InputEBML::getNext(idx);
      recurse = false;
      uint64_t pTime = thisPacket.getTime();
      if (thisPacket){
        if (!getFirst){
          packetTimeDiff = sendPacketTime - pTime;
          getFirst = true;
        }
        pTime += packetTimeDiff;
        // change packettime
        char *data = thisPacket.getData();
        Bit::htobll(data + 12, pTime);
        if (pTime >= statSinkMs){statSinkMs = pTime;}
        if (meta && meta.getBootMsOffset() != bootMsOffset){meta.setBootMsOffset(bootMsOffset);}
      }
    }
    void setInFile(int stdin_val){
      inFile.open(stdin_val);
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      Util::streamVariables(streamName, opt["source"].asString());
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
      {
        std::lock_guard<std::mutex> guard(statsMutex);
        pStat["proc_status_update"]["sink"] = streamName;
        pStat["proc_status_update"]["source"] = opt["source"];
      }
      if (opt.isMember("target_mask") && !opt["target_mask"].isNull() && opt["target_mask"].asString() != ""){
        DTSC::trackValidDefault = opt["target_mask"].asInt();
      }
    }
    bool needsLock(){return false;}
    bool isSingular(){return false;}
    void connStats(Comms::Connections &statComm){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second){it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus());}
      }
      InputEBML::connStats(statComm);
    }
  };

  class ProcessSource : public OutEBML{
  public:
    bool isRecording(){return false;}
    ProcessSource(Socket::Connection &c) : OutEBML(c){
      capa["name"] = "MKVExec";
      targetParams["keeptimes"] = true;
      realTime = 0;
      subtractTime = 0;
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
      return OutEBML::onFinish();
    }
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true){
      if (opt.isMember("exit_unmask") && opt["exit_unmask"].asBool()){
        INFO_MSG("Unmasking source track %zu" PRIu64, trackId);
        meta.validateTrack(trackId, TRACK_VALID_ALL);
      }
      OutEBML::dropTrack(trackId, reason, probablyBad);
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
      OutEBML::sendHeader();
    };
    void connStats(uint64_t now, Comms::Connections &statComm){
      for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
        if (it->second){it->second.setStatus(COMM_STATUS_DONOTTRACK | it->second.getStatus());}
      }
      OutEBML::connStats(now, statComm);
    }
    void sendNext(){
      {
        std::lock_guard<std::mutex> guard(statsMutex);
        if (pData["source_tracks"].size() != userSelect.size()){
          pData["source_tracks"].null();
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            pData["source_tracks"].append((uint64_t)it->first);
          }
        }
      }
      if (thisTime-subtractTime > statSourceMs){statSourceMs = thisTime-subtractTime;}
      needsLookAhead = 0;
      maxSkipAhead = 0;
      if (!sendFirst){
        sendPacketTime = thisPacket.getTime();
        bootMsOffset = M.getBootMsOffset();
        sendFirst = true;
        /*
        uint64_t maxJitter = 1;
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti !=
        userSelect.end(); ++ti){if (!M.trackValid(ti->first)){continue;
          }// ignore missing tracks
          if (M.getMinKeepAway(ti->first) > maxJitter){
            maxJitter = M.getMinKeepAway(ti->first);
          }
        }
        DTSC::veryUglyJitterOverride = maxJitter;
        */
      }
      OutEBML::sendNext();
    }
  };

  /// check source, sink, source_track, codec, bitrate, flags  and process options.
  bool ProcMKVExec::CheckConfig(){
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

  void ProcMKVExec::Run(){
    int ffer = 2;
    pid_t execd_proc = -1;


    std::string streamName = opt["sink"].asString();
    if (!streamName.size()){streamName = opt["source"].asStringRef();}
    Util::streamVariables(streamName, opt["source"].asStringRef());
    
    //Do variable substitution on command
    std::string tmpCmd = opt["exec"].asStringRef();
    Util::streamVariables(tmpCmd, streamName, opt["source"].asStringRef());

    // exec command
    char exec_cmd[10240];
    strncpy(exec_cmd, tmpCmd.c_str(), 10240);
    INFO_MSG("Executing command: %s", exec_cmd);
    uint8_t argCnt = 0;
    char *startCh = 0;
    char *args[1280];
    for (char *i = exec_cmd; i - exec_cmd < 10240; ++i){
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


    {
      std::unique_lock<std::mutex> lk(xMutex);
      xCV.wait(lk, []() { return conf.is_active && co.is_active; });
      execd_proc = Util::Procs::StartPiped(args, &pipein, &pipeout, &ffer);
      if (!execd_proc) { return; }
    }
    xCV.notify_all();

    uint64_t lastProcUpdate = Util::bootSecs();
    {
      std::lock_guard<std::mutex> guard(statsMutex);
      pStat["proc_status_update"]["id"] = getpid();
      pStat["proc_status_update"]["proc"] = "MKVExec";
      pData["ainfo"]["child_pid"] = execd_proc;
      pData["ainfo"]["cmd"] = opt["exec"];
    }
    uint64_t startTime = Util::bootSecs();
    while (conf.is_active && Util::Procs::isRunning(execd_proc)){
      Util::sleep(200);
      if (lastProcUpdate + 5 <= Util::bootSecs()){
        std::lock_guard<std::mutex> guard(statsMutex);
        pData["active_seconds"] = (Util::bootSecs() - startTime);
        pData["ainfo"]["sourceTime"] = statSourceMs;
        pData["ainfo"]["sinkTime"] = statSinkMs;
        Util::sendUDPApi(pStat);
        lastProcUpdate = Util::bootSecs();
      }
    }

    while (Util::Procs::isRunning(execd_proc)){
      INFO_MSG("Stopping process...");
      Util::Procs::StopAll();
      Util::sleep(200);
    }

    INFO_MSG("Closing process clean");
  }
}// namespace Mist

void sinkThread(){
  Mist::ProcessSink in(&co);
  co.getOption("output", true).append("-");

  {
    std::unique_lock<std::mutex> lk(xMutex);
    co.activate();
  }
  xCV.notify_all();
  {
    std::unique_lock<std::mutex> lk(xMutex);
    xCV.wait(lk, []() { return pipeout != -1 || !co.is_active; });
  }

  MEDIUM_MSG("Running sink thread...");
  in.setInFile(pipeout);
  in.run();

  {
    std::unique_lock<std::mutex> lk(xMutex);
    conf.is_active = false;
  }
  xCV.notify_all();
}

void sourceThread(){
  Mist::ProcessSource::init(&conf);
  conf.getOption("streamname", true).append(Mist::opt["source"].c_str());
  conf.getOption("target", true).append("-?audio=all&video=all");
  if (Mist::opt.isMember("track_select")){
    conf.getOption("target", true).append("-?" + Mist::opt["track_select"].asString());
  }
  {
    std::unique_lock<std::mutex> lk(xMutex);
    conf.is_active = true;
  }
  xCV.notify_all();
  {
    std::unique_lock<std::mutex> lk(xMutex);
    xCV.wait(lk, []() { return pipein != -1 || !conf.is_active; });
  }

  Socket::Connection c(pipein, 0);
  Mist::ProcessSource out(c);

  MEDIUM_MSG("Running source thread...");
  out.run();

  {
    std::unique_lock<std::mutex> lk(xMutex);
    co.is_active = false;
  }
  xCV.notify_all();
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_INT_PROCESS;
  Util::Config config(argv[0]);
  Util::Config::binaryType = Util::PROCESS;
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
  capa["codecs"][0u][0u].append("HEVC");
  capa["codecs"][0u][0u].append("VP8");
  capa["codecs"][0u][0u].append("VP9");
  capa["codecs"][0u][0u].append("theora");
  capa["codecs"][0u][0u].append("MPEG2");
  capa["codecs"][0u][0u].append("AV1");
  capa["codecs"][0u][1u].append("AAC");
  capa["codecs"][0u][1u].append("vorbis");
  capa["codecs"][0u][1u].append("opus");
  capa["codecs"][0u][1u].append("PCM");
  capa["codecs"][0u][1u].append("ALAW");
  capa["codecs"][0u][1u].append("ULAW");
  capa["codecs"][0u][1u].append("MP2");
  capa["codecs"][0u][1u].append("MP3");
  capa["codecs"][0u][1u].append("FLOAT");
  capa["codecs"][0u][1u].append("AC3");
  capa["codecs"][0u][1u].append("DTS");
  capa["codecs"][0u][2u].append("+JSON");

  capa["ainfo"]["sinkTime"]["name"] = "Sink timestamp";
  capa["ainfo"]["sourceTime"]["name"] = "Source timestamp";
  capa["ainfo"]["child_pid"]["name"] = "Child process PID";
  capa["ainfo"]["cmd"]["name"] = "Child process command";

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "MKVExec";
    capa["hrn"] = "Executable: Matroska In/Out";
    capa["desc"] = "Pipe MKV in, expect MKV out. You choose the executable in between yourself.";
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
    capa["optional"]["target_mask"]["help"] = "What internal processes should have access to the output track(s)";
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

    capa["required"]["exec"]["name"] = "Executable";
    capa["required"]["exec"]["help"] = "What to executable to run on the stream data";
    capa["required"]["exec"]["type"] = "string";

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
  Mist::ProcMKVExec Enc;
  if (!Enc.CheckConfig()){
    FAIL_MSG("Error config syntax error!");
    return 1;
  }

  // stream which connects to input
  std::thread source(sourceThread);

  // needs to pass through encoder to outputEBML
  std::thread sink(sinkThread);

  co.is_active = true;

  // run process
  Enc.Run();

  {
    std::unique_lock<std::mutex> lk(xMutex);
    co.is_active = false;
    conf.is_active = false;
  }
  xCV.notify_all();


  source.join();
  HIGH_MSG("source thread joined");

  sink.join();
  HIGH_MSG("sink thread joined")


  return 0;
}
