/// \file procs.cpp
/// Contains generic functions for managing processes.

#include "procs.h"
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
#include <stdlib.h>
#include <stdio.h>

std::map<pid_t, std::string> Util::Procs::plist;
bool Util::Procs::handler_set = false;

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
    std::cerr << "Process " << pname << " part-terminated." << std::endl;
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
/// \return 0 if process was not started, sub (sending) process PID otherwise.
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

  int devnull = open("/dev/null", O_RDWR);
  pid_t ret = fork();
  if (ret == 0){
    close(pfildes[0]);
    dup2(pfildes[1],STDOUT_FILENO);
    close(pfildes[1]);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDERR_FILENO);
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
    dup2(pfildes[0],STDIN_FILENO);
    close(pfildes[0]);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
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

/// Starts three piped processes if the name is not already active.
/// \return 0 if process was not started, sub (sending) process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg cmd Commandline for sub (sending) process.
/// \arg cmd2 Commandline for sub (middle) process.
/// \arg cmd3 Commandline for main (receiving) process.
pid_t Util::Procs::Start(std::string name, std::string cmd, std::string cmd2, std::string cmd3){
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
  int pfildes2[2];
  if (pipe(pfildes) == -1){
    #if DEBUG >= 1
    std::cerr << "Process " << name << " could not be started. Pipe creation failed." << std::endl;
    #endif
    return 0;
  }
  if (pipe(pfildes2) == -1){
    #if DEBUG >= 1
    std::cerr << "Process " << name << " could not be started. Pipe creation failed." << std::endl;
    #endif
    return 0;
  }
  
  int devnull = open("/dev/null", O_RDWR);
  pid_t ret = fork();
  if (ret == 0){
    close(pfildes[0]);
    dup2(pfildes[1],STDOUT_FILENO);
    close(pfildes[1]);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDERR_FILENO);
    close(pfildes2[1]);
    close(pfildes2[0]);
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
      close(pfildes2[1]);
      close(pfildes2[0]);
      return 0;
    }
  }
  
  pid_t ret2 = fork();
  if (ret2 == 0){
    close(pfildes[1]);
    close(pfildes2[0]);
    dup2(pfildes[0],STDIN_FILENO);
    close(pfildes[0]);
    dup2(pfildes2[1],STDOUT_FILENO);
    close(pfildes2[1]);
    dup2(devnull, STDERR_FILENO);
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
      close(pfildes2[1]);
      close(pfildes2[0]);
      return 0;
    }
  }
  close(pfildes[1]);
  close(pfildes[0]);

  pid_t ret3 = fork();
  if (ret3 == 0){
    close(pfildes[1]);
    close(pfildes[0]);
    close(pfildes2[1]);
    dup2(pfildes2[0],STDIN_FILENO);
    close(pfildes2[0]);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    runCmd(cmd3);
  }else{
    if (ret3 > 0){
      #if DEBUG >= 1
      std::cerr << "Process " << name << " started, PIDs (" << ret << ", " << ret2 << ", " << ret3 << "): " << cmd << " | " << cmd2 << " | " << cmd3 << std::endl;
      #endif
      plist.insert(std::pair<pid_t, std::string>(ret3, name));
    }else{
      #if DEBUG >= 1
      std::cerr << "Process " << name << " could not be started. fork() failed." << std::endl;
      #endif
      Stop(name);
      close(pfildes[1]);
      close(pfildes[0]);
      close(pfildes2[1]);
      close(pfildes2[0]);
      return 0;
    }
  }
  
  return ret3;
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
