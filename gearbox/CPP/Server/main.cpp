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
#include "../../../util/md5.h"

std::string GenerateRandomString( int charamount ) {
  std::string Result;
  for( int i = 0; i < charamount; i++ ) {
    Result += (char)((rand() % 93)+33);
  }
  return Result;
}

int MainHandler(DDV::Socket conn) {
  srand( time( NULL ) );
  Gearbox_Server ServerConnection;
  std::string CurCmd;
  std::string RandomConnect = GenerateRandomString( 8 );
  while( conn.ready( ) == -1 ) {}
  conn.write( "WELCOME" + RandomConnect + "\n");
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


// Load main server setup file, default port 7337, handler is MainHandler
#define DEFAULT_PORT 7337
#define MAINHANDLER MainHandler
#define CONFIGSECT GBTEST
#include "../../../util/server_setup.cpp"
