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
#include "../util/socket.h"
#include "../util/http_parser.h"
#include "../util/md5.h"
#include "../util/json.h"
#include "../util/procs.h"
#include "../util/config.h"
#include "../util/auth.h"

#define UPLINK_INTERVAL 30

Socket::Server API_Socket; ///< Main connection socket.
std::map<std::string, int> lastBuffer; ///< Last moment of contact with all buffers.
Auth keychecker; ///< Checks key authorization.

/// Basic signal handler. Disconnects the server_socket if it receives
/// a SIGINT, SIGHUP or SIGTERM signal, but does nothing for SIGPIPE.
/// Disconnecting the server_socket will terminate the main listening loop
/// and cleanly shut down the process.
void signal_handler (int signum){
  switch (signum){
    case SIGINT:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGINT - closing server socket.\n");
      #endif
      break;
    case SIGHUP:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGHUP - closing server socket.\n");
      #endif
      break;
    case SIGTERM:
      #if DEBUG >= 1
      fprintf(stderr, "Received SIGTERM - closing server socket.\n");
      #endif
      break;
    default: return; break;
  }
  API_Socket.close();
}//signal_handler



JSON::Value Storage; ///< Global storage of data.

void WriteFile( std::string Filename, std::string contents ) {
  std::ofstream File;
  File.open( Filename.c_str( ) );
  File << contents << std::endl;
  File.close( );
}

