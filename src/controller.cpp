/// \file controller.cpp
/// Contains all code for the controller executable.

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <set>
#include <cstdlib>
#include <queue>
#include <cmath>
#include <cstdio>
#include <climits>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <set>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <signal.h>
#include <sstream>
#include <openssl/md5.h>
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/procs.h>
#include <mist/auth.h>
#include <mist/timing.h>
#include "server.html.h"

#define UPLINK_INTERVAL 30
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""

namespace Controller{

std::map<std::string, int> lastBuffer; ///< Last moment of contact with all buffers.
Auth keychecker; ///< Checks key authorization.

/// Wrapper function for openssl MD5 implementation
std::string md5(std::string input){
  char tmp[3];
  std::string ret;
  const unsigned char * res = MD5((const unsigned char*)input.c_str(), input.length(), 0);
  for (int i = 0; i < 16; ++i){
    snprintf(tmp, 3, "%02x", res[i]);
    ret += tmp;
  }
  return ret;
}


JSON::Value Storage; ///< Global storage of data.

void WriteFile( std::string Filename, std::string contents ) {
  std::ofstream File;
  File.open( Filename.c_str( ) );
  File << contents << std::endl;
  File.close( );
}

class ConnectedUser{
  public:
    Socket::Connection C;
    HTTP::Parser H;
    bool Authorized;
    bool clientMode;
    int logins;
    std::string Username;
    ConnectedUser(Socket::Connection c){
      C = c;
      H.Clean();
      logins = 0;
      Authorized = false;
      clientMode = false;
    }
};

void Log(std::string kind, std::string message){
  //if last log message equals this one, do not log.
  if (Storage["log"].size() > 0){
    JSON::ArrIter it = Storage["log"].ArrEnd() - 1;
    if ((*it)[2] == message){return;}
  }
  JSON::Value m;
  m.append(Util::epoch());
  m.append(kind);
  m.append(message);
  Storage["log"].append(m);
  Storage["log"].shrink(100);//limit to 100 log messages
  std::cout << "[" << kind << "] " << message << std::endl;
}

void Authorize( JSON::Value & Request, JSON::Value & Response, ConnectedUser & conn ) {
  time_t Time = time(0);
  tm * TimeInfo = localtime(&Time);
  std::stringstream Date;
  std::string retval;
  Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
  std::string Challenge = md5( Date.str().c_str() + conn.C.getHost() );
  if( Request.isMember( "authorize" ) ) {
    std::string UserID = Request["authorize"]["username"];
    if (Storage["account"].isMember(UserID)){
      if (md5(Storage["account"][UserID]["password"].asString() + Challenge) == Request["authorize"]["password"].asString()){
        Response["authorize"]["status"] = "OK";
        conn.Username = UserID;
        conn.Authorized = true;
        return;
      }
    }
    if (UserID != ""){
      if (Request["authorize"]["password"].asString() != "" && md5(Storage["account"][UserID]["password"].asString()) != Request["authorize"]["password"].asString()){
        Log("AUTH", "Failed login attempt "+UserID+" @ "+conn.C.getHost());
      }
    }
    conn.logins++;
  }
  conn.Username = "";
  conn.Authorized = false;
  Response["authorize"]["status"] = "CHALL";
  Response["authorize"]["challenge"] = Challenge;
  return;
}

void CheckProtocols(JSON::Value & p){
  static std::map<std::string, std::string> current_connectors;
  std::map<std::string, std::string> new_connectors;
  std::map<std::string, std::string>::iterator iter;

  std::string tmp;
  JSON::Value counter = (long long int)0;

  //collect object type
  for (JSON::ObjIter jit = p.ObjBegin(); jit != p.ObjEnd(); jit++){
    if (!jit->second.isMember("connector") || jit->second["connector"].asString() == ""){continue;}
    if (!jit->second.isMember("port") || jit->second["port"].asInt() == 0){continue;}
    tmp = "MistConn";
    tmp += jit->second["connector"].asString();
    tmp += " -n -p ";
    tmp += jit->second["port"].asString();
    if (jit->second.isMember("interface") && jit->second["interface"].asString() != "" && jit->second["interface"].asString() != "0.0.0.0"){
      tmp += " -i ";
      tmp += jit->second["interface"].asString();
    }
    if (jit->second.isMember("username") && jit->second["username"].asString() != "" && jit->second["username"].asString() != "root"){
      tmp += " -u ";
      tmp += jit->second["username"].asString();
    }
    counter = counter.asInt() + 1;
    new_connectors[std::string("Conn")+counter.asString()] = tmp;
  }
  //collect array type
  for (JSON::ArrIter ait = p.ArrBegin(); ait != p.ArrEnd(); ait++){
    if (!(*ait).isMember("connector") || (*ait)["connector"].asString() == ""){continue;}
    if (!(*ait).isMember("port") || (*ait)["port"].asInt() == 0){continue;}
    tmp = "MistConn";
    tmp += (*ait)["connector"].asString();
    tmp += " -n -p ";
    tmp += (*ait)["port"].asString();
    if ((*ait).isMember("interface") && (*ait)["interface"].asString() != "" && (*ait)["interface"].asString() != "0.0.0.0"){
      tmp += " -i ";
      tmp += (*ait)["interface"].asString();
    }
    if ((*ait).isMember("username") && (*ait)["username"].asString() != "" && (*ait)["username"].asString() != "root"){
      tmp += " -u ";
      tmp += (*ait)["username"].asString();
    }
    counter = counter.asInt() + 1;
    new_connectors[std::string("Conn")+counter.asString()] = tmp;
    if (Util::Procs::isActive(std::string("Conn")+counter.asString())){
      (*ait)["online"] = 1;
    }else{
      (*ait)["online"] = 0;
    }
  }

  //shut down deleted/changed connectors
  for (iter = current_connectors.begin(); iter != current_connectors.end(); iter++){
    if (new_connectors.count(iter->first) != 1 || new_connectors[iter->first] != iter->second){
      Log("CONF", "Stopping connector: " + iter->second);
      Util::Procs::Stop(iter->first);
    }
  }

  //start up new/changed connectors
  for (iter = new_connectors.begin(); iter != new_connectors.end(); iter++){
    if (current_connectors.count(iter->first) != 1 || current_connectors[iter->first] != iter->second || !Util::Procs::isActive(iter->first)){
      Log("CONF", "Starting connector: " + iter->second);
      Util::Procs::Start(iter->first, iter->second);
    }
  }

  //store new state
  current_connectors = new_connectors;
}

void CheckConfig(JSON::Value & in, JSON::Value & out){
  for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
    if (out.isMember(jit->first)){
      if (jit->second != out[jit->first]){
        if (jit->first != "time"){
          Log("CONF", std::string("Updated configuration value ")+jit->first);
        }
      }
    }else{
      Log("CONF", std::string("New configuration value ")+jit->first);
    }
  }
  for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
    if (!in.isMember(jit->first)){
      Log("CONF", std::string("Deleted configuration value ")+jit->first);
    }
  }
  out = in;
}

