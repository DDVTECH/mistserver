/// \file procs.cpp
/// Contains generic functions for managing processes.

#include "defines.h"
#include "procs.h"
#include "stream.h"
#include <signal.h>
#include <string.h>
#include <sys/types.h>

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__)
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#include "timing.h"
#include "json.h"
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <dirent.h>

std::set<pid_t> Util::Procs::plist;
std::set<int> Util::Procs::socketList;
bool Util::Procs::handler_set = false;
bool Util::Procs::thread_handler = false;
tthread::mutex Util::Procs::plistMutex;
tthread::thread *Util::Procs::reaper_thread = 0;

/// How many seconds to wait when shutting down child processes. Defaults to 10
int Util::Procs::kill_timeout = 10;

/// Local-only function. Attempts to reap child and returns current running status.
bool Util::Procs::childRunning(pid_t p){
  int status;
  pid_t ret = waitpid(p, &status, WNOHANG);
  if (ret == p){
    tthread::lock_guard<tthread::mutex> guard(plistMutex);
    int exitcode = -1;
    if (WIFEXITED(status)){
      exitcode = WEXITSTATUS(status);
    }else if (WIFSIGNALED(status)){
      exitcode = -WTERMSIG(status);
    }
    if (plist.count(ret)){
      HIGH_MSG("Process %d fully terminated with code %d", ret, exitcode);
      plist.erase(ret);
    }else{
      HIGH_MSG("Child process %d exited with code %d", ret, exitcode);
    }
    return false;
  }
  if (ret < 0 && errno == EINTR){return childRunning(p);}
  return !kill(p, 0);
}

/// sends sig 0 to process (pid). returns true if process is running
bool Util::Procs::isRunning(pid_t pid){
  return !kill(pid, 0);
}