class ConnectedUser{
  public:
    std::string writebuffer;
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
  JSON::Value m;
  m.append((long long int)time(0));
  m.append(kind);
  m.append(message);
  Storage["log"].append(m);
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
      if( md5( (std::string)(Storage["account"][UserID]["password"]) + Challenge ) == (std::string)Request["authorize"]["password"] ) {
        Response["authorize"]["status"] = "OK";
        conn.Username = UserID;
        conn.Authorized = true;
        return;
      }
    }
    if (UserID != ""){
      Log("AUTH", "Failed login attempt "+UserID+" @ "+conn.C.getHost());
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
    tmp = "MistConn";
    tmp += (std::string)jit->second["connector"];
    tmp += " -n -p ";
    tmp += (std::string)jit->second["port"];
    if (jit->second.isMember("interface")){
      tmp += " -i ";
      tmp += (std::string)jit->second["interface"];
    }
    if (jit->second.isMember("username")){
      tmp += " -u ";
      tmp += (std::string)jit->second["username"];
    }
    counter = (long long int)counter + 1;
    new_connectors[std::string("Conn")+(std::string)counter] = tmp;
  }
  //collect array type
  for (JSON::ArrIter ait = p.ArrBegin(); ait != p.ArrEnd(); ait++){
    tmp = "MistConn";
    tmp += (std::string)(*ait)["connector"];
    tmp += " -n -p ";
    tmp += (std::string)(*ait)["port"];
    if ((*ait).isMember("interface")){
      tmp += " -i ";
      tmp += (std::string)(*ait)["interface"];
    }
    if ((*ait).isMember("username")){
      tmp += " -u ";
      tmp += (std::string)(*ait)["username"];
    }
    counter = (long long int)counter + 1;
    new_connectors[std::string("Conn")+(std::string)counter] = tmp;
  }

  //shut down deleted/changed connectors
  for (iter = current_connectors.begin(); iter != current_connectors.end(); iter++){
    if (new_connectors.count(iter->first) != 1 || new_connectors[iter->first] != iter->second){
      Util::Procs::Stop(iter->first);
    }
  }

  //start up new/changed connectors
  for (iter = new_connectors.begin(); iter != new_connectors.end(); iter++){
    if (current_connectors.count(iter->first) != 1 || current_connectors[iter->first] != iter->second || !Util::Procs::isActive(iter->first)){
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
        Log("CONF", std::string("Updated configuration value ")+jit->first);
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
  out["version"] = TOSTRING(VERSION);
}

bool streamsEqual(JSON::Value & one, JSON::Value & two){
  if (one["channel"]["URL"] != two["channel"]["URL"]){return false;}
  if (one["preset"]["cmd"] != two["preset"]["cmd"]){return false;}
  return true;
}

void startStream(std::string name, JSON::Value & data){
  Log("BUFF", "(re)starting stream buffer "+name);
  std::string URL = data["channel"]["URL"];
  std::string preset = data["preset"]["cmd"];
  std::string cmd1, cmd2;
  if (URL.substr(0, 4) == "push"){
    std::string pusher = URL.substr(7);
    cmd2 = "MistBuffer 500 "+name+" "+pusher;
    Util::Procs::Start(name, cmd2);
  }else{
    if (preset == ""){
      cmd1 = "cat "+URL;
    }else{
      cmd1 = "ffmpeg -re -async 2 -i "+URL+" "+preset+" -f flv -";
    }
    cmd2 = "MistBuffer 500 "+name;
    Util::Procs::Start(name, cmd1, cmd2);
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
      if ((long long int)jit->second["online"] != 0){changed = true;}
      jit->second["online"] = 0;
    }else{
      if ((long long int)jit->second["online"] != 1){changed = true;}
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

int main(int argc, char ** argv){
  //setup signal handler
  struct sigaction new_action;
  new_action.sa_handler = signal_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);

  Storage = JSON::fromFile("config.json");
  Util::Config C;
  C.listen_port = (long long int)Storage["config"]["controller"]["port"];
  if (C.listen_port < 1){C.listen_port = 4242;}
  C.interface = (std::string)Storage["config"]["controller"]["interface"];
  if (C.interface == ""){C.interface = "0.0.0.0";}
  C.username = (std::string)Storage["config"]["controller"]["username"];
  if (C.username == ""){C.username = "root";}
  C.parseArgs(argc, argv);
  time_t lastuplink = 0;
  time_t processchecker = 0;
  API_Socket = Socket::Server(C.listen_port, C.interface, true);
  mkdir("/tmp/mist", S_IRWXU | S_IRWXG | S_IRWXO);//attempt to create /tmp/mist/ - ignore failures
  Socket::Server Stats_Socket = Socket::Server("/tmp/mist/statistics", true);
  Util::setUser(C.username);
  if (C.daemon_mode){
    Util::Daemonize();
  }
  Socket::Connection Incoming;
  std::vector< ConnectedUser > users;
  std::vector<Socket::Connection> buffers;
  JSON::Value Request;
  JSON::Value Response;
  std::string jsonp;
  ConnectedUser * uplink = 0;
  while (API_Socket.connected()){
    usleep(100000); //sleep for 100 ms - prevents 100% CPU time

    if (time(0) - processchecker > 10){
      processchecker = time(0);
      CheckProtocols(Storage["config"]["protocols"]);
      CheckAllStreams(Storage["streams"]);
    }
    
    if (time(0) - lastuplink > UPLINK_INTERVAL){
      lastuplink = time(0);
      bool gotUplink = false;
      if (users.size() > 0){
        for( std::vector< ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
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
        Response["config"] = Storage["config"];
        Response["streams"] = Storage["streams"];
        Response["log"] = Storage["log"];
        Response["statistics"] = Storage["statistics"];
        Response["now"] = (unsigned int)lastuplink;
        uplink->H.Clean();
        uplink->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
        uplink->H.BuildRequest();
        uplink->writebuffer += uplink->H.BuildResponse("200", "OK");
        uplink->H.Clean();
        //Log("UPLK", "Sending server data to uplink.");
      }else{
        Log("UPLK", "Could not connect to uplink.");
      }
    }
    
    Incoming = API_Socket.accept();
    if (Incoming.connected()){users.push_back(Incoming);}
    Incoming = Stats_Socket.accept();
    if (Incoming.connected()){buffers.push_back(Incoming);}
    if (buffers.size() > 0){
      for( std::vector< Socket::Connection >::iterator it = buffers.begin(); it != buffers.end(); it++) {
        if (!it->connected()){
          it->close();
          buffers.erase(it);
          break;
        }
        it->spool();
        if (it->Received() != ""){
          size_t newlines = it->Received().find("\n\n");
          while (newlines != std::string::npos){
            Request = it->Received().substr(0, newlines);
            if (Request.isMember("totals") && Request["totals"].isMember("buffer")){
              std::string thisbuffer = Request["totals"]["buffer"];
              lastBuffer[thisbuffer] = time(0);
              Storage["statistics"][thisbuffer]["curr"] = Request["curr"];
              std::stringstream st;
              st << (long long int)Request["totals"]["now"];
              std::string nowstr = st.str();
              Storage["statistics"][thisbuffer]["totals"][nowstr] = Request["totals"];
              for (JSON::ObjIter jit = Request["log"].ObjBegin(); jit != Request["log"].ObjEnd(); jit++){
                Storage["statistics"][thisbuffer]["log"].append(jit->second);
              }
            }
            it->Received().erase(0, newlines+2);
            newlines = it->Received().find("\n\n");
          }
        }
      }
    }
    if (users.size() > 0){
      for( std::vector< ConnectedUser >::iterator it = users.begin(); it != users.end(); it++) {
        if (!it->C.connected() || it->logins > 3){
          it->C.close();
          users.erase(it);
          break;
        }
        if (it->writebuffer != ""){
          it->C.iwrite(it->writebuffer);
        }
        if (it->H.Read(it->C)){
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
                  Log("UPLK", "Max login attempts passed - dropping connection to uplink.");
                  it->C.close();
                }else{
                  Response["config"] = Storage["config"];
                  Response["streams"] = Storage["streams"];
                  Response["log"] = Storage["log"];
                  Response["statistics"] = Storage["statistics"];
                  Response["authorize"]["username"] = TOSTRING(COMPILED_USERNAME);
                  Log("UPLK", "Responding to login challenge: " + (std::string)Request["authorize"]["challenge"]);
                  Response["authorize"]["password"] = md5(TOSTRING(COMPILED_PASSWORD) + (std::string)Request["authorize"]["challenge"]);
                  it->H.Clean();
                  it->H.SetBody("command="+HTTP::Parser::urlencode(Response.toString()));
                  it->H.BuildRequest();
                  it->writebuffer += it->H.BuildResponse("200", "OK");
                  it->H.Clean();
                  Log("UPLK", "Attempting login to uplink.");
                }
              }
            }else{
              if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
              if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
              if (Request.isMember("clearstatlogs")){
                Storage["log"].null();
                Storage["statistics"].null();
              }
            }
          }else{
            Request = JSON::fromString(it->H.GetVar("command"));
            std::cout << "Request: " << Request.toString() << std::endl;
            Authorize(Request, Response, (*it));
            if (it->Authorized){
              //Parse config and streams from the request.
              if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
              if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
              //sent current configuration, no matter if it was changed or not
              //Response["streams"] = Storage["streams"];
              Response["config"] = Storage["config"];
              Response["streams"] = Storage["streams"];
              //add required data to the current unix time to the config, for syncing reasons
              Response["config"]["time"] = (long long int)time(0);
              if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
              //sent any available logs and statistics
              Response["log"] = Storage["log"];
              Response["statistics"] = Storage["statistics"];
              //clear log and statistics to prevent useless data transfer
              Storage["log"].null();
              Storage["statistics"].null();
            }
            jsonp = "";
            if (it->H.GetVar("callback") != ""){jsonp = it->H.GetVar("callback");}
            if (it->H.GetVar("jsonp") != ""){jsonp = it->H.GetVar("jsonp");}
            it->H.Clean();
            it->H.protocol = "HTTP/1.0";
            it->H.SetHeader("Content-Type", "text/javascript");
            if (jsonp == ""){
              it->H.SetBody(Response.toString()+"\n\n");
            }else{
              it->H.SetBody(jsonp+"("+Response.toString()+");\n\n");
            }
            it->writebuffer += it->H.BuildResponse("200", "OK");
            it->H.Clean();
          }
        }
      }
    }
  }
  Util::Procs::StopAll();
  WriteFile("config.json", Storage.toString());
  std::cout << "Killed all processes, wrote config to disk. Exiting." << std::endl;
  return 0;
}
