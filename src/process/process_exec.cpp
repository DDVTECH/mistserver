#include "process_exec.h"
#include <algorithm> //for std::find
#include <fstream>
#include <mist/procs.h>
#include <mist/tinythread.h>
#include <mist/util.h>
#include <ostream>
#include <sys/stat.h>  //for stat
#include <sys/types.h> //for stat
#include <unistd.h>    //for stat

int pipein[2], pipeout[2], pipeerr[2];

Util::Config co;
Util::Config conf;

void sinkThread(void *){
  Mist::ProcessSink in(&co);
  co.getOption("output", true).append("-");
  co.activate();
  MEDIUM_MSG("Running sink thread...");
  in.setInFile(pipeout[0]);
  co.is_active = true;
  in.run();
  conf.is_active = false;
}

void sourceThread(void *){
  Mist::ProcessSource::init(&conf);
  conf.getOption("streamname", true).append(Mist::opt["source"].c_str());
  conf.getOption("target", true).append("-?audio=all&video=all");
  if (Mist::opt.isMember("track_select")){
    conf.getOption("target", true).append("-?" + Mist::opt["track_select"].asString());
  }
  conf.is_active = true;
  Socket::Connection c(pipein[1], 0);
  Mist::ProcessSource out(c);
  MEDIUM_MSG("Running source thread...");
  out.run();
  co.is_active = false;
}

int main(int argc, char *argv[]){
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

  if (!(config.parseArgs(argc, argv))){return 1;}
  if (config.getBool("json")){

    capa["name"] = "MKVExec";
    capa["desc"] = "Pipe MKV in, expect MKV out. You choose the executable in between yourself.";

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

  // create pipe pair before thread
  pipe(pipein);
  pipe(pipeout);

  // stream which connects to input
  tthread::thread source(sourceThread, 0);

  // needs to pass through encoder to outputEBML
  tthread::thread sink(sinkThread, 0);

  co.is_active = true;

  // run process
  Enc.Run();

  co.is_active = false;
  conf.is_active = false;

  // close pipes
  close(pipein[0]);
  close(pipeout[0]);
  close(pipein[1]);
  close(pipeout[1]);

  sink.join();
  HIGH_MSG("sink thread joined")

  source.join();
  HIGH_MSG("source thread joined");

  return 0;
}

namespace Mist{
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
    Util::Procs p;
    int ffer = 2;
    pid_t execd_proc = -1;

    // exec command
    char exec_cmd[10240];
    strncpy(exec_cmd, opt["exec"].asString().c_str(), 10240);
    MEDIUM_MSG("Executing command: %s", exec_cmd);
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

    execd_proc = p.StartPiped(args, &pipein[0], &pipeout[1], &ffer);

    while (conf.is_active && p.isRunning(execd_proc)){Util::sleep(200);}

    while (p.isRunning(execd_proc)){
      MEDIUM_MSG("Stopping process...");
      p.StopAll();
      Util::sleep(200);
    }

    MEDIUM_MSG("Closing process clean");
  }
}// namespace Mist
