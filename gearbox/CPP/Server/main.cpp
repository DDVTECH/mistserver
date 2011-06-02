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
#include "gearbox_server.h"

int MainHandler(DDV::Socket conn) {
  Gearbox_Server ServerConnection;
  std::string CurCmd;
  while( conn.ready( ) != -1 ) {
    if( conn.ready( ) ) {
      conn.read( CurCmd );
      if( CurCmd.find('\n') != std::string::npos ) {
        conn.write( ServerConnection.ParseCommand( CurCmd.substr(0, CurCmd.find('\n') ) ) + "\n" );
        while( CurCmd[0] != '\n' ) { CurCmd.erase( CurCmd.begin() );}
        CurCmd.erase(CurCmd.begin());
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
