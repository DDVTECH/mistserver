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
  
}

void CheckStreams(Json::Value & in, Json::Value & out){
  
}

int main() {
  Socket::Server API_Socket = Socket::Server( 4242, "0.0.0.0", true );
  Socket::Connection TempConn;
  std::vector< ConnectedUser > users;
  HTTP::Parser HTTP_R, HTTP_S;
  Json::Value Request;
  Json::Value Response;
  Json::Value Storage;
  Json::Reader JsonParse;
  JsonParse.parse(ReadFile("config.json"), Storage, false);
  Storage["account"]["gearbox"]["password"] = Json::Value("7e0f87b116377621a75a6440ac74dcf4");
  while (API_Socket.connected()){
    usleep(10000); //sleep for 10 ms - prevents 100% CPU time
    TempConn = API_Socket.accept();
    if (TempConn.connected()){users.push_back(TempConn);}
    if (users.size() > 0){
      for( std::vector< ConnectedUser >::iterator it = users.end() - 1; it >= users.begin(); it-- ) {
        if( !(*it).C.connected() ) {
          (*it).C.close();
          users.erase( it );
        }
        if ((*it).H.Read((*it).C)){
          Response.clear(); //make sure no data leaks from previous requests
          std::cout << "Body: " << HTTP_R.body << std::endl;
          std::cout << "Command: " << HTTP_R.GetVar("command") << std::endl;
          JsonParse.parse(HTTP_R.GetVar("command"), Request, false);
          std::cout << Request.toStyledString() << std::endl;
          Authorize(Request, Storage, Response, (*it));
          if ((*it).Authorized){
            //Parse config and streams from the request.
            if (Request.isMember("config")){CheckConfig(Request["config"], Storage["config"]);}
            if (Request.isMember("streams")){CheckStreams(Request["streams"], Storage["streams"]);}
            //sent current configuration, no matter if it was changed or not
            Response["streams"] = Storage["streams"];
            Response["config"] = Storage["config"];
            //add the current unix time to the config, for syncing reasons
            Response["config"]["time"] = (Json::Value::UInt)time(0);
            //sent any available logs and statistics
            Response["log"] = Storage["log"];
            Response["statistics"] = Storage["statistics"];
            //clear log and statistics to prevent useless data transfer
            Storage["log"].clear();
            Storage["statistics"].clear();
          }
          (*it).H.Clean();
          (*it).H.protocol = "HTTP/1.1";
          (*it).H.SetHeader( "Content-Type", "text/javascript" );
          (*it).H.SetBody( Response.toStyledString() + "\n\n" );
          (*it).C.write( (*it).H.BuildResponse( "200", "OK" ) );
          (*it).H.Clean();
        }
      }
    }
  }
  return 0;
}
