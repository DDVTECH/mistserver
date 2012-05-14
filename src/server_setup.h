/// \file server_setup.h
/// Contains generic functions for setting up a DDVTECH Connector.

#ifndef MAINHANDLER
  /// Handler that is called for accepted incoming connections.
  #define MAINHANDLER NoHandler
  #error "No handler was set!"
#endif


#ifndef DEFAULT_PORT
  /// Default port for this server.
  #define DEFAULT_PORT 0
  #error "No default port was set!"
#endif


#include "../lib/socket.h" //Socket library
#include "../lib/config.h" //utilities for config management
#include <signal.h>
#include <sys/types.h>
#include <pwd.h>
#include <fstream>
Socket::Server server_socket; ///< Placeholder for the server socket

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
    case SIGCHLD:
      wait(0);
      return;
      break;
    default: return; break;
  }
  if (!server_socket.connected()) return;
  server_socket.close();
}//signal_handler

/// Generic main entry point and loop for DDV Connectors.
/// This sets up the proper termination handler, checks commandline options,
/// parses config files and opens a listening socket on the requested port.
/// Any incoming connections will be accepted and start up the function #MAINHANDLER,
/// which should be defined before including server_setup.cpp.
/// The default port is set by define #DEFAULT_PORT.
/// The configuration file section is set by define #CONFIGSECT.
int main(int argc, char ** argv){
  Socket::Connection S;//placeholder for incoming connections

  //setup signal handler
  struct sigaction new_action;
  new_action.sa_handler = signal_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);
  sigaction(SIGCHLD, &new_action, NULL);

  //set and parse configuration
  Util::Config C;
  C.listen_port = DEFAULT_PORT;
  C.parseArgs(argc, argv);

  //setup a new server socket, for the correct interface and port
  server_socket = Socket::Server(C.listen_port, C.interface);
  if (!server_socket.connected()){
    #if DEBUG >= 1
    fprintf(stderr, "Error: could not make listening socket\n");
    #endif
    return 1;
  }else{
    #if DEBUG >= 3
    fprintf(stderr, "Made a listening socket on %s:%i...\n", C.interface.c_str(), C.listen_port);
    #endif
  }

  Util::setUser(C.username);
  if (C.daemon_mode){Util::Daemonize();}

  while (server_socket.connected()){
    S = server_socket.accept();
    if (S.connected()){//check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){//if new child, start MAINHANDLER
        return MAINHANDLER(S);
      }else{//otherwise, do nothing or output debugging text
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
        #endif
      }
    }
  }//while connected
  #if DEBUG >= 1
  fprintf(stderr, "Server socket closed, exiting.\n");
  #endif
  return 0;
}//main
