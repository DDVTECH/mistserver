/// \file proc.cpp
/// Contains generic functions for managing processes.

#include "proc.h"
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>
#if DEBUG >= 1
#include <iostream>
#endif

std::map<pid_t, std::string> Util::Procs::plist;
bool Util::Procs::handler_set = false;

/// Used internally to capture child signals and update plist.
void Util::Procs::childsig_handler(int signum){
  if (signum != SIGCHLD){return;}
  pid_t ret = wait(0);
  #if DEBUG >= 1
  std::cerr << "Process " << plist[ret] << " terminated." << std::endl;
  #endif
  plist.erase(ret);
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
    std::cerr << "Error: " << strerror(errno) << std::endl;
    #endif
    _exit(42);
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

/// Stops the named process, if running.
/// \arg name (Internal) name of process to stop
void Util::Procs::Stop(std::string name){
  if (!isActive(name)){return;}
  Stop(getPid(name));
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
