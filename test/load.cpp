#include "../lib/json.cpp"
#include "../lib/downloader.cpp"
#include <list>
#include <stdio.h>
#include <mist/procs.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string>
#include "../lib/config.h"
#include "../lib/util.h"
#include <iostream>
#include <loadtest_html.h>
#include <sys/resource.h>

// These variables must be global because the prometheus thread uses them
std::string jsonUrl;
JSON::Value promLogs;
bool thread_handler = false;

/// Holds the PID and stdout integer for a running viewer process
class Process{
public:
  Process(pid_t p, int o){
    myPid = p;
    output = o;
  }
  /// Checks if the process has finished, and attempts to parse its output
  bool done(){
    // Still running? We're not done yet.
    if (Util::Procs::childRunning(myPid)){return false;}

    char dataString[1024*10];
    ssize_t bytes = read(output, dataString, 1024*10);
    close(output);
    if (bytes == -1){
      FAIL_MSG("Could not read response from fd %d: %s", output, strerror(errno));
    }
    DONTEVEN_MSG("Received %zd bytes of data from process %zu: %s", bytes, (size_t)myPid, dataString);
    out = JSON::fromString(dataString, bytes);
    return true;
  }
  /// Creates a JSON::Value array of the output values logged for this process
  JSON::Value & getData(){return out;}
  pid_t myPid;
  int output;
  JSON::Value out;
};

/// Starts a process to view the given URL.
Process newViewer(int time, HTTP::URL url, const std::string &protocol){
  std::deque<std::string> args;

  args.push_back(Util::getMyPath() + "MistAnalyser" + protocol);
  args.push_back(url.getUrl());
  args.push_back("-T");
  args.push_back(JSON::Value((uint64_t)time).asString());
  args.push_back("-V");

  if(url.protocol == "srt"){
    args.push_back("-A");
  }

  // Start process and return class holding it
  int fout = -1, ferr = fileno(stderr);
  pid_t proc = Util::Procs::StartPiped(args, 0, &fout, &ferr);
  Util::Procs::socketList.insert(fout);
  return Process(proc, fout);
}

/// Downloads prometheus data at most once per second and lots it into promLogs global variable
void prom_fetch(void * n){
  HTTP::Downloader d;
  int total_time = *((int*)n);
  uint32_t sleepTime = total_time*3.333;
  if (sleepTime < 1000){sleepTime = 1000;}
  uint64_t started = Util::bootSecs();
  d.dataTimeout = 15;
  while (thread_handler){
    if(d.get(jsonUrl)){
      JSON::Value J = JSON::fromString(d.data());
      if (J.isMember("cpu") && J.isMember("mem_total") && J["mem_total"].asInt()){
        int t = (Util::bootSecs()-started);
        if (total_time > t){t = total_time - t;}else{t = 0;}
        INFO_MSG("%#3.1f%% CPU, %#3.1f%% RAM; %um%us left", J["cpu"].asInt() / 10.0, (J["mem_used"].asInt()*100)/(double)J["mem_total"].asInt(), ((int)(t) % 3600) / 60, (int)(t) % 60);
        JSON::Value jsonData;
        jsonData["bootMS"] = Util::bootMS();
        jsonData["timestamp"] = Util::getMS();
        jsonData["data"] = J;
        promLogs.append(jsonData);
      }else{
        WARN_MSG("Invalid system statistics JSON read from %s", jsonUrl.c_str());
      }
    }
    Util::sleep(sleepTime);
  }
}

bool sysSetNrOpenFiles(int n){
    struct rlimit limit;
    
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) {
      FAIL_MSG("getrlimit() failed with errno=%d\n", errno);
      return false;
    }

    if(limit.rlim_cur < n){
      limit.rlim_cur = n;
      if (setrlimit(RLIMIT_NOFILE, &limit) != 0) {
        FAIL_MSG("setrlimit() '%d' failed err: %d: %s\n",n, errno, strerror(errno));
        return false;
      }

      INFO_MSG("ulimit increased to: %d", limit.rlim_cur)
    }else{
      
      INFO_MSG("current ulimit value not changed. Current: %d, wanted: %d", limit.rlim_cur,n)
    }

    return true;
  }