bool streamsEqual(JSON::Value & one, JSON::Value & two){
  if (one["channel"]["URL"] != two["channel"]["URL"]){return false;}
  if (one["preset"]["cmd"] != two["preset"]["cmd"]){return false;}
  return true;
}

void startStream(std::string name, JSON::Value & data){
  std::string URL = data["channel"]["URL"];
  std::string preset = data["preset"]["cmd"];
  std::string cmd1, cmd2, cmd3;
  if (URL.substr(0, 4) == "push"){
    std::string pusher = URL.substr(7);
    cmd2 = "MistBuffer -s "+name+" "+pusher;
    Util::Procs::Start(name, cmd2);
    Log("BUFF", "(re)starting stream buffer "+name+" for push data from "+pusher);
  }else{
    if (URL.substr(0, 1) == "/"){
      struct stat fileinfo;
      if (stat(URL.c_str(), &fileinfo) != 0 || S_ISDIR(fileinfo.st_mode)){
        Log("BUFF", "Warning for VoD stream "+name+"! File not found: "+URL);
        data["error"] = "Not found: "+URL;
        return;
      }
      cmd1 = "cat "+URL;
      data["error"] = "Available";
      return; //MistPlayer handles VoD
    }else{
      cmd1 = "ffmpeg -re -async 2 -i "+URL+" "+preset+" -f flv -";
      cmd2 = "MistFLV2DTSC";
    }
    cmd3 = "MistBuffer -s "+name;
    if (cmd2 != ""){
      Util::Procs::Start(name, cmd1, cmd2, cmd3);
      Log("BUFF", "(re)starting stream buffer "+name+" for ffmpeg data: "+cmd1);
    }else{
      Util::Procs::Start(name, cmd1, cmd3);
      Log("BUFF", "(re)starting stream buffer "+name+" using input file "+URL);
    }
  }
}

