/// \file server_setup.cpp
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


#ifndef CONFIGSECT
  /// Configuration file section for this server.
  #define CONFIGSECT None
  #error "No configuration file section was set!"
#endif

#include "socket.h" //Socket library
#include <signal.h>
#include <sys/types.h>
#include <pwd.h>
#include <fstream>
#define defstr(x) #x ///< converts a define name to string
#define defstrh(x) "[" defstr(x) "]" ///< converts define name to [string]
Socket::Server server_socket(-1); ///< Placeholder for the server socket

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
  
  //default values
  int listen_port = DEFAULT_PORT;
  bool daemon_mode = true;
  std::string interface = "0.0.0.0";
  std::string configfile = "/etc/ddvtech.conf";
  std::string username = "root";
  bool ignore_daemon = false;
  bool ignore_interface = false;
  bool ignore_port = false;
  bool ignore_user = false;
  
  int opt = 0;
  static const char *optString = "ndp:i:u:c:h?";
  static const struct option longOpts[] = {
    {"help",0,0,'h'},
    {"port",1,0,'p'},
    {"interface",1,0,'i'},
    {"username",1,0,'u'},
    {"no-daemon",0,0,'n'},
    {"daemon",0,0,'d'},
    {"configfile",1,0,'c'}
  };
  while ((opt = getopt_long(argc, argv, optString, longOpts, 0)) != -1){
    switch (opt){
      case 'p': listen_port = atoi(optarg); ignore_port = true; break;
      case 'i': interface = optarg; ignore_interface = true; break;
      case 'n': daemon_mode = false; ignore_daemon = true; break;
      case 'd': daemon_mode = true; ignore_daemon = true; break;
      case 'c': configfile = optarg; break;
      case 'u': username = optarg; ignore_user = true; break;
      case 'h':
      case '?':
        printf("Options: -h[elp], -?, -n[odaemon], -d[aemon], -p[ort] VAL, -i[nterface] VAL, -c[onfigfile] VAL, -u[sername] VAL\n");
        printf("Defaults:\n  interface: 0.0.0.0\n  port: %i\n  daemon mode: true\n  configfile: /etc/ddvtech.conf\n  username: root\n", listen_port);
        printf("Username root means no change to UID, no matter what the UID is.\n");
        printf("If the configfile exists, it is always loaded first. Commandline settings then overwrite the config file.\n");
        printf("\nThis process takes it directives from the %s section of the configfile.\n", defstrh(CONFIGSECT));
        return 1;
        break;
    }
  }//commandline options parser

  std::ifstream conf(configfile.c_str(), std::ifstream::in);
  std::string tmpstr;
  bool acc_comm = false;
  size_t foundeq;
  if (conf.fail()){
    #if DEBUG >= 3
    fprintf(stderr, "Configuration file %s not found - using build-in defaults...\n", configfile.c_str());
    #endif
  }else{
    while (conf.good()){
      getline(conf, tmpstr);
      if (tmpstr[0] == '['){//new section? check if we care.
        if (tmpstr == defstrh(CONFIGSECT)){acc_comm = true;}else{acc_comm = false;}
      }else{
        if (!acc_comm){break;}//skip all lines in this section if we do not care about it
        foundeq = tmpstr.find('=');
        if (foundeq != std::string::npos){
          if ((tmpstr.substr(0, foundeq) == "port") && !ignore_port){listen_port = atoi(tmpstr.substr(foundeq+1).c_str());}
          if ((tmpstr.substr(0, foundeq) == "interface") && !ignore_interface){interface = tmpstr.substr(foundeq+1);}
          if ((tmpstr.substr(0, foundeq) == "username") && !ignore_user){username = tmpstr.substr(foundeq+1);}
          if ((tmpstr.substr(0, foundeq) == "daemon") && !ignore_daemon){daemon_mode = true;}
          if ((tmpstr.substr(0, foundeq) == "nodaemon") && !ignore_daemon){daemon_mode = false;}
        }//found equals sign
      }//section contents
    }//configfile line loop
  }//configuration

  //setup a new server socket, for the correct interface and port
  server_socket = Socket::Server(listen_port, interface);
  #if DEBUG >= 3
  fprintf(stderr, "Made a listening socket on %s:%i...\n", interface.c_str(), listen_port);
  #endif
  if (server_socket.connected()){
    //if setup success, enter daemon mode if requested
    if (daemon_mode){
      daemon(1, 0);
      #if DEBUG >= 3
      fprintf(stderr, "Going into background mode...\n");
      #endif
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Error: could not make listening socket\n");
    #endif
    return 1;
  }

  if (username != "root"){
    struct passwd * user_info = getpwnam(username.c_str());
    if (!user_info){
      #if DEBUG >= 1
      fprintf(stderr, "Error: could not setuid %s: could not get PID\n", username.c_str());
      #endif
      return 1;
    }else{
      if (setuid(user_info->pw_uid) != 0){
        #if DEBUG >= 1
        fprintf(stderr, "Error: could not setuid %s: not allowed\n", username.c_str());
        #endif
      }else{
        #if DEBUG >= 3
        fprintf(stderr, "Changed user to %s\n", username.c_str());
        #endif
      }
    }
  }

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