int main(int argc, char *argv[]){
  Util::redirectLogsIfNeeded();
  Util::Config config(argv[0]);

  JSON::Value option;
  option = JSON::Value();
  option["arg_num"] = 1;
  option["arg"] = "string";
  option["help"] = "URL to analyse";
  config.addOption("url", option);
  option.null();

  option = JSON::Value();
  option["arg"] = "string";
  option["short"] = "I";
  option["default"] = "";
  option["long"] = "streaminfo";
  option["help"] = "json_STREAM.js URL for stream to test";
  config.addOption("streaminfo", option);
  option.null();

  option["arg"] = "integer";
  option["default"] = 30;
  option["short"] = "t";
  option["long"] = "time-limit";
  option["help"] = "How long to test (seconds)";
  config.addOption("timelimit", option);
  option.null();

  option["arg"] = "integer";
  option["default"] = 10;
  option["short"] = "n";
  option["long"] = "connections";
  option["help"] = "Number of connections to make";
  config.addOption("connections", option);
  option.null();

  option["arg"] = "string";
  option["default"] = "";
  option["short"] = "S";
  option["long"] = "prometheus";
  option["help"] = "Full prometheus JSON output URL for server to test";
  config.addOption("prometheus", option);
  option.null();

  option["arg"] = "integer";
  option["default"] = 0;
  option["short"] = "l";
  option["long"] = "ulimit";
  option["help"] = "Increase ulimit only if current setting is below value";
  config.addOption("ulimit", option);
  option.null();

  option["arg"] = "integer";
  option["default"] = 0;
  option["short"] = "d";
  option["long"] = "delay";
  option["help"] = "Delay between viewers in milliseconds";
  config.addOption("delay", option);
  option.null();

  option["arg"] = "string";
  option["default"] = "";
  option["short"] = "o";
  option["long"] = "outputdir";
  option["help"] = "Output directory to save json/html results e.g. /tmp/";
  config.addOption("outputdir", option);
  option.null();

  if (!config.parseArgs(argc, argv)){
    config.printHelp(std::cout);
    return 0;
  }
  config.activate();

  int total_time = config.getInteger("timelimit"); // time to run in seconds
  int total = config.getInteger("connections");    // total connections
  HTTP::URL url(config.getString("url"));
  jsonUrl = config.getString("prometheus"); // URL for prometheus JSON output

  int ulimit = config.getInteger("ulimit");
  if(ulimit > 0){
    sysSetNrOpenFiles(ulimit);
  }

  std::list<Process> processes;

  //check which analyser is needed
  std::string ext = url.getExt();
  std::string protocol;

  // \TODO Protocol detection should probably use Mist's capabilities system instead
  if (url.protocol == "rtmp"){
    protocol = "RTMP";
  }else if (url.protocol == "srt"){
    protocol = "TS";
  }else if(ext == "flv"){
    protocol = "FLV";
  }else if(ext == "mp4"){
    protocol = "MP4";
  }else if(ext == "m3u8"){
    protocol = "HLS";
  }else if(ext == "ts"){
    protocol = "TS";
  }else if(ext == "webm" || ext == "mkv"){
    protocol = "EBML";
  }else if(url.path.find("webrtc/") != std::string::npos){
    protocol = "WebRTC";
  }else{
    FAIL_MSG("I don't know how to analyse stream '%s', sorry.", url.getUrl().c_str());
    return 1;
  }

  // start prometheus collection thread
  thread_handler = true;
  tthread::thread prom(prom_fetch, &total_time);

  uint64_t delay = config.getInteger("delay");
  for(int i = 0; i < total; i++){
    processes.push_back(newViewer(total_time, url, protocol));
    if (delay){Util::sleep(delay);}
  }


  WARN_MSG("All analysers started");
  size_t successful = 0;

  JSON::Value allStats;
  JSON::Value &viewer_output = allStats["viewer_output"];
  while (processes.size() > 0 && config.is_active){
    bool changed = true;
    std::list<Process>::iterator it;
    while (processes.size() && changed){
      changed = false;
      it = processes.begin();
      for (it = processes.begin(); it != processes.end(); ++it){
        if (it->done()){
          JSON::Value & data = it->getData();
          if ((data["media_stop"].asInt() - data["media_start"].asInt()) > (total_time - 5) * 1000){successful++;}
          viewer_output.append(data);
          processes.erase(it);
          changed = true;
          break;
        }
      }
    }
    if (processes.size() != total){INFO_MSG("Analysers running: %zu", processes.size());}
    Util::wait(1000);
  }

  allStats["viewers"] = total;
  allStats["viewers_passed"] = successful;
  allStats["testing_time"] = total_time;
  allStats["protocol"] = protocol;
  allStats["host"] = url.host;
  allStats["url"] = url.getUrl();

  // Join prometheus thread before we attempt to read its collected data
  thread_handler = false;
  prom.join();
  allStats["dataPoints"] = promLogs;

  {
    // Collect stream info
    HTTP::Downloader d;
    d.get(config.getString("streaminfo"));
    allStats["streamInfo"] = JSON::fromString(d.data());
  }


  std::stringstream outFile;
  {
    // Generate filename for outputs
    time_t rawtime;
    struct tm * timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    char buffer[16];
    strftime(buffer,sizeof(buffer),"%Y%m%d_",timeinfo);
    outFile << "loadtest_" << buffer << protocol << "_" << total << "x_" << total_time << "s_";
    strftime(buffer,sizeof(buffer),"%H%M%S",timeinfo);
    outFile << buffer;
  }
  {
    // Write raw JSON
    std::string fName = config.getString("outputdir")+"/"+outFile.str()+".json";
    std::ofstream fPromOut(fName.c_str());
    fPromOut << allStats.toString() << std::endl;
  }
  {
    // Write HTML page
    std::string fName = config.getString("outputdir")+"/"+outFile.str()+".html";
    std::ofstream fHtmlOut(fName.c_str());
    fHtmlOut << loadtest_html_prefix << allStats.toString() << loadtest_html_suffix << std::endl;
  }
  // Write result to stdout
  std::cout << "Successful connections: " << successful << " / " << total << std::endl;
  return 0;
}