void CheckStats(JSON::Value & stats){
  long long int currTime = Util::epoch();
  for (JSON::ObjIter jit = stats.ObjBegin(); jit != stats.ObjEnd(); jit++){
    if (currTime - lastBuffer[jit->first] > 120){
      stats.removeMember(jit->first);
      return;
    }else{
      if (jit->second.isMember("curr") && jit->second["curr"].size() > 0){
        for (JSON::ObjIter u_it = jit->second["curr"].ObjBegin(); u_it != jit->second["curr"].ObjEnd(); ++u_it){
          if (u_it->second.isMember("now") && u_it->second["now"].asInt() < currTime - 3){
            jit->second["log"].append(u_it->second);
            jit->second["curr"].removeMember(u_it->first);
            if (!jit->second["curr"].size()){break;}
            u_it = jit->second["curr"].ObjBegin();
          }
        }
      }
    }
  }
}

class cpudata {
  public:
    std::string model;
    int cores;
    int threads;
    int mhz;
    int id;
    cpudata(){
      model = "Unknown";
      cores = 1;
      threads = 1;
      mhz = 0;
      id = 0;
    };
    void fill(char * data){
      int i;
      i = 0;
      if (sscanf(data, "model name : %n", &i) != EOF && i > 0){model = (data+i);}
      if (sscanf(data, "cpu cores : %d", &i) == 1){cores = i;}
      if (sscanf(data, "siblings : %d", &i) == 1){threads = i;}
      if (sscanf(data, "physical id : %d", &i) == 1){id = i;}
      if (sscanf(data, "cpu MHz : %d", &i) == 1){mhz = i;}
    };
};

void checkCapable(JSON::Value & capa){
  capa.null();
  std::ifstream cpuinfo("/proc/cpuinfo");
  if (cpuinfo){
    std::map<int, cpudata> cpus;
    char line[300];
    int proccount = -1;
    while (cpuinfo.good()){
      cpuinfo.getline(line, 300);
      if (cpuinfo.fail()){
        //empty lines? ignore them, clear flags, continue
        if (!cpuinfo.eof()){
          cpuinfo.ignore();
          cpuinfo.clear();
        }
        continue;
      }
      if (memcmp(line, "processor", 9) == 0){proccount++;}
      cpus[proccount].fill(line);
    }
    //fix wrong core counts
    std::map<int,int> corecounts;
    for (int i = 0; i <= proccount; ++i){
      corecounts[cpus[i].id]++;
    }
    //remove double physical IDs - we only want real CPUs.
    std::set<int> used_physids;
    int total_speed = 0;
    int total_threads = 0;
    for (int i = 0; i <= proccount; ++i){
      if (!used_physids.count(cpus[i].id)){
        used_physids.insert(cpus[i].id);
        JSON::Value thiscpu;
        thiscpu["model"] = cpus[i].model;
        thiscpu["cores"] = cpus[i].cores;
        if (cpus[i].cores < 2 && corecounts[cpus[i].id] > cpus[i].cores){
          thiscpu["cores"] = corecounts[cpus[i].id];
        }
        thiscpu["threads"] = cpus[i].threads;
        if (thiscpu["cores"].asInt() > thiscpu["threads"].asInt()){
          thiscpu["threads"] = thiscpu["cores"];
        }
        thiscpu["mhz"] = cpus[i].mhz;
        capa["cpu"].append(thiscpu);
        total_speed += cpus[i].cores * cpus[i].mhz;
        total_threads += cpus[i].threads;
      }
    }
    capa["speed"] = total_speed;
    capa["threads"] = total_threads;
  }
  std::ifstream meminfo("/proc/meminfo");
  if (meminfo){
    char line[300];
    int bufcache = 0;
    while (meminfo.good()){
      meminfo.getline(line, 300);
      if (meminfo.fail()){
        //empty lines? ignore them, clear flags, continue
        if (!meminfo.eof()){
          meminfo.ignore();
          meminfo.clear();
        }
        continue;
      }
      long long int i;
      if (sscanf(line, "MemTotal : %Li kB", &i) == 1){capa["mem"]["total"] = i/1024;}
      if (sscanf(line, "MemFree : %Li kB", &i) == 1){capa["mem"]["free"] = i/1024;}
      if (sscanf(line, "SwapTotal : %Li kB", &i) == 1){capa["mem"]["swaptotal"] = i/1024;}
      if (sscanf(line, "SwapFree : %Li kB", &i) == 1){capa["mem"]["swapfree"] = i/1024;}
      if (sscanf(line, "Buffers : %Li kB", &i) == 1){bufcache += i/1024;}
      if (sscanf(line, "Cached : %Li kB", &i) == 1){bufcache += i/1024;}
    }
    capa["mem"]["used"] = capa["mem"]["total"].asInt() - capa["mem"]["free"].asInt() - bufcache;
    capa["mem"]["cached"] = bufcache;
    capa["load"]["memory"] = ((capa["mem"]["used"].asInt() + (capa["mem"]["swaptotal"].asInt() - capa["mem"]["swapfree"].asInt())) * 100) / capa["mem"]["total"].asInt();
  }
  std::ifstream loadavg("/proc/loadavg");
  if (loadavg){
    char line[300];
    int bufcache = 0;
    loadavg.getline(line, 300);
    //parse lines here
    float onemin, fivemin, fifteenmin;
    if (sscanf(line, "%f %f %f", &onemin, &fivemin, &fifteenmin) == 3){
      capa["load"]["one"] = (long long int)(onemin * 100);
      capa["load"]["five"] = (long long int)(onemin * 100);
      capa["load"]["fifteen"] = (long long int)(onemin * 100);
    }
  }
}

