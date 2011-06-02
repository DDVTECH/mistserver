#include <iostream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/epoll.h>
#include <getopt.h>
#include <ctime>
#include <string>
#include <map>
#include "../../../util/ddv_socket.h"

enum Commands{
  CM_ERR,
  CM_OCC,
  CM_OCD
};

static std::map<std::string,Commands> CommandMap;

void InitializeMap( ) {
  CommandMap["OCC"] = CM_OCC;
  CommandMap["OCD"] = CM_OCD;
}

std::string ParseCommand( std::string Input ) {
  std::string Result;
  switch( CommandMap[Input.substr(0,3).c_str()] ) {
    case CM_OCC:
      Result = "OK";
      break;
    case CM_OCD:
      Result = "OK";
      break;
    default:
      Result = "ER_InvalidCommand";
      break;
  }
  return Result;
}

int MainHandler(DDV::Socket conn) {
  InitializeMap();
  std::string CurrentCommand;
  while( conn.ready( ) != -1 ) {
    if( conn.ready( ) ) {
      conn.read( CurrentCommand );
      if( CurrentCommand.find("\n") != std::string::npos ) {
        conn.write( ParseCommand( CurrentCommand ) + "\n" );
        while( CurrentCommand[0] != '\n' ) { CurrentCommand.erase( CurrentCommand.begin() );}
        CurrentCommand.erase(CurrentCommand.begin());
      }
    }
  }
  return 0;
}


// Load main server setup file, default port 8080, handler is Connector_HTTP::Connector_HTTP
#define DEFAULT_PORT 7337
#define MAINHANDLER MainHandler
#define CONFIGSECT GBTEST
#include "../../../util/server_setup.cpp"
