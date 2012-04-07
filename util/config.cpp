/// \file config.cpp
/// Contains generic functions for managing configuration.

#include "config.h"
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#ifdef __FreeBSD__
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include <errno.h>
#include <iostream>
#include <sys/types.h>
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
  configfile = "/etc/ddvtech.conf";
  username = "root";
  ignore_daemon = false;
  ignore_interface = false;
  ignore_port = false;
  ignore_user = false;
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
    {"configfile",1,0,'c'},
    {"version",0,0,'v'}
  };
  while ((opt = getopt_long(argc, argv, optString, longOpts, 0)) != -1){
    switch (opt){
      case 'p': listen_port = atoi(optarg); ignore_port = true; break;
      case 'i': interface = optarg; ignore_interface = true; break;
      case 'n': daemon_mode = false; ignore_daemon = true; break;
      case 'd': daemon_mode = true; ignore_daemon = true; break;
      case 'c': configfile = optarg; break;
      case 'u': username = optarg; ignore_user = true; break;
      case 'v':
        printf("%s\n", TOSTRING(VERSION));
        exit(1);
        break;
      case 'h':
      case '?':
        std::string doingdaemon = "true";
        if (!daemon_mode){doingdaemon = "false";}
        if (confsection == ""){
          printf("Options: -h[elp], -?, -v[ersion], -n[odaemon], -d[aemon], -p[ort] VAL, -i[nterface] VAL, -u[sername] VAL\n");
          printf("Defaults:\n  interface: %s\n  port: %i\n  daemon mode: %s\n  username: %s\n", interface.c_str(), listen_port, doingdaemon.c_str(), username.c_str());
        }else{
          printf("Options: -h[elp], -?, -v[ersion], -n[odaemon], -d[aemon], -p[ort] VAL, -i[nterface] VAL, -c[onfigfile] VAL, -u[sername] VAL\n");
          printf("Defaults:\n  interface: %s\n  port: %i\n  daemon mode: %s\n  configfile: %s\n  username: %s\n", interface.c_str(), listen_port, doingdaemon.c_str(), configfile.c_str(), username.c_str());
          printf("Username root means no change to UID, no matter what the UID is.\n");
          printf("If the configfile exists, it is always loaded first. Commandline settings then overwrite the config file.\n");
          printf("\nThis process takes it directives from the %s section of the configfile.\n", confsection.c_str());
        }
        printf("This is %s version %s\n", argv[0], TOSTRING(VERSION));
        exit(1);
        break;
    }
  }//commandline options parser
}

/// Parses the configuration file at configfile, if it exists.
/// Assumes confsection is set.
void Util::Config::parseFile(){
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
        if (tmpstr == confsection){acc_comm = true;}else{acc_comm = false;}
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
