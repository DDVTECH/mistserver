#include "../lib/json.cpp"
#include "../lib/downloader.cpp"
#include <list>
#include <stdio.h>
#include <mist/procs.h>
#include <unistd.h>
#include <getopt.h>
#include <stdlib.h>
#include <string>
#include <../lib/config.h>
#include <../lib/util.h>
#include <iostream> 
#include <loadtest_html.h>

// These variables must be global because the prometheus thread uses them
std::string jsonUrl;
JSON::Value promLogs;
bool thread_handler = false;

std::string getFileExt(const std::string& s) {
  if(s.substr(0,4) == "rtmp" ){
    return "rtmp";
  }

  size_t i = s.rfind('.', s.length());
  if (i != std::string::npos) {
    return(s.substr(i+1, s.length() - i));
  }

  return("");
}

std::string exec(const char* cmd) {
    char buffer[128];
    std::string result = "";
    FILE* pipe = popen(cmd, "r");
        while (fgets(buffer, sizeof buffer, pipe) != NULL) {
            result += buffer;
        }
    pclose(pipe);
    return result;
}

/// Holds the PID and stdout integer for a running viewer process
class Process{
public:
  Process(pid_t p, int o){
    myPid = p;
    output = o;
    for (int i = 0; i < 6; ++i){data[i] = 0;}
  }
  /// Checks if the process has finished, and attempts to parse its output
  bool done(){
    // Still running? We're not done yet.
    if (Util::Procs::childRunning(myPid)){return false;}

    char dataString[512];
    ssize_t bytes = read(output, dataString, 512);
    close(output);
    if (bytes == -1){
      FAIL_MSG("Could not read response from fd %d: %s", output, strerror(errno));
    }
    DONTEVEN_MSG("Received %zd bytes of data from process %zu: %s", bytes, (size_t)myPid, dataString);
    sscanf(dataString, "%" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64 ", %" PRIu64,
           &data[0], &data[1], &data[2], &data[3], &data[4], &data[5]);
    return true;
  }
  /// Creates a JSON::Value array of the output values logged for this process
  JSON::Value getData(){
    JSON::Value tmp;
    for (int i = 0; i < 6; ++i){tmp.append(data[i]);}
    return tmp;
  }
  pid_t myPid;
  int output;
  uint64_t data[6];
};

/// Starts a process to view the given URL.
// \TODO Protocol detection should probably use Mist's capabilities system instead
Process newViewer(int time, HTTP::URL url, const std::string &protocol){
  std::deque<std::string> args;
  
  args.push_back("./MistAnalyser" + protocol);
  args.push_back(url.getUrl());
  args.push_back("-T");
  args.push_back(JSON::Value((uint64_t)time).asString());
  args.push_back("-V");


  // Start process and return class holding it
  int fout = -1, ferr = fileno(stderr);
  pid_t proc = Util::Procs::StartPiped(args, 0, &fout, &ferr);
  Util::Procs::socketList.insert(fout);
  return Process(proc, fout);
}


/// Downloads prometheus data at most once per second and lots it into promLogs global variable
void prom_fetch(void * n){
  HTTP::Downloader d;
  d.dataTimeout = 15;

  while (thread_handler){
    if(d.get(jsonUrl)){
      JSON::Value jsonData;
      JSON::Value J = JSON::fromString(d.data());
      jsonData["bootMS"] = Util::bootMS();
      jsonData["bootSecs"] = Util::bootMS();
      jsonData["timestamp"] = Util::getMS(); 
      jsonData["data"] = J;
      
      promLogs.append(jsonData);
      INFO_MSG("CPU: %" PRIu64 ", mem: %" PRIu64, J["cpu"].asInt(), J["mem_used"].asInt());
    }else{
      FAIL_MSG("cannot get prom url: %s", jsonUrl.c_str());
    }

    Util::sleep(1000);
  }
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
  option["long"] = "streaminfo";
  option["help"] = "info_STREAM.js URL for stream to test";
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

  if (!config.parseArgs(argc, argv)){
    config.printHelp(std::cout);
    return 0;
  }
  config.activate();
  
  int total_time = config.getInteger("timelimit"); // time to run in seconds
  int total = config.getInteger("connections");    // total connections
  HTTP::URL url(config.getString("url"));
  jsonUrl = config.getString("prometheus"); // URL for prometheus JSON output

  
  std::list<Process> processes;

  //check wich analyser is needed
  std::string ext = url.getExt();
  std::string protocol;

  if (url.protocol == "rtmp"){
    protocol = "RTMP";
  }else if(ext == "flv"){
    protocol = "FLV";
  }else if(ext == "mp4"){
    protocol = "MP4";
  }else if(ext == "m3u8"){
    protocol = "HLS";
  }else if(ext == "ts"){
    protocol = "TS";
  }else if(ext == "webm"){
    protocol = "EBML";
  }else if(url.path.find("webrtc/") != std::string::npos){
    protocol = "WebRTC";
  }else{
    FAIL_MSG("I don't know how to analyse stream '%s', sorry.", url.getUrl().c_str());
    return 1;
  }

  // start prometheus collection thread
  thread_handler = true;
  tthread::thread prom(prom_fetch, 0);

  for(int i = 0; i < total; i++){
/* burst
    if(i % 25 == 0 && i > 0){
      Util::sleep(1000);
    }
*/
    processes.push_back(newViewer(total_time, url, protocol));
  }


  WARN_MSG("All processes started");
  size_t successful = 0;
  size_t zeroes = 0;

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
          JSON::Value data = it->getData();
          if ((data[3].asInt() - data[4].asInt()) > (total_time - 5) * 1000){successful++;}
          if (!data[5].asInt()){zeroes++;}
          viewer_output.append(data);
          processes.erase(it);
          changed = true;
          break;
        }
      }
    }
    if (processes.size() != total){INFO_MSG("processes running: %zu", processes.size());}
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

  {
    // Write raw JSON
    std::ofstream fPromOut("stats.json");
    fPromOut << allStats.toString() << std::endl;
  }
  {
    // Write HTML page
    std::string outputFile;

    time_t rawtime;
    struct tm * timeinfo;
    char buffer[80];

    time (&rawtime);
    timeinfo = localtime(&rawtime);

    strftime(buffer,sizeof(buffer),"%d%m%Y_",timeinfo);
    outputFile.append("loadtest_");
    outputFile.append(std::string(buffer));
    outputFile.append(protocol);
    sprintf(buffer, "_%d_", total);
    outputFile.append(std::string(buffer));
    sprintf(buffer, "%d_", total_time);
    outputFile.append(std::string(buffer));
    strftime(buffer,sizeof(buffer),"%H%M%S",timeinfo);
    outputFile.append(std::string(buffer));
    outputFile.append(".html");
    
    std::ofstream fHtmlOut(outputFile.c_str());
    fHtmlOut << loadtest_html_prefix << allStats.toString() << loadtest_html_suffix << std::endl;
  }
  // Write result to stdout
  std::cout << "Successful connections: " << successful << std::endl;
  std::cout << "No data: " << zeroes << std::endl;

}
