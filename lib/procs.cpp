/// \file procs.cpp
/// Contains generic functions for managing processes.

#include "procs.h"
#include "defines.h"
#include <string.h>
#include <sys/types.h>
#include <signal.h>

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__)
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
#include "timing.h"

std::map<pid_t, std::string> Util::Procs::plist;
std::map<pid_t, Util::TerminationNotifier> Util::Procs::exitHandlers;
bool Util::Procs::handler_set = false;

/// Called at exit of any program that used a Start* function.
/// Waits up to 1 second, then sends SIGINT signal to all managed processes.
/// After that waits up to 5 seconds for children to exit, then sends SIGKILL to
/// all remaining children. Waits one more second for cleanup to finish, then exits.
void Util::Procs::exit_handler(){
  int waiting = 0;
  std::map<pid_t, std::string> listcopy = plist;
  std::map<pid_t, std::string>::iterator it;

  //wait up to 1 second for applications to shut down 
  while ( !listcopy.empty() && waiting <= 50){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (kill(( *it).first, 0) == 0){
        Util::sleep(20);
        ++waiting;
      }else{
        listcopy.erase(it);
        break;
      }
    }
  }
  
  //send sigint to all remaining
  if ( !listcopy.empty()){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      kill(( *it).first, SIGINT);
    }
  }

  waiting = 0;
  //wait up to 5 seconds for applications to shut down 
  while ( !listcopy.empty() && waiting <= 50){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (kill(( *it).first, 0) == 0){
        Util::sleep(100);
        ++waiting;
      }else{
        listcopy.erase(it);
        break;
      }
    }
  }
  
  //send sigkill to all remaining
  if ( !plist.empty()){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      kill(( *it).first, SIGKILL);
    }
  }

  waiting = 0;
  //wait up to 1 second for applications to shut down 
  while ( !listcopy.empty() && waiting <= 50){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (kill(( *it).first, 0) == 0){
        Util::sleep(20);
        ++waiting;
      }else{
        listcopy.erase(it);
        break;
      }
    }
  }
  
}

