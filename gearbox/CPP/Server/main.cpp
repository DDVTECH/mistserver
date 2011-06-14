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


int MainHandler(DDV::Socket conn) {
  Gearbox_Server gbconn( conn );
  std::cout << "Starting Handshake\n";
  gbconn.Handshake( );
  while( conn.connected( ) && (conn.ready( ) != -1) ) {
    gbconn.HandleConnection( );
  }
  return 0;
}


// Load main server setup file, default port 7337, handler is MainHandler
#define DEFAULT_PORT 7337
#define MAINHANDLER MainHandler
#define CONFIGSECT GBTEST
#include "../../../util/server_setup.cpp"
