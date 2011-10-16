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
#include <sys/types.h>
#include <sys/epoll.h>
#include "../util/socket.h"
#include "../util/http_parser.h"
#include "../util/md5.h"
#include "../util/json/json.h"


void WriteFile( std::string Filename, std::string contents ) {
  std::ofstream File;
  File.open( Filename.c_str( ) );
  File << contents << std::endl;
  File.close( );
}

std::string ReadFile( std::string Filename ) {
  std::string Result;
  std::ifstream File;
  File.open( Filename.c_str( ) );
  while( File.good( ) ) { Result += File.get( ); }
  File.close( );
  return Result;
}

class ConnectedUser{
  public:
    Socket::Connection C;
    HTTP::Parser H;
    bool Authorized;
    std::string Username;
    ConnectedUser(Socket::Connection c){
      C = c;
      H.Clean();
      Authorized = false;
    }
};

void Log(std::string kind, std::string message, Json::Value & Storage){
  Json::Value m;
  m.append((Json::Value::UInt)time(0));
  m.append(kind);
  m.append(message);
  Storage["log"].append(m);
  std::cout << "[" << kind << "] " << message << std::endl;
}

void Authorize( Json::Value & Request, Json::Value & Storage, Json::Value & Response, ConnectedUser & conn ) {
  time_t Time = time(0);
  tm * TimeInfo = localtime(&Time);
  std::stringstream Date;
  std::string retval;
  Date << TimeInfo->tm_mday << "-" << TimeInfo->tm_mon << "-" << TimeInfo->tm_year + 1900;
  std::string Challenge = md5( Date.str().c_str() + conn.C.getHost() );
  if( Request.isMember( "authorize" ) ) {
    std::string UserID = Request["authorize"]["username"].asString();
    if (Storage["account"].isMember(UserID)){
      if( md5( Storage["account"][UserID]["password"].asString() + Challenge ) == Request["authorize"]["password"].asString() ) {
        Response["authorize"]["status"] = "OK";
        conn.Username = UserID;
        conn.Authorized = true;
        return;
      }
    }
    Log("AUTH", "Failed login attempt "+UserID+" @ "+conn.C.getHost(), Storage);
  }
  conn.Username = "";
  conn.Authorized = false;
  Response["authorize"]["status"] = "CHALL";
  Response["authorize"]["challenge"] = Challenge;
  return;
}

void CheckConfig(Json::Value & in, Json::Value & out){
  Json::ValueIterator jit;
  if (in.isObject()){
    for (jit = in.begin(); jit != in.end(); jit++){
      if (out.isObject() && out.isMember(jit.memberName())){
        Log("CONF", std::string("Updated configuration value ")+jit.memberName(), out);
      }else{
        Log("CONF", std::string("New configuration value ")+jit.memberName(), out);
      }
    }
    if (out.isObject()){
      for (jit = out.begin(); jit != out.end(); jit++){
        if (!in.isMember(jit.memberName())){
          Log("CONF", std::string("Deleted configuration value ")+jit.memberName(), out);
        }
      }
    }
  }
  out = in;
}

void CheckStreams(Json::Value & in, Json::Value & out){
  Json::ValueIterator jit;
  if (in.isObject()){
    for (jit = in.begin(); jit != in.end(); jit++){
      if (out.isObject() && out.isMember(jit.key().asString())){
        Log("STRM", "Updated stream "+jit.key().asString(), out);
      }else{
        Log("STRM", "New stream "+jit.key().asString(), out);
      }
    }
    if (out.isObject()){
      for (jit = out.begin(); jit != out.end(); jit++){
        if (!in.isMember(jit.key().asString())){
          Log("STRM", "Deleted stream "+jit.key().asString(), out);
        }
      }
    }
  }
  out = in;
}

int main() {
  time_t lastuplink = 0;
  Socket::Server API_Socket = Socket::Server(4242, "0.0.0.0", true);
  Socket::Server Stats_Socket = Socket::Server("/tmp/ddv_statistics", true);
  Socket::Connection Incoming;
  std::vector< ConnectedUser > users;
  Json::Value Request = Json::Value(Json::objectValue);
  Json::Value Response = Json::Value(Json::objectValue);
  Json::Value Storage = Json::Value(Json::objectValue);
  Json::Reader JsonParse;
  std::string jsonp;
  JsonParse.parse(ReadFile("config.json"), Storage, false);
  Storage["config"] = Json::Value(Json::objectValue);
  Storage["account"]["gearbox"]["password"] = Json::Value("7e0f87b116377621a75a6440ac74dcf4");
  while (API_Socket.connected()){
    usleep(10000); //sleep for 10 ms - prevents 100% CPU time
    Incoming = API_Socket.accept();
    if (Incoming.connected()){users.push_back(Incoming);}
    if (users.size() > 0){
      for( std::vector< ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it--) {
        if (!it->C.connected()){
          it->C.close();
          users.erase(it);
        }
        if (it->H.Read(it->C)){
          Response.clear(); //make sure no data leaks from previous requests
          if (!JsonParse.parse(it->H.GetVar("command"), Request, false)){
            Log("HTTP", "Failed to parse JSON: "+it->H.GetVar("command"), Storage);
            Response["authorize"]["status"] = "INVALID";
          }else{
            std::cout << "Request: " << Request.toStyledString() << std::endl;
            Authorize(Request, Storage, Response, (*it));
            if (it->Authorized){
              //Parse config and streams from the request.
              if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
              //if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
              //sent current configuration, no matter if it was changed or not
              //Response["streams"] = Storage["streams"];
              Response["config"] = Storage["config"];
              //add required data to the current unix time to the config, for syncing reasons
              Response["config"]["time"] = (Json::Value::UInt)time(0);
              if (!Response["config"].isMember("serverid")){Response["config"]["serverid"] = "";}
              //sent any available logs and statistics
              Response["log"] = Storage["log"];
              Response["statistics"] = Storage["statistics"];
              //clear log and statistics to prevent useless data transfer
              Storage["log"].clear();
              Storage["statistics"].clear();
            }
          }
          jsonp = "";
          if (it->H.GetVar("callback") != ""){jsonp = it->H.GetVar("callback");}
          if (it->H.GetVar("jsonp") != ""){jsonp = it->H.GetVar("jsonp");}
          it->H.Clean();
          it->H.protocol = "HTTP/1.0";
          it->H.SetHeader("Content-Type", "text/javascript");
          if (jsonp == ""){
            it->H.SetBody(Response.toStyledString()+"\n\n");
          }else{
            it->H.SetBody(jsonp+"("+Response.toStyledString()+");\n\n");
          }
          it->C.write(it->H.BuildResponse("200", "OK"));
          it->H.Clean();
        }
      }
    }
  }
  return 0;
}