/// Sets up exit and childsig handlers.
/// Called by every Start* function.
void Util::Procs::setHandler(){
  if ( !handler_set){
    struct sigaction new_action;
    new_action.sa_handler = childsig_handler;
    sigemptyset( &new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    atexit(exit_handler);
    handler_set = true;
  }
}

/// Used internally to capture child signals and update plist.
void Util::Procs::childsig_handler(int signum){
  if (signum != SIGCHLD){
    return;
  }
  int status;
  pid_t ret = -1;
  while (ret != 0){
    ret = waitpid( -1, &status, WNOHANG);
    if (ret <= 0){ //ignore, would block otherwise
      if (ret == 0 || errno != EINTR){
        return;
      }
    }
    int exitcode;
    if (WIFEXITED(status)){
      exitcode = WEXITSTATUS(status);
    }else if (WIFSIGNALED(status)){
      exitcode = -WTERMSIG(status);
    }else{/* not possible */
      return;
    }

#if DEBUG >= DLVL_DEVEL
    std::string pname = plist[ret];
#endif
    plist.erase(ret);
#if DEBUG >= DLVL_DEVEL
    if (!isActive(pname)){
      DEBUG_MSG(DLVL_DEVEL, "Process %s fully terminated", pname.c_str());
    }
#endif

    if (exitHandlers.count(ret) > 0){
      TerminationNotifier tn = exitHandlers[ret];
      exitHandlers.erase(ret);
      tn(ret, exitcode);
    }
  }
}


/// Runs the given command and returns the stdout output as a string.
std::string Util::Procs::getOutputOf(char* const* argv){
  std::string ret;
  int fin = 0, fout = -1, ferr = 0;
  StartPiped("output_getter", argv, &fin, &fout, &ferr);
  while (isActive("output_getter")){Util::sleep(100);}
  FILE * outFile = fdopen(fout, "r");
  char * fileBuf = 0;
  size_t fileBufLen = 0;
  while ( !(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)){
    ret += fileBuf;
  }
  fclose(outFile);
  return ret;
}

/// Runs the given command and returns the stdout output as a string.
std::string Util::Procs::getOutputOf(std::string cmd){
  std::string ret;
  int fin = 0, fout = -1, ferr = 0;
  StartPiped("output_getter", cmd, &fin, &fout, &ferr);
  while (isActive("output_getter")){Util::sleep(100);}
  FILE * outFile = fdopen(fout, "r");
  char * fileBuf = 0;
  size_t fileBufLen = 0;
  while ( !(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)){
    ret += fileBuf;
  }
  fclose(outFile);
  return ret;
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
  if (i == 20){
    args[20] = 0;
  }
  //execute the command
  execvp(args[0], args);
  DEBUG_MSG(DLVL_ERROR, "Error running %s: %s", cmd.c_str(), strerror(errno));
  _exit(42);
}

/// Starts a new process if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg cmd Commandline for this process.
pid_t Util::Procs::Start(std::string name, std::string cmd){
  if (isActive(name)){
    return getPid(name);
  }
  setHandler();
  pid_t ret = fork();
  if (ret == 0){
    runCmd(cmd);
  }else{
    if (ret > 0){
      DEBUG_MSG(DLVL_DEVEL, "Process %s started, PID %d: %s", name.c_str(), ret, cmd.c_str());
      plist.insert(std::pair<pid_t, std::string>(ret, name));
    }else{
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started: fork() failed", name.c_str());
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
  if (isActive(name)){
    return getPid(name);
  }
  setHandler();
  int pfildes[2];
  if (pipe(pfildes) == -1){
    DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. Pipe creation failed.", name.c_str());
    return 0;
  }

  int devnull = open("/dev/null", O_RDWR);
  pid_t ret = fork();
  if (ret == 0){
    close(pfildes[0]);
    dup2(pfildes[1], STDOUT_FILENO);
    close(pfildes[1]);
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDERR_FILENO);
    runCmd(cmd);
  }else{
    if (ret > 0){
      plist.insert(std::pair<pid_t, std::string>(ret, name));
    }else{
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. fork() failed.", name.c_str());
      close(pfildes[1]);
      close(pfildes[0]);
      return 0;
    }
  }

  pid_t ret2 = fork();
  if (ret2 == 0){
    close(pfildes[1]);
    dup2(pfildes[0], STDIN_FILENO);
    close(pfildes[0]);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    runCmd(cmd2);
  }else{
    if (ret2 > 0){
      DEBUG_MSG(DLVL_DEVEL, "Process %s started, PIDs (%d, %d): %s | %s", name.c_str(), ret, ret2, cmd.c_str(), cmd2.c_str());
      plist.insert(std::pair<pid_t, std::string>(ret2, name));
    }else{
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. fork() failed.", name.c_str());
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
  if (isActive(name)){
    return getPid(name);
  }
  setHandler();
  int pfildes[2];
  int pfildes2[2];
  if (pipe(pfildes) == -1){
    DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. Pipe creation failed.", name.c_str());
    return 0;
  }
  if (pipe(pfildes2) == -1){
    DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. Pipe creation failed.", name.c_str());
    return 0;
  }

  int devnull = open("/dev/null", O_RDWR);
  pid_t ret = fork();
  if (ret == 0){
    close(pfildes[0]);
    dup2(pfildes[1], STDOUT_FILENO);
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
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. fork() failed.", name.c_str());
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
    dup2(pfildes[0], STDIN_FILENO);
    close(pfildes[0]);
    dup2(pfildes2[1], STDOUT_FILENO);
    close(pfildes2[1]);
    dup2(devnull, STDERR_FILENO);
    runCmd(cmd2);
  }else{
    if (ret2 > 0){
      DEBUG_MSG(DLVL_DEVEL, "Process %s started, PIDs (%d, %d): %s | %s", name.c_str(), ret, ret2, cmd.c_str(), cmd2.c_str());
      plist.insert(std::pair<pid_t, std::string>(ret2, name));
    }else{
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. fork() failed.", name.c_str());
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
    dup2(pfildes2[0], STDIN_FILENO);
    close(pfildes2[0]);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    runCmd(cmd3);
  }else{
    if (ret3 > 0){
      DEBUG_MSG(DLVL_DEVEL, "Process %s started, PIDs (%d, %d, %d): %s | %s | %s", name.c_str(), ret, ret2, ret3, cmd.c_str(), cmd2.c_str(), cmd3.c_str());
      plist.insert(std::pair<pid_t, std::string>(ret3, name));
    }else{
      DEBUG_MSG(DLVL_ERROR, "Process %s could not be started. fork() failed.", name.c_str());
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

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg argv Command for this process.
/// \arg fdin Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be used as fd.
/// \arg fdout Same as fdin, but for stdout.
/// \arg fdout Same as fdin, but for stderr.
pid_t Util::Procs::StartPiped(std::string name, char* const* argv, int * fdin, int * fdout, int * fderr){
  if (isActive(name)){
    DEBUG_MSG(DLVL_WARN, "Process %s already active - skipping start", name.c_str());
    return getPid(name);
  }
  pid_t pid;
  int pipein[2], pipeout[2], pipeerr[2];
  setHandler();
  if (fdin && *fdin == -1 && pipe(pipein) < 0){
    DEBUG_MSG(DLVL_ERROR, "Pipe in creation failed for process %s", name.c_str());
    return 0;
  }
  if (fdout && *fdout == -1 && pipe(pipeout) < 0){
    DEBUG_MSG(DLVL_ERROR, "Pipe out creation failed for process %s", name.c_str());
    if ( *fdin == -1){
      close(pipein[0]);
      close(pipein[1]);
    }
    return 0;
  }
  if (fderr && *fderr == -1 && pipe(pipeerr) < 0){
    DEBUG_MSG(DLVL_ERROR, "Pipe err creation failed for process %s", name.c_str());
    if ( *fdin == -1){
      close(pipein[0]);
      close(pipein[1]);
    }
    if ( *fdout == -1){
      close(pipeout[0]);
      close(pipeout[1]);
    }
    return 0;
  }
  int devnull = -1;
  if ( !fdin || !fdout || !fderr){
    devnull = open("/dev/null", O_RDWR);
    if (devnull == -1){
      DEBUG_MSG(DLVL_ERROR, "Could not open /dev/null for process %s: %s", name.c_str(), strerror(errno));
      if ( *fdin == -1){
        close(pipein[0]);
        close(pipein[1]);
      }
      if ( *fdout == -1){
        close(pipeout[0]);
        close(pipeout[1]);
      }
      if ( *fderr == -1){
        close(pipeerr[0]);
        close(pipeerr[1]);
      }
      return 0;
    }
  }
  pid = fork();
  if (pid == 0){ //child
    if ( !fdin){
      dup2(devnull, STDIN_FILENO);
    }else if ( *fdin == -1){
      close(pipein[1]); // close unused write end
      dup2(pipein[0], STDIN_FILENO);
      close(pipein[0]);
    }else if ( *fdin != STDIN_FILENO){
      dup2( *fdin, STDIN_FILENO);
      close( *fdin);
    }
    if ( !fdout){
      dup2(devnull, STDOUT_FILENO);
    }else if ( *fdout == -1){
      close(pipeout[0]); // close unused read end
      dup2(pipeout[1], STDOUT_FILENO);
      close(pipeout[1]);
    }else if ( *fdout != STDOUT_FILENO){
      dup2( *fdout, STDOUT_FILENO);
      close( *fdout);
    }
    if ( !fderr){
      dup2(devnull, STDERR_FILENO);
    }else if ( *fderr == -1){
      close(pipeerr[0]); // close unused read end
      dup2(pipeerr[1], STDERR_FILENO);
      close(pipeerr[1]);
    }else if ( *fderr != STDERR_FILENO){
      dup2( *fderr, STDERR_FILENO);
      close( *fderr);
    }
    if (devnull != -1){
      close(devnull);
    }
    execvp(argv[0], argv);
    DEBUG_MSG(DLVL_ERROR, "execvp() failed for process %s", name.c_str());
    exit(42);
  }else if (pid == -1){
    DEBUG_MSG(DLVL_ERROR, "fork() for pipe failed for process %s", name.c_str());
    if (fdin && *fdin == -1){
      close(pipein[0]);
      close(pipein[1]);
    }
    if (fdout && *fdout == -1){
      close(pipeout[0]);
      close(pipeout[1]);
    }
    if (fderr && *fderr == -1){
      close(pipeerr[0]);
      close(pipeerr[1]);
    }
    if (devnull != -1){
      close(devnull);
    }
    return 0;
  }else{ //parent
    DEBUG_MSG(DLVL_DEVEL, "Piped process %s started, PID %d: %s", name.c_str(), pid, argv[0]);
    if (devnull != -1){
      close(devnull);
    }
    if (fdin && *fdin == -1){
      close(pipein[0]); // close unused end end
      *fdin = pipein[1];
    }
    if (fdout && *fdout == -1){
      close(pipeout[1]); // close unused write end
      *fdout = pipeout[0];
    }
    if (fderr && *fderr == -1){
      close(pipeerr[1]); // close unused write end
      *fderr = pipeerr[0];
    }
    plist.insert(std::pair<pid_t, std::string>(pid, name));
  }
  return pid;
}

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg name Name for this process - only used internally.
/// \arg cmd Command for this process.
/// \arg fdin Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be used as fd.
/// \arg fdout Same as fdin, but for stdout.
/// \arg fdout Same as fdin, but for stderr.
pid_t Util::Procs::StartPiped(std::string name, std::string cmd, int * fdin, int * fdout, int * fderr){
  //Convert the given command to a char * []
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
  if (i == 20){
    args[20] = 0;
  }
  return StartPiped(name,args,fdin,fdout,fderr);
}


pid_t Util::Procs::StartPiped2(std::string name, std::string cmd1, std::string cmd2, int * fdin, int * fdout, int * fderr1, int * fderr2){
  int pfildes[2];
  if (pipe(pfildes) == -1){
    DEBUG_MSG(DLVL_ERROR, "Pipe creation failed for process %s", name.c_str());
    return 0;
  }
  pid_t res1 = StartPiped(name, cmd1, fdin, &pfildes[1], fderr1);
  if ( !res1){
    close(pfildes[1]);
    close(pfildes[0]);
    return 0;
  }
  pid_t res2 = StartPiped(name+"receiving", cmd2, &pfildes[0], fdout, fderr2);
  if ( !res2){
    Stop(res1);
    close(pfildes[1]);
    close(pfildes[0]);
    return 0;
  }
  //we can close these because the fork in StartPiped() copies them.
  close(pfildes[1]);
  close(pfildes[0]);
  return res1;
}
/// Stops the named process, if running.
/// \arg name (Internal) name of process to stop
void Util::Procs::Stop(std::string name){
  int max = 5;
  while (isActive(name)){
    Stop(getPid(name));
    max--;
    if (max <= 0){
      return;
    }
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
  std::map<pid_t, std::string> listcopy = plist;
  std::map<pid_t, std::string>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++){
    Stop(( *it).first);
  }
}

/// Returns the number of active child processes.
int Util::Procs::Count(){
  return plist.size();
}

/// Returns true if a process by this name is currently active.
bool Util::Procs::isActive(std::string name){
  std::map<pid_t, std::string> listcopy = plist;
  std::map<pid_t, std::string>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++){
    if (( *it).second == name){
      if (kill(( *it).first, 0) == 0){
        return true;
      }else{
        plist.erase(( *it).first);
      }
    }
  }
  return false;
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name){
  return (plist.count(name) == 1) && (kill(name, 0) == 0);
}

/// Gets PID for this named process, if active.
/// \return NULL if not active, process PID otherwise.
pid_t Util::Procs::getPid(std::string name){
  std::map<pid_t, std::string>::iterator it;
  for (it = plist.begin(); it != plist.end(); it++){
    if (( *it).second == name){
      return ( *it).first;
    }
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

/// Registers one notifier function for when a process indentified by PID terminates.
/// \return true if the notifier could be registered, false otherwise.
bool Util::Procs::SetTerminationNotifier(pid_t pid, TerminationNotifier notifier){
  if (plist.find(pid) != plist.end()){
    exitHandlers[pid] = notifier;
    return true;
  }
  return false;
}