void CheckAllStreams(JSON::Value & data){
  long long int currTime = Util::epoch();
  for (JSON::ObjIter jit = data.ObjBegin(); jit != data.ObjEnd(); jit++){
    if (!Util::Procs::isActive(jit->first)){
      startStream(jit->first, jit->second);
    }
    if (currTime - lastBuffer[jit->first] > 5){
      if (jit->second.isMember("error") && jit->second["error"].asString() != ""){
        jit->second["online"] = jit->second["error"];
      }else{
        jit->second["online"] = 0;
      }
    }else{
      jit->second["online"] = 1;
    }
  }
  static JSON::Value strlist;
  bool changed = false;
  if (strlist["config"] != Storage["config"]){
    strlist["config"] = Storage["config"];
    changed = true;
  }
  if (strlist["streams"] != Storage["streams"]){
    strlist["streams"] = Storage["streams"];
    changed = true;
  }
  if (changed){WriteFile("/tmp/mist/streamlist", strlist.toString());}
}

void CheckStreams(JSON::Value & in, JSON::Value & out){
  bool changed = false;
  for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
    if (out.isMember(jit->first)){
      if (!streamsEqual(jit->second, out[jit->first])){
        Log("STRM", std::string("Updated stream ")+jit->first);
        Util::Procs::Stop(jit->first);
        startStream(jit->first, jit->second);
      }
    }else{
      Log("STRM", std::string("New stream ")+jit->first);
      startStream(jit->first, jit->second);
    }
  }
  for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
    if (!in.isMember(jit->first)){
      Log("STRM", std::string("Deleted stream ")+jit->first);
      Util::Procs::Stop(jit->first);
    }
  }
  out = in;
}

}; //Connector namespace

