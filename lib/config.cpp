/// \file config.cpp
/// Contains generic functions for managing configuration.

#include "config.h"
#include <string.h>
#include <signal.h>

#ifdef __FreeBSD__
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <errno.h>
#include <iostream>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>

/// Creates a new configuration manager.
Util::Config::Config(){
  listen_port = 4242;
  daemon_mode = true;
  interface = "0.0.0.0";
  username = "root";
}

/// Parses commandline arguments.
/// Calls exit if an unknown option is encountered, printing a help message.
/// confsection must be either already set or never be set at all when this function is called.
/// In other words: do not change confsection after calling this function.
void Util::Config::parseArgs(int argc, char ** argv){
  int opt = 0;
  static const char *optString = "ndvp:i:u:c:h?";
  static const struct option longOpts[] = {
    {"help",0,0,'h'},
    {"port",1,0,'p'},
    {"interface",1,0,'i'},
    {"username",1,0,'u'},
    {"no-daemon",0,0,'n'},
    {"daemon",0,0,'d'},
    {"version",0,0,'v'}
  };
  while ((opt = getopt_long(argc, argv, optString, longOpts, 0)) != -1){
    switch (opt){
      case 'p': listen_port = atoi(optarg); break;
      case 'i': interface = optarg; break;
      case 'n': daemon_mode = false; break;
      case 'd': daemon_mode = true; break;
      case 'u': username = optarg; break;
      case 'v':
        printf("%s\n", TOSTRING(PACKAGE_VERSION));
        exit(1);
        break;
      case 'h':
      case '?':
        std::string doingdaemon = "true";
        if (!daemon_mode){doingdaemon = "false";}
        printf("Options: -h[elp], -?, -v[ersion], -n[odaemon], -d[aemon], -p[ort] VAL, -i[nterface] VAL, -u[sername] VAL\n");
        printf("Defaults:\n  interface: %s\n  port: %i\n  daemon mode: %s\n  username: %s\n", interface.c_str(), listen_port, doingdaemon.c_str(), username.c_str());
        printf("Username root means no change to UID, no matter what the UID is.\n");
        printf("This is %s version %s\n", argv[0], TOSTRING(PACKAGE_VERSION));
        exit(1);
        break;
    }
  }//commandline options parser
}

/// Sets the current process' running user
void Util::setUser(std::string username){
  if (username != "root"){
    struct passwd * user_info = getpwnam(username.c_str());
    if (!user_info){
      #if DEBUG >= 1
      fprintf(stderr, "Error: could not setuid %s: could not get PID\n", username.c_str());
      #endif
      return;
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
}

/// Will turn the current process into a daemon.
/// Works by calling daemon(1,0):
/// Does not change directory to root.
/// Does redirect output to /dev/null
void Util::Daemonize(){
  #if DEBUG >= 3
  fprintf(stderr, "Going into background mode...\n");
  #endif
  daemon(1, 0);
}
