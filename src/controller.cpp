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
#include <ctime>
#include <cstdio>
#include <climits>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <set>
#include <sys/time.h>
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
#include "server.html.h"

#define UPLINK_INTERVAL 30
#define COMPILED_USERNAME ""
#define COMPILED_PASSWORD ""

namespace Connector{

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
  m.append((long long int)time(0));
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
  out["version"] = PACKAGE_VERSION;
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
        return;
      }
      cmd1 = "cat "+URL;
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
  unsigned int currTime = time(0);
  for (JSON::ObjIter jit = stats.ObjBegin(); jit != stats.ObjEnd(); jit++){
    if (currTime - lastBuffer[jit->first] > 120){
      stats.removeMember(jit->first);
      return;
    }else{
      if (jit->second.isMember("curr") && jit->second["curr"].size() > 0){
        long long int nowtime = time(0);
        for (JSON::ObjIter u_it = jit->second["curr"].ObjBegin(); u_it != jit->second["curr"].ObjEnd(); ++u_it){
          if (u_it->second.isMember("now") && u_it->second["now"].asInt() < nowtime - 3){
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

void CheckAllStreams(JSON::Value & data){
  unsigned int currTime = time(0);
  bool changed = false;
  for (JSON::ObjIter jit = data.ObjBegin(); jit != data.ObjEnd(); jit++){
    if (!Util::Procs::isActive(jit->first)){
      startStream(jit->first, jit->second);
    }
    if (currTime - lastBuffer[jit->first] > 5){
      if (jit->second["online"].asInt() != 0){changed = true;}
      jit->second["online"] = 0;
    }else{
      if (jit->second["online"].asInt() != 1){changed = true;}
      jit->second["online"] = 1;
    }
  }
  if (changed){
    WriteFile("/tmp/mist/streamlist", Storage.toString());
  }
}

void CheckStreams(JSON::Value & in, JSON::Value & out){
  bool changed = false;
  for (JSON::ObjIter jit = in.ObjBegin(); jit != in.ObjEnd(); jit++){
    if (out.isMember(jit->first)){
      if (!streamsEqual(jit->second, out[jit->first])){
        Log("STRM", std::string("Updated stream ")+jit->first);
        changed = true;
        Util::Procs::Stop(jit->first);
        startStream(jit->first, jit->second);
      }
    }else{
      Log("STRM", std::string("New stream ")+jit->first);
      changed = true;
      startStream(jit->first, jit->second);
    }
  }
  for (JSON::ObjIter jit = out.ObjBegin(); jit != out.ObjEnd(); jit++){
    if (!in.isMember(jit->first)){
      Log("STRM", std::string("Deleted stream ")+jit->first);
      changed = true;
      Util::Procs::Stop(jit->first);
    }
  }
  out = in;
  if (changed){
    WriteFile("/tmp/mist/streamlist", Storage.toString());
  }
}

}; //Connector namespace

int main(int argc, char ** argv){
  Connector::Storage = JSON::fromFile("config.json");
  JSON::Value stored_port = JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP port to listen on.\"}");
  stored_port["default"] = Connector::Storage["config"]["controller"]["port"];
  if (!stored_port["default"]){stored_port["default"] = 4242;}
  JSON::Value stored_interface = JSON::fromString("{\"long\":\"interface\", \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address to listen on, or 0.0.0.0 for all available interfaces.\"}");
  stored_interface["default"] = Connector::Storage["config"]["controller"]["interface"];
  if (!stored_interface["default"]){stored_interface["default"] = "0.0.0.0";}
  JSON::Value stored_user = JSON::fromString("{\"long\":\"username\", \"short\":\"u\", \"arg\":\"string\", \"help\":\"Username to drop privileges to, or root to not drop provileges.\"}");
  stored_user["default"] = Connector::Storage["config"]["controller"]["username"];
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
      Connector::Log("CONF", "Created account "+uname+" through commandline option");
      Connector::Storage["account"][uname]["password"] = Connector::md5(pword);
    }
  }
  time_t lastuplink = 0;
  time_t processchecker = 0;
  Socket::Server API_Socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"), true);
  mkdir("/tmp/mist", S_IRWXU | S_IRWXG | S_IRWXO);//attempt to create /tmp/mist/ - ignore failures
  Socket::Server Stats_Socket = Socket::Server("/tmp/mist/statistics", true);
  conf.activate();
  Socket::Connection Incoming;
  std::vector< Connector::ConnectedUser > users;
  std::vector<Socket::Connection> buffers;
  JSON::Value Request;
  JSON::Value Response;
  std::string jsonp;
  Connector::ConnectedUser * uplink = 0;
  Connector::Log("CONF", "Controller started");
  conf.activate();
  while (API_Socket.connected() && conf.is_active){
    usleep(100000); //sleep for 100 ms - prevents 100% CPU time

    if (time(0) - processchecker > 10){
      processchecker = time(0);
      Connector::CheckProtocols(Connector::Storage["config"]["protocols"]);
      Connector::CheckAllStreams(Connector::Storage["streams"]);
      Connector::CheckStats(Connector::Storage["statistics"]);
    }
    if (conf.getBool("uplink") && time(0) - lastuplink > UPLINK_INTERVAL){
      lastuplink = time(0);
      bool gotUplink = false;
      if (users.size() > 0){
        for( std::vector< Connector::ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
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
        Response["config"] = Connector::Storage["config"];
        Response["streams"] = Connector::Storage["streams"];
        Response["log"] = Connector::Storage["log"];
        Response["statistics"] = Connector::Storage["statistics"];
        Response["now"] = (unsigned int)lastuplink;
        uplink->H.Clean();
        uplink->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
        uplink->H.BuildRequest();
        uplink->C.Send(uplink->H.BuildResponse("200", "OK"));
        uplink->H.Clean();
        //Connector::Log("UPLK", "Sending server data to uplink.");
      }else{
        Connector::Log("UPLK", "Could not connect to uplink.");
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
          size_t newlines = it->Received().find("\n\n");
          while (newlines != std::string::npos){
            Request = JSON::fromString(it->Received().substr(0, newlines));
            if (Request.isMember("buffer")){
              std::string thisbuffer = Request["buffer"];
              Connector::lastBuffer[thisbuffer] = time(0);
              if (Request.isMember("meta")){
                Connector::Storage["statistics"][thisbuffer]["meta"] = Request["meta"];
              }
              if (Request.isMember("totals")){
                Connector::Storage["statistics"][thisbuffer]["curr"] = Request["curr"];
                std::string nowstr = Request["totals"]["now"].asString();
                Connector::Storage["statistics"][thisbuffer]["totals"][nowstr] = Request["totals"];
                Connector::Storage["statistics"][thisbuffer]["totals"][nowstr].removeMember("now");
                Connector::Storage["statistics"][thisbuffer]["totals"].shrink(600);//limit to 10 minutes of data
                //if metadata is available, store it
                for (JSON::ObjIter jit = Request["log"].ObjBegin(); jit != Request["log"].ObjEnd(); jit++){
                  Connector::Storage["statistics"][thisbuffer]["log"].append(jit->second);
                  Connector::Storage["statistics"][thisbuffer]["log"].shrink(1000);//limit to 1000 users per buffer
                }
              }
            }
            if (Request.isMember("vod")){
              std::string thisfile = Request["vod"]["filename"];
              for (JSON::ObjIter oit = Connector::Storage["streams"].ObjBegin(); oit != Connector::Storage["streams"].ObjEnd(); ++oit){
                if (oit->second["channel"]["URL"].asString() == thisfile){
                  Connector::lastBuffer[oit->first] = time(0);
                  if (Request["vod"].isMember("meta")){
                    Connector::Storage["statistics"][oit->first]["meta"] = Request["vod"]["meta"];
                  }
                  JSON::Value sockit = (long long int)it->getSocket();
                  std::string nowstr = Request["vod"]["now"].asString();
                  Connector::Storage["statistics"][oit->first]["curr"][sockit.asString()] = Request["vod"];
                  Connector::Storage["statistics"][oit->first]["curr"][sockit.asString()].removeMember("meta");
                  JSON::Value nowtotal;
                  for (JSON::ObjIter u_it = Connector::Storage["statistics"][oit->first]["curr"].ObjBegin(); u_it != Connector::Storage["statistics"][oit->first]["curr"].ObjEnd(); ++u_it){
                    nowtotal["up"] = nowtotal["up"].asInt() + u_it->second["up"].asInt();
                    nowtotal["down"] = nowtotal["down"].asInt() + u_it->second["down"].asInt();
                    nowtotal["count"] = nowtotal["count"].asInt() + 1;
                  }
                  Connector::Storage["statistics"][oit->first]["totals"][nowstr] = nowtotal;
                  Connector::Storage["statistics"][oit->first]["totals"].shrink(600);
                }
              }
            }
            it->Received().erase(0, newlines+2);
            newlines = it->Received().find("\n\n");
          }
        }
      }
    }
    if (users.size() > 0){
      for( std::vector< Connector::ConnectedUser >::iterator it = users.begin(); it != users.end(); it++) {
        if (!it->C.connected() || it->logins > 3){
          it->C.close();
          users.erase(it);
          break;
        }
        if (it->C.spool()){
          if (it->H.Read(it->C.Received())){
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
                    Connector::Log("UPLK", "Max login attempts passed - dropping connection to uplink.");
                    it->C.close();
                  }else{
                    Response["config"] = Connector::Storage["config"];
                    Response["streams"] = Connector::Storage["streams"];
                    Response["log"] = Connector::Storage["log"];
                    Response["statistics"] = Connector::Storage["statistics"];
                    Response["authorize"]["username"] = COMPILED_USERNAME;
                    Connector::Log("UPLK", "Responding to login challenge: " + Request["authorize"]["challenge"].asString());
                    Response["authorize"]["password"] = Connector::md5(COMPILED_PASSWORD + Request["authorize"]["challenge"].asString());
                    it->H.Clean();
                    it->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
                    it->H.BuildRequest();
                    it->C.Send(it->H.BuildResponse("200", "OK"));
                    it->H.Clean();
                    Connector::Log("UPLK", "Attempting login to uplink.");
                  }
                }
              }else{
                if (Request.isMember("config")){Connector::CheckConfig(Request["config"], Connector::Storage["config"]);}
                if (Request.isMember("streams")){Connector::CheckStreams(Request["streams"], Connector::Storage["streams"]);}
                if (Request.isMember("clearstatlogs")){
                  Connector::Storage["log"].null();
                  Connector::Storage["statistics"].null();
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
                  if (Request.isMember("config")){Connector::CheckConfig(Request["config"], Connector::Storage["config"]);}
                  if (Request.isMember("streams")){Connector::CheckStreams(Request["streams"], Connector::Storage["streams"]);}
                  if (Request.isMember("save")){
                    Connector::WriteFile("config.json", Connector::Storage.toString());
                    Connector::Log("CONF", "Config written to file on request through API");
                  }
                  //sent current configuration, no matter if it was changed or not
                  //Response["streams"] = Storage["streams"];
                  Response["config"] = Connector::Storage["config"];
                  Response["streams"] = Connector::Storage["streams"];
                  //add required data to the current unix time to the config, for syncing reasons
                  Response["config"]["time"] = (long long int)time(0);
                  if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
                  //sent any available logs and statistics
                  Response["log"] = Connector::Storage["log"];
                  Response["statistics"] = Connector::Storage["statistics"];
                  //clear log and statistics if requested
                  if (Request.isMember("clearstatlogs")){
                    Connector::Storage["log"].null();
                    Connector::Storage["statistics"].null();
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
  Connector::Log("CONF", "Controller shutting down");
  Util::Procs::StopAll();
  Connector::WriteFile("config.json", Connector::Storage.toString());
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  return 0;
}