int main(int argc, char ** argv){
  Controller::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port = JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP port to listen on.\"}");
  stored_port["default"] = Controller::Storage["config"]["controller"]["port"];
  if (!stored_port["default"]){stored_port["default"] = 4242;}
  JSON::Value stored_interface = JSON::fromString("{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Controller::Storage["config"]["controller"]["interface"];
  if (!stored_interface["default"]){stored_interface["default"] = "0.0.0.0";}
  JSON::Value stored_user = JSON::fromString("{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", \"help\":\"Username to drop privileges to, or root to not drop provileges.\"}");
  stored_user["default"] = Controller::Storage["config"]["controller"]["username"];
  if (!stored_user["default"]){stored_user["default"] = "root";}
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("listen_port", stored_port);
  conf.addOption("listen_interface", stored_interface);
  conf.addOption("username", stored_user);
  conf.addOption("daemonize", JSON::fromString("{\"long\":\"daemon\", \"short\":\"d\", \"default\":1, \"long_off\":\"nodaemon\", \"short_off\":\"n\", \"help\":\"Whether or not to daemonize the process after starting.\"}"));
  conf.addOption("account", JSON::fromString("{\"long\":\"account\", \"short\":\"a\", \"arg\":\"string\" \"default\":\"\", \"help\":\"A username:password string to create a new account with.\"}"));
  conf.addOption("uplink", JSON::fromString("{\"default\":0, \"help\":\"Enable MistSteward uplink.\", \"short\":\"U\", \"long\":\"uplink\"}"));
  conf.parseArgs(argc, argv);

  std::string account = conf.getString("account");
  if (account.size() > 0){
    size_t colon = account.find(':');
    if (colon != std::string::npos && colon != 0 && colon != account.size()){
      std::string uname = account.substr(0, colon);
      std::string pword = account.substr(colon + 1, std::string::npos);
      Controller::Log("CONF", "Created account "+uname+" through commandline option");
      Controller::Storage["account"][uname]["password"] = Controller::md5(pword);
    }
  }
  time_t lastuplink = 0;
  time_t processchecker = 0;
  Socket::Server API_Socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"), true);
  mkdir("/tmp/mist", S_IRWXU | S_IRWXG | S_IRWXO);//attempt to create /tmp/mist/ - ignore failures
  Socket::Server Stats_Socket = Socket::Server("/tmp/mist/statistics", true);
  conf.activate();
  Socket::Connection Incoming;
  std::vector< Controller::ConnectedUser > users;
  std::vector<Socket::Connection> buffers;
  JSON::Value Request;
  JSON::Value Response;
  std::string jsonp;
  Controller::ConnectedUser * uplink = 0;
  Controller::Log("CONF", "Controller started");
  conf.activate();
  while (API_Socket.connected() && conf.is_active){
    usleep(10000); //sleep for 10 ms - prevents 100% CPU time

    if (Util::epoch() - processchecker > 10){
      processchecker = Util::epoch();
      Controller::CheckProtocols(Controller::Storage["config"]["protocols"]);
      Controller::CheckAllStreams(Controller::Storage["streams"]);
      Controller::CheckStats(Controller::Storage["statistics"]);
    }
    if (conf.getBool("uplink") && Util::epoch() - lastuplink > UPLINK_INTERVAL){
      lastuplink = Util::epoch();
      bool gotUplink = false;
      if (users.size() > 0){
        for( std::vector< Controller::ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
          if (!it->C.connected()){
            it->C.close();
            users.erase(it);
            break;
          }
          if (it->clientMode){uplink = &*it; gotUplink = true;}
        }
      }
      if (!gotUplink){
        Incoming = Socket::Connection("gearbox.ddvtech.com", 4242, true);
        if (Incoming.connected()){
          users.push_back(Incoming);
          users.back().clientMode = true;
          uplink = &users.back();
          gotUplink = true;
        }
      }
      if (gotUplink){
        Response.null(); //make sure no data leaks from previous requests
        Response["config"] = Controller::Storage["config"];
        Response["streams"] = Controller::Storage["streams"];
        Response["log"] = Controller::Storage["log"];
        Response["statistics"] = Controller::Storage["statistics"];
        Response["now"] = (unsigned int)lastuplink;
        uplink->H.Clean();
        uplink->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
        uplink->H.BuildRequest();
        uplink->C.Send(uplink->H.BuildResponse("200", "OK"));
        uplink->H.Clean();
        //Controller::Log("UPLK", "Sending server data to uplink.");
      }else{
        Controller::Log("UPLK", "Could not connect to uplink.");
      }
    }

    Incoming = API_Socket.accept(true);
    if (Incoming.connected()){users.push_back(Incoming);}
    Incoming = Stats_Socket.accept(true);
    if (Incoming.connected()){buffers.push_back(Incoming);}
    if (buffers.size() > 0){
      for( std::vector< Socket::Connection >::iterator it = buffers.begin(); it != buffers.end(); it++) {
        if (!it->connected()){
          it->close();
          buffers.erase(it);
          break;
        }
        if (it->spool()){
          while (it->Received().size()){
            it->Received().get().resize(it->Received().get().size() - 1);
            Request = JSON::fromString(it->Received().get());
            it->Received().get().clear();
            if (Request.isMember("buffer")){
              std::string thisbuffer = Request["buffer"];
              Controller::lastBuffer[thisbuffer] = Util::epoch();
              //if metadata is available, store it
              if (Request.isMember("meta")){
                Controller::Storage["streams"][thisbuffer]["meta"] = Request["meta"];
              }
              if (Request.isMember("totals")){
                Controller::Storage["statistics"][thisbuffer]["curr"] = Request["curr"];
                std::string nowstr = Request["totals"]["now"].asString();
                Controller::Storage["statistics"][thisbuffer]["totals"][nowstr] = Request["totals"];
                Controller::Storage["statistics"][thisbuffer]["totals"][nowstr].removeMember("now");
                Controller::Storage["statistics"][thisbuffer]["totals"].shrink(600);//limit to 10 minutes of data
                for (JSON::ObjIter jit = Request["log"].ObjBegin(); jit != Request["log"].ObjEnd(); jit++){
                  Controller::Storage["statistics"][thisbuffer]["log"].append(jit->second);
                  Controller::Storage["statistics"][thisbuffer]["log"].shrink(1000);//limit to 1000 users per buffer
                }
              }
            }
            if (Request.isMember("vod")){
              std::string thisfile = Request["vod"]["filename"];
              for (JSON::ObjIter oit = Controller::Storage["streams"].ObjBegin(); oit != Controller::Storage["streams"].ObjEnd(); ++oit){
                if (oit->second["channel"]["URL"].asString() == thisfile){
                  Controller::lastBuffer[oit->first] = Util::epoch();
                  if (Request["vod"].isMember("meta")){
                    Controller::Storage["streams"][oit->first]["meta"] = Request["vod"]["meta"];
                  }
                  JSON::Value sockit = (long long int)it->getSocket();
                  std::string nowstr = Request["vod"]["now"].asString();
                  Controller::Storage["statistics"][oit->first]["curr"][sockit.asString()] = Request["vod"];
                  Controller::Storage["statistics"][oit->first]["curr"][sockit.asString()].removeMember("meta");
                  JSON::Value nowtotal;
                  for (JSON::ObjIter u_it = Controller::Storage["statistics"][oit->first]["curr"].ObjBegin(); u_it != Controller::Storage["statistics"][oit->first]["curr"].ObjEnd(); ++u_it){
                    nowtotal["up"] = nowtotal["up"].asInt() + u_it->second["up"].asInt();
                    nowtotal["down"] = nowtotal["down"].asInt() + u_it->second["down"].asInt();
                    nowtotal["count"] = nowtotal["count"].asInt() + 1;
                  }
                  Controller::Storage["statistics"][oit->first]["totals"][nowstr] = nowtotal;
                  Controller::Storage["statistics"][oit->first]["totals"].shrink(600);
                }
              }
            }
          }
        }
      }
    }
    if (users.size() > 0){
      for( std::vector< Controller::ConnectedUser >::iterator it = users.begin(); it != users.end(); it++) {
        if (!it->C.connected() || it->logins > 3){
          it->C.close();
          users.erase(it);
          break;
        }
        if (it->C.spool() || it->C.Received().size()){
          if (*(it->C.Received().get().rbegin()) != '\n'){
            std::string tmp = it->C.Received().get();
            it->C.Received().get().clear();
            if (it->C.Received().size()){
              it->C.Received().get().insert(0, tmp);
            }else{
              it->C.Received().append(tmp);
            }
            continue;
          }
          if (it->H.Read(it->C.Received().get())){
            Response.null(); //make sure no data leaks from previous requests
            if (it->clientMode){
              // In clientMode, requests are reversed. These are connections we initiated to GearBox.
              // They are assumed to be authorized, but authorization to gearbox is still done.
              // This authorization uses the compiled-in username and password (account).
              Request = JSON::fromString(it->H.body);
              if (Request["authorize"]["status"] != "OK"){
                if (Request["authorize"].isMember("challenge")){
                  it->logins++;
                  if (it->logins > 2){
                    Controller::Log("UPLK", "Max login attempts passed - dropping connection to uplink.");
                    it->C.close();
                  }else{
                    Response["config"] = Controller::Storage["config"];
                    Response["streams"] = Controller::Storage["streams"];
                    Response["log"] = Controller::Storage["log"];
                    Response["statistics"] = Controller::Storage["statistics"];
                    Response["authorize"]["username"] = COMPILED_USERNAME;
                    Controller::checkCapable(Response["capabilities"]);
                    Controller::Log("UPLK", "Responding to login challenge: " + Request["authorize"]["challenge"].asString());
                    Response["authorize"]["password"] = Controller::md5(COMPILED_PASSWORD + Request["authorize"]["challenge"].asString());
                    it->H.Clean();
                    it->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
                    it->H.BuildRequest();
                    it->C.Send(it->H.BuildResponse("200", "OK"));
                    it->H.Clean();
                    Controller::Log("UPLK", "Attempting login to uplink.");
                  }
                }
              }else{
                if (Request.isMember("config")){Controller::CheckConfig(Request["config"], Controller::Storage["config"]);}
                if (Request.isMember("streams")){Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);}
                if (Request.isMember("clearstatlogs")){
                  Controller::Storage["log"].null();
                  Controller::Storage["statistics"].null();
                }
              }
            }else{
              Request = JSON::fromString(it->H.GetVar("command"));
              if (!Request.isObject() && it->H.url != "/api"){
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/html");
                it->H.SetHeader("X-Info", "To force an API response, request the file /api");
                it->H.SetHeader("Server", "mistserver/" PACKAGE_VERSION "/" + Util::Config::libver);
                it->H.SetBody(std::string((char*)server_html, (size_t)server_html_len));
                it->C.Send(it->H.BuildResponse("200", "OK"));
                it->H.Clean();
              }else{
                Authorize(Request, Response, (*it));
                if (it->Authorized){
                  //Parse config and streams from the request.
                  if (Request.isMember("config")){Controller::CheckConfig(Request["config"], Controller::Storage["config"]);}
                  if (Request.isMember("streams")){Controller::CheckStreams(Request["streams"], Controller::Storage["streams"]);}
                  if (Request.isMember("save")){
                    Controller::WriteFile("config.json", Controller::Storage.toString());
                    Controller::Log("CONF", "Config written to file on request through API");
                  }
                  //sent current configuration, no matter if it was changed or not
                  //Response["streams"] = Storage["streams"];
                  Response["config"] = Controller::Storage["config"];
                  Controller::checkCapable(Response["capabilities"]);
                  Response["config"]["version"] = PACKAGE_VERSION "/" + Util::Config::libver;
                  Response["streams"] = Controller::Storage["streams"];
                  //add required data to the current unix time to the config, for syncing reasons
                  Response["config"]["time"] = Util::epoch();
                  if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
                  //sent any available logs and statistics
                  Response["log"] = Controller::Storage["log"];
                  Response["statistics"] = Controller::Storage["statistics"];
                  //clear log and statistics if requested
                  if (Request.isMember("clearstatlogs")){
                    Controller::Storage["log"].null();
                    Controller::Storage["statistics"].null();
                  }
                }
                jsonp = "";
                if (it->H.GetVar("callback") != ""){jsonp = it->H.GetVar("callback");}
                if (it->H.GetVar("jsonp") != ""){jsonp = it->H.GetVar("jsonp");}
                it->H.Clean();
                it->H.SetHeader("Content-Type", "text/javascript");
                if (jsonp == ""){
                  it->H.SetBody(Response.toString()+"\n\n");
                }else{
                  it->H.SetBody(jsonp+"("+Response.toString()+");\n\n");
                }
                it->C.Send(it->H.BuildResponse("200", "OK"));
                it->H.Clean();
              }
            }
          }
        }
      }
    }
  }
  API_Socket.close();
  Controller::Log("CONF", "Controller shutting down");
  Util::Procs::StopAll();
  Controller::WriteFile("config.json", Controller::Storage.toString());
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  return 0;
}