/// Called at exit of any program that used a Start* function.
/// Waits up to 1 second, then sends SIGINT signal to all managed processes.
/// After that waits up to 5 seconds for children to exit, then sends SIGKILL to
/// all remaining children. Waits one more second for cleanup to finish, then exits.
void Util::Procs::exit_handler(){
  if (!handler_set){return;}
  int waiting = 0;
  std::set<pid_t> listcopy;
  {
    tthread::lock_guard<tthread::mutex> guard(plistMutex);
    listcopy = plist;
    thread_handler = false;
  }
  if (reaper_thread){
    reaper_thread->join();
    delete reaper_thread;
    reaper_thread = 0;
  }
  std::set<pid_t>::iterator it;
  if (listcopy.empty()){return;}

  // wait up to 0.5 second for applications to shut down
  while (!listcopy.empty() && waiting <= 25){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (!childRunning(*it)){
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()){
        Util::wait(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()){return;}

  INFO_MSG("Sending SIGINT and waiting up to 10 seconds for %d children to terminate.",
           (int)listcopy.size());
  waiting = 0;
  // wait up to 10 seconds for applications to shut down
  while (!listcopy.empty() && waiting <= 50*Util::Procs::kill_timeout){
    bool doWait = true;
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (!childRunning(*it)){
        listcopy.erase(it);
        doWait = false;
        break;
      }
    }
    if (doWait && !listcopy.empty()){
      if ((waiting % 50) == 0){
        for (it = listcopy.begin(); it != listcopy.end(); it++){
          INFO_MSG("SIGINT %d", *it);
          kill(*it, SIGINT);
        }
      }
      Util::wait(20);
      ++waiting;
    }
  }
  if (listcopy.empty()){return;}

  ERROR_MSG("Sending SIGKILL to remaining %d children", (int)listcopy.size());
  // send sigkill to all remaining
  if (!listcopy.empty()){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      INFO_MSG("SIGKILL %d", *it);
      kill(*it, SIGKILL);
    }
  }

  INFO_MSG("Waiting up to a second for %d children to terminate.", (int)listcopy.size());
  waiting = 0;
  // wait up to 1 second for applications to shut down
  while (!listcopy.empty() && waiting <= 50){
    for (it = listcopy.begin(); it != listcopy.end(); it++){
      if (!childRunning(*it)){
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()){
        Util::wait(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()){return;}
  FAIL_MSG("Giving up with %d children left.", (int)listcopy.size());
}

// Joins the reaper thread, if any, before a fork
void Util::Procs::fork_prepare(){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  if (handler_set){
    thread_handler = false;
    if (reaper_thread){
      reaper_thread->join();
      delete reaper_thread;
      reaper_thread = 0;
    }
  }
}

/// Restarts reaper thread if it was joined
void Util::Procs::fork_complete(){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  if (handler_set){
    thread_handler = true;
    reaper_thread = new tthread::thread(grim_reaper, 0);
  }
}

/// Sets up exit and childsig handlers.
/// Spawns grim_reaper. exit handler despawns grim_reaper
/// Called by every Start* function.
void Util::Procs::setHandler(){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  if (!handler_set){
    thread_handler = true;
    reaper_thread = new tthread::thread(grim_reaper, 0);
    struct sigaction new_action;
    new_action.sa_handler = childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    atexit(exit_handler);
    handler_set = true;
  }
}

/// Thread that loops until thread_handler is false.
/// Reaps available children and then sleeps for a second.
/// Not done in signal handler so we can use a mutex to prevent race conditions.
void Util::Procs::grim_reaper(void *n){
  VERYHIGH_MSG("Grim reaper start");
  while (thread_handler){
    {
      if (plistMutex.try_lock()){
        int status;
        pid_t ret = -1;
        while (ret != 0){
          ret = waitpid(-1, &status, WNOHANG);
          if (ret <= 0){// ignore, would block otherwise
            if (ret == 0 || errno != EINTR){break;}
            continue;
          }
          int exitcode;
          if (WIFEXITED(status)){
            exitcode = WEXITSTATUS(status);
          }else if (WIFSIGNALED(status)){
            exitcode = -WTERMSIG(status);
          }else{// not possible
            break;
          }
          if (plist.count(ret)){
            HIGH_MSG("Process %d fully terminated with code %d", ret, exitcode);
            plist.erase(ret);
          }else{
            HIGH_MSG("Child process %d exited with code %d", ret, exitcode);
          }
        }
        plistMutex.unlock();
      }
    }
    Util::sleep(500);
  }
  VERYHIGH_MSG("Grim reaper stop");
}

/// Ignores everything. Separate thread handles waiting for children.
void Util::Procs::childsig_handler(int signum){
  return;
}

/// Runs the given command and returns the stdout output as a string.
/// \param maxWait amount of milliseconds to wait for new output to come in over stdout before aborting
std::string Util::Procs::getOutputOf(char *const *argv, uint64_t maxWait){
  int fin = 0, fout = -1, ferr = 0;
  uint64_t waitedFor = 0;
  uint8_t tries = 0;
  pid_t myProc = StartPiped(argv, &fin, &fout, &ferr);
  Socket::Connection O(-1, fout);
  O.setBlocking(false);
  Util::ResizeablePointer ret;
  while (childRunning(myProc) || O){
    if (O.spool() || O.Received().size()){
      waitedFor = 0;
      tries = 0;
      while (O.Received().size()){
        std::string & t = O.Received().get();
        ret.append(t);
        t.clear();
      }
    }else{
      if (maxWait && waitedFor > maxWait){
        WARN_MSG("Timeout while getting output of '%s', returning %zuB of data",  (char *)argv, ret.size());
        break;
      }
      else if(maxWait){
        uint64_t waitTime = Util::expBackoffMs(tries++, 10, maxWait);
        Util::sleep(waitTime);
        waitedFor += waitTime;
      }
      else{
        Util::sleep(50);
      }
    }
  }
  return std::string(ret, ret.size());
}

/// Runs the given command and returns the stdout output as a string.
/// \param maxWait amount of milliseconds to wait before shutting down the spawned process
/// \param maxValBytes amount of Bytes allowed in the output before shutting down the spawned process
std::string Util::Procs::getLimitedOutputOf(char *const *argv, uint64_t maxWait, uint32_t maxValBytes){
  int fout = -1;
  uint64_t waitedFor = 0;
  uint8_t tries = 0;
  pid_t myProc = StartPiped(argv, NULL, &fout, NULL);
  Socket::Connection O(-1, fout);
  O.setBlocking(false);
  Util::ResizeablePointer ret;
  std::string fullCmd;
  uint8_t idx = 0;
  while (argv[idx]){
    fullCmd += argv[idx++];
    fullCmd += " ";
  }
  while (childRunning(myProc) || O){
    if (O.spool() || O.Received().size()){
      tries = 0;
      while (O.Received().size()){
        std::string & t = O.Received().get();
        ret.append(t);
        t.clear();
      }
    }else{
      if (waitedFor > maxWait){
        WARN_MSG("Reached timeout of %" PRIu64 " ms. Killing process with command %s...", maxWait, fullCmd.c_str());
        break;
      }
      else {
        uint64_t waitTime = Util::expBackoffMs(tries++, 10, maxWait);
        Util::sleep(waitTime);
        waitedFor += waitTime;
      }
    }
    if (ret.size() > maxValBytes){
      WARN_MSG("Have a limit of %" PRIu32 "B, but received %zuB of data. Killing process with command %s...",  maxValBytes, ret.size(), fullCmd.c_str());
      break;
    }
  }
  O.close();
  // Stop the process if it is still running
  if (childRunning(myProc)){
    Stop(myProc);
    waitedFor = 0;
  }
  // Give it a few seconds, but then forcefully stop it
  while (childRunning(myProc)){
    if (waitedFor > 2000){
      Murder(myProc);
      break;
    }else{
      waitedFor += 50;
      Util::sleep(50);
    }
  }
  return std::string(ret, ret.size());
}

/// This function prepares a deque for getOutputOf and automatically inserts a NULL at the end of the char* const*
char *const *Util::Procs::dequeToArgv(std::deque<std::string> &argDeq){
  char **ret = (char **)malloc((argDeq.size() + 1) * sizeof(char *));
  for (int i = 0; i < argDeq.size(); i++){ret[i] = (char *)argDeq[i].c_str();}
  ret[argDeq.size()] = NULL;
  return ret;
}

std::string Util::Procs::getOutputOf(std::deque<std::string> &argDeq, uint64_t maxWait){
  std::string ret;
  char *const *argv = dequeToArgv(argDeq); // Note: Do not edit deque before executing command
  ret = getOutputOf(argv, maxWait);
  return ret;
}

pid_t Util::Procs::StartPiped(std::deque<std::string> &argDeq, int *fdin, int *fdout, int *fderr){
  pid_t ret;
  char *const *argv = dequeToArgv(argDeq); // Note: Do not edit deque before executing command
  ret = Util::Procs::StartPiped(argv, fdin, fdout, fderr);
  return ret;
}

// Create a new deque with a rewritten first element to reflect the correct path
void mistifyDeque(std::deque<std::string> &argDeq) {
  argDeq[0] = Util::getMyPath() + argDeq[0];
}

// Start one of our Mist* processes, resolving our path automatically and such.
pid_t Util::Procs::StartPipedMist(std::deque<std::string> &argDeq, int *fdin, int *fdout, int *fderr){
  mistifyDeque(argDeq);
  return Util::Procs::StartPiped(argDeq, fdin, fdout, fderr);
}

// Exec to one of our Mist* processes, resolving our path automatically and such.
int Util::Procs::ExecMist(std::deque<std::string> &argDeq){
  mistifyDeque(argDeq);
  char *const *argv = dequeToArgv(argDeq);
  return execvp(argv[0], argv);
}

// Check whether a given Mist* binary is available for us to run
bool Util::Procs::HasMistBinary(std::string binName){
  std::string tmparg = Util::getMyPath() + binName;
  struct stat buf;
  return ::stat(tmparg.c_str(), &buf) == 0;
}

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg argv Command for this process.
/// \arg fdin Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg
/// contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be
/// used as fd. \arg fdout Same as fdin, but for stdout. \arg fdout Same as fdin, but for stderr.
pid_t Util::Procs::StartPiped(const char *const *argv, int *fdin, int *fdout, int *fderr){
  pid_t pid;
  int pipein[2], pipeout[2], pipeerr[2];
  setHandler();
  if (fdin && *fdin == -1 && pipe(pipein) < 0){
    ERROR_MSG("stdin pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    return 0;
  }
  if (fdout && *fdout == -1 && pipe(pipeout) < 0){
    ERROR_MSG("stdout pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    if (*fdin == -1){
      close(pipein[0]);
      close(pipein[1]);
    }
    return 0;
  }
  if (fderr && *fderr == -1 && pipe(pipeerr) < 0){
    ERROR_MSG("stderr pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    if (*fdin == -1){
      close(pipein[0]);
      close(pipein[1]);
    }
    if (*fdout == -1){
      close(pipeout[0]);
      close(pipeout[1]);
    }
    return 0;
  }
  int devnull = -1;
  if (!fdin || !fdout || !fderr){
    devnull = open("/dev/null", O_RDWR);
    if (devnull == -1){
      ERROR_MSG("Could not open /dev/null for process %s, reason: %s", argv[0], strerror(errno));
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
      return 0;
    }
  }
  pid = fork();
  if (pid == 0){// child
    int ch_stdin = 0, ch_stdout = 0, ch_stderr = 0;
    handler_set = false;
    if (!fdin){
      ch_stdin = dup(devnull);
    }else if (*fdin == -1){
      close(pipein[1]); // close unused write end
      ch_stdin = dup(pipein[0]);
      close(pipein[0]);
    }else{
      ch_stdin = dup(*fdin);
    }
    while (ch_stdin < 3){ch_stdin = dup(ch_stdin);}
    if (!fdout){
      ch_stdout = dup(devnull);
    }else if (*fdout == -1){
      close(pipeout[0]); // close unused read end
      ch_stdout = dup(pipeout[1]);
      close(pipeout[1]);
    }else{
      ch_stdout = dup(*fdout);
    }
    while (ch_stdout < 3){ch_stdout = dup(ch_stdout);}
    if (!fderr){
      ch_stderr = dup(devnull);
    }else if (*fderr == -1){
      close(pipeerr[0]); // close unused read end
      ch_stderr = dup(pipeerr[1]);
      close(pipeerr[1]);
    }else{
      ch_stderr = dup(*fderr);
    }
    while (ch_stderr < 3){ch_stderr = dup(ch_stderr);}
    if (fdin && *fdin != -1){close(*fdin);}
    if (fdout && *fdout != -1){close(*fdout);}
    if (fderr && *fderr != -1){close(*fderr);}
    if (devnull != -1){close(devnull);}
    // Close all sockets in the socketList
    for (std::set<int>::iterator it = Util::Procs::socketList.begin();
         it != Util::Procs::socketList.end(); ++it){
      close(*it);
    }
    //Black magic to make sure if 0/1/2 are not what we think they are, we end up with them not mixed up and weird.
    dup2(ch_stdin, 0);
    dup2(ch_stdout, 1);
    dup2(ch_stderr, 2);
    close(ch_stdout);
    close(ch_stdin);
    close(ch_stderr);
    //There! Now we normalized our stdio
    // Because execvp requires a char* const* and we have a const char* const*
    execvp(argv[0], (char *const *)argv);
    /*LTS-START*/
    char *trggr = getenv("MIST_TRIGGER");
    if (trggr && strlen(trggr)){
      ERROR_MSG("%s trigger failed to execute %s: %s", trggr, argv[0], strerror(errno));
      JSON::Value j;
      j["trigger_fail"] = trggr;
      Util::sendUDPApi(j);
      std::cout << getenv("MIST_TRIG_DEF");
      _exit(42);
    }
    /*LTS-END*/
    ERROR_MSG("execvp failed for process %s, reason: %s", argv[0], strerror(errno));
    _exit(42);
  }else if (pid == -1){
    ERROR_MSG("fork failed for process %s, reason: %s", argv[0], strerror(errno));
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
    if (devnull != -1){close(devnull);}
    return 0;
  }else{// parent
    {
      tthread::lock_guard<tthread::mutex> guard(plistMutex);
      plist.insert(pid);
    }
    HIGH_MSG("Piped process %s started, PID %d", argv[0], pid);
    if (devnull != -1){close(devnull);}
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
  }
  return pid;
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to stop.
void Util::Procs::Stop(pid_t name){
  kill(name, SIGTERM);
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to murder.
void Util::Procs::Murder(pid_t name){
  kill(name, SIGKILL);
}

/// (Attempts to) stop all running child processes.
void Util::Procs::StopAll(){
  std::set<pid_t> listcopy;
  {
    tthread::lock_guard<tthread::mutex> guard(plistMutex);
    listcopy = plist;
  }
  std::set<pid_t>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++){Stop(*it);}
}

/// Returns the number of active child processes.
int Util::Procs::Count(){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  return plist.size();
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  return (kill(name, 0) == 0);
}

/// Forget about the given PID, keeping it running on shutdown.
void Util::Procs::forget(pid_t pid){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  plist.erase(pid);
}

/// Remember the given PID, killing it on shutdown.
void Util::Procs::remember(pid_t pid){
  tthread::lock_guard<tthread::mutex> guard(plistMutex);
  plist.insert(pid);
}

/// Gets directory the current executable is stored in.
std::string Util::getMyPath(){
  char mypath[500];
#ifdef __CYGWIN__
  GetModuleFileName(0, mypath, 500);
#else
#ifdef __APPLE__
  memset(mypath, 0, 500);
  unsigned int refSize = 500;
  _NSGetExecutablePath(mypath, &refSize);
#else
  int ret = readlink("/proc/self/exe", mypath, 500);
  if (ret != -1){
    mypath[ret] = 0;
  }else{
    mypath[0] = 0;
  }
#endif
#endif
  std::string tPath = mypath;
  size_t slash = tPath.rfind('/');
  if (slash == std::string::npos){
    slash = tPath.rfind('\\');
    if (slash == std::string::npos){return "";}
  }
  tPath.resize(slash + 1);
  return tPath;
}

/// Gets all executables in getMyPath that start with "Mist".
void Util::getMyExec(std::deque<std::string> &execs){
  std::string path = Util::getMyPath();
#ifdef __CYGWIN__
  path += "\\Mist*";
  WIN32_FIND_DATA FindFileData;
  HANDLE hdl = FindFirstFile(path.c_str(), &FindFileData);
  while (hdl != INVALID_HANDLE_VALUE){
    if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
      execs.push_back(FindFileData.cFileName);
    }
    if (!FindNextFile(hdl, &FindFileData)){
      FindClose(hdl);
      hdl = INVALID_HANDLE_VALUE;
    }
  }
#else
  DIR *d = opendir(path.c_str());
  if (!d){return;}
  struct dirent *dp;
  do{
    errno = 0;
    if ((dp = readdir(d))){
      if (dp->d_type != DT_DIR && strncmp(dp->d_name, "Mist", 4) == 0){
        if (dp->d_type != DT_REG) {
          struct stat st = {};
          stat(dp->d_name, &st);
          if (!S_ISREG(st.st_mode))
            continue;
        }
        execs.push_back(dp->d_name);}
    }
  }while (dp != NULL);
  closedir(d);
#endif
}
