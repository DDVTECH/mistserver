/// \file util.cpp
/// Contains generic functions for managing processes and configuration.

#include "util.h"
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
#include <pwd.h>
#include <getopt.h>
#include <stdlib.h>
#include <stdio.h>
#include <fstream>

std::map<pid_t, std::string> Util::Procs::plist;
bool Util::Procs::handler_set = false;

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

/// Used internally to capture child signals and update plist.
void Util::Procs::childsig_handler(int signum){
  if (signum != SIGCHLD){return;}
  pid_t ret = wait(0);
  #if DEBUG >= 1
  std::string pname = plist[ret];
  #endif
  plist.erase(ret);
  #if DEBUG >= 1
  if (isActive(pname)){
    std::cerr << "Process " << pname << " half-terminated." << std::endl;
    Stop(pname);
  }else{
    std::cerr << "Process " << pname << " fully terminated." << std::endl;
  }
  #endif
}

/// Attempts to run the command cmd.
/// Replaces the current process - use after forking first!
/// This function will never return - it will either run the given
/// command or kill itself with return code 42.
void Util::Procs::runCmd(std::string & cmd){
  //split cmd into arguments
  //supports a maximum of 20 arguments
  char * tmp = (char*)cmd.c_str();
  char * tmp2 = 0;
  char * args[21];
  int i = 0;
  tmp2 = strtok(tmp, " ");
  args[0] = tmp2;
  while (tmp2 != 0 && (i < 20)){
    tmp2 = strtok(0, " ");
    ++i;
    args[i] = tmp2;
  }
  if (i == 20){args[20] = 0;}
  //execute the command
  execvp(args[0], args);
  #if DEBUG >= 1
  std::cerr << "Error running \"" << cmd << "\": " << strerror(errno) << std::endl;
  #endif
  _exit(42);
}

/// Starts a new process if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg cmd Commandline for this process.
pid_t Util::Procs::Start(std::string name, std::string cmd){
  if (isActive(name)){return getPid(name);}
  if (!handler_set){
    struct sigaction new_action;
    new_action.sa_handler = Util::Procs::childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    handler_set = true;
  }
  pid_t ret = fork();
  if (ret == 0){
    runCmd(cmd);
  }else{
    if (ret > 0){
      #if DEBUG >= 1
      std::cerr << "Process " << name << " started, PID " << ret << ": " << cmd << std::endl;
      #endif
      plist.insert(std::pair<pid_t, std::string>(ret, name));
    }else{
      #if DEBUG >= 1
      std::cerr << "Process " << name << " could not be started. fork() failed." << std::endl;
      #endif
      return 0;
    }
  }
  return ret;
}

/// Starts two piped processes if the name is not already active.
/// \return 0 if process was not started, main (receiving) process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg cmd Commandline for sub (sending) process.
/// \arg cmd2 Commandline for main (receiving) process.
pid_t Util::Procs::Start(std::string name, std::string cmd, std::string cmd2){
  if (isActive(name)){return getPid(name);}
  if (!handler_set){
    struct sigaction new_action;
    new_action.sa_handler = Util::Procs::childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    handler_set = true;
  }
  int pfildes[2];
  if (pipe(pfildes) == -1){
    #if DEBUG >= 1
    std::cerr << "Process " << name << " could not be started. Pipe creation failed." << std::endl;
    #endif
    return 0;
  }

  pid_t ret = fork();
  if (ret == 0){
    close(pfildes[0]);
    dup2(pfildes[1],1);
    close(pfildes[1]);
    runCmd(cmd);
  }else{
    if (ret > 0){
      plist.insert(std::pair<pid_t, std::string>(ret, name));
    }else{
      #if DEBUG >= 1
      std::cerr << "Process " << name << " could not be started. fork() failed." << std::endl;
      #endif
      close(pfildes[1]);
      close(pfildes[0]);
      return 0;
    }
  }
  
  pid_t ret2 = fork();
  if (ret2 == 0){
    close(pfildes[1]);
    dup2(pfildes[0],0);
    close(pfildes[0]);
    runCmd(cmd2);
  }else{
    if (ret2 > 0){
      #if DEBUG >= 1
        std::cerr << "Process " << name << " started, PIDs (" << ret << ", " << ret2 << "): " << cmd << " | " << cmd2 << std::endl;
      #endif
      plist.insert(std::pair<pid_t, std::string>(ret2, name));
    }else{
      #if DEBUG >= 1
      std::cerr << "Process " << name << " could not be started. fork() failed." << std::endl;
      #endif
      Stop(name);
      close(pfildes[1]);
      close(pfildes[0]);
      return 0;
    }
  }
  close(pfildes[1]);
  close(pfildes[0]);
  return ret;
}

/// Stops the named process, if running.
/// \arg name (Internal) name of process to stop
void Util::Procs::Stop(std::string name){
  int max = 5;
  while (isActive(name)){
    Stop(getPid(name));
    max--;
    if (max <= 0){return;}
  }
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to stop.
void Util::Procs::Stop(pid_t name){
  if (isActive(name)){
    kill(name, SIGTERM);
  }
}

/// (Attempts to) stop all running child processes.
void Util::Procs::StopAll(){
  std::map<pid_t, std::string>::iterator it;
  for (it = plist.begin(); it != plist.end(); it++){
    Stop((*it).first);
  }
}

/// Returns the number of active child processes.
int Util::Procs::Count(){
   return plist.size();
}

/// Returns true if a process by this name is currently active.
bool Util::Procs::isActive(std::string name){
  std::map<pid_t, std::string>::iterator it;
  for (it = plist.begin(); it != plist.end(); it++){
    if ((*it).second == name){return true;}
  }
  return false;
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name){
  return (plist.count(name) == 1);
}

/// Gets PID for this named process, if active.
/// \return NULL if not active, process PID otherwise.
pid_t Util::Procs::getPid(std::string name){
  std::map<pid_t, std::string>::iterator it;
  for (it = plist.begin(); it != plist.end(); it++){
    if ((*it).second == name){return (*it).first;}
  }
  return 0;
}

/// Gets name for this process PID, if active.
/// \return Empty string if not active, name otherwise.
std::string Util::Procs::getName(pid_t name){
  if (plist.count(name) == 1){
    return plist[name];
  }
  return "";
}

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
/// Assumes confsection is set.
void Util::Config::parseArgs(int argc, char ** argv){
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
        printf("\nThis process takes it directives from the %s section of the configfile.\n", confsection.c_str());
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

/// Will turn the current process into a daemon.
/// Works by calling daemon(1,0):
/// Does not change directory to root.
/// Does redirect output to /dev/null
void Util::Daemonize(){
  daemon(1, 0);
}
