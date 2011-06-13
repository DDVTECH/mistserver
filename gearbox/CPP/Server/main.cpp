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

std::string GetSingleCommand( DDV::Socket conn ) {
  static std::string CurCmd;
  std::string Result = "";
  if( conn.ready( ) ) {
    conn.read( CurCmd );
    if( CurCmd.find('\n') != std::string::npos ) {
      Result = CurCmd.substr(0, CurCmd.find('\n') );
      while( CurCmd[0] != '\n' ) { CurCmd.erase( CurCmd.begin( ) ); }
      CurCmd.erase( CurCmd.begin( ) );
    }
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
  while( CurCmd == "" ) { CurCmd = GetSingleCommand( conn ); }
  if( CurCmd.substr(0,3) != "OCC" ) { conn.write( "ERR\n" ); conn.close( ); exit(1); }
  conn.write( ServerConnection.ParseCommand( CurCmd ) + "\n" );
  if( !ServerConnection.IsConnected( ) ) { conn.close( ); exit(1); }
  while( conn.ready( ) != -1 ) {
    CurCmd = GetSingleCommand( conn );
    if( CurCmd != "" ) {
      conn.write( ServerConnection.ParseCommand( CurCmd ) + "\n" );
    }
  }
  return 0;
}


// Load main server setup file, default port 7337, handler is MainHandler
#define DEFAULT_PORT 7337
#define MAINHANDLER MainHandler
#define CONFIGSECT GBTEST
#include "../../../util/server_setup.cpp"
