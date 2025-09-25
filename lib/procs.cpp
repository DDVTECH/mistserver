/// \file procs.cpp
/// Contains generic functions for managing processes.

#include "procs.h"

#include "defines.h"
#include "ev.h"
#include "stream.h"

#include <mutex>
#include <signal.h>
#include <spawn.h>
#include <sstream>
#include <string.h>
#include <sys/types.h>
#include <thread>

#ifdef HASSYSWAIT
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

#if defined(__APPLE__) || defined(__MACH__)
extern char **environ;
#endif

std::set<pid_t> plist;
bool handler_set = false;
bool thread_handler = false;
std::mutex plistMutex;
std::mutex reaperMutex;
std::thread *reaper_thread = 0;

/// How many seconds to wait when shutting down child processes. Defaults to 10
int Util::Procs::kill_timeout = 10;

/// List of sockets that need to be closed when spawning a child process.
std::set<int> Util::Procs::socketList;

/// Ignores everything. Separate thread handles waiting for children.
void childsig_handler(int signum){return;}

/// Attempts to reap child and returns current running status.
bool Util::Procs::childRunning(pid_t p) {
  int status;
  pid_t ret;
  do {
    ret = waitpid(p, &status, WNOHANG);
    if (ret == p) {
      std::lock_guard<std::mutex> guard(plistMutex);
      int exitcode = -1;
      if (WIFEXITED(status)) {
        exitcode = WEXITSTATUS(status);
      } else if (WIFSIGNALED(status)) {
        exitcode = -WTERMSIG(status);
      }
      if (plist.count(ret)) {
        HIGH_MSG("Process %d fully terminated with code %d", ret, exitcode);
        plist.erase(ret);
      } else {
        HIGH_MSG("Child process %d exited with code %d", ret, exitcode);
      }
      return false;
    }
  } while (ret < 0 && errno == EINTR);
  return !kill(p, 0);
}

/// This local-only function prepares a deque for getOutputOf and automatically inserts a NULL at the end of the char* const*
char *const *dequeToArgv(const std::deque<std::string> & argDeq) {
  char **ret = (char **)malloc((argDeq.size() + 1) * sizeof(char *));
  for (int i = 0; i < argDeq.size(); i++){ret[i] = (char *)argDeq[i].c_str();}
  ret[argDeq.size()] = NULL;
  return ret;
}

/// sends sig 0 to process (pid). returns true if process is running
bool Util::Procs::isRunning(pid_t pid){
  return !kill(pid, 0);
}

/// Called at exit of any program that used a Start* function.
/// Waits up to 1 second, then sends SIGINT signal to all managed processes.
/// After that waits up to 5 seconds for children to exit, then sends SIGKILL to
/// all remaining children. Waits one more second for cleanup to finish, then exits.
void exit_handler() {
  if (!handler_set) { return; }
  thread_handler = false;
  if (reaper_thread) {
    // Send a child signal, to guarantee the thread wakes up immediately
    pthread_kill(reaper_thread->native_handle(), SIGCHLD);
    reaper_thread->join();
    delete reaper_thread;
    reaper_thread = 0;
  }
  if (plist.empty()) { return; }

  Event::Loop evLp;
  evLp.setup();

  uint64_t waitStart = Util::bootMS();

  // wait up to 0.5 second for applications to shut down
  uint64_t now = Util::bootMS();
  while (!plist.empty() && waitStart + 500 > now) {
    const std::set<pid_t> listcopy = plist;
    for (pid_t P : listcopy) {
      if (!Util::Procs::childRunning(P)) { plist.erase(P); }
    }
    if (!plist.empty() && waitStart + 500 > now) { evLp.await(waitStart + 500 - now); }
    now = Util::bootMS();
  }
  if (plist.empty()) { return; }

  INFO_MSG("Waiting up to %d seconds for %zu processes...", Util::Procs::kill_timeout, plist.size());
  // wait up to 10 seconds for applications to shut down

  uint64_t nextSignal = 0;
  while (!plist.empty() && waitStart + 1000 * Util::Procs::kill_timeout > now) {
    const std::set<pid_t> listcopy = plist;
    for (pid_t P : listcopy) {
      if (!Util::Procs::childRunning(P)) {
        plist.erase(P);
      } else {
        if (nextSignal <= now) {
          INFO_MSG("SIGINT %d", P);
          kill(P, SIGINT);
        }
      }
    }
    if (nextSignal <= now) { nextSignal = now + 1000; }
    if (!plist.empty() && waitStart + 1000 * Util::Procs::kill_timeout > now) {
      uint64_t waitTime = waitStart + 1000 * Util::Procs::kill_timeout - now;
      if (waitTime > 1000) { waitTime = 1000; }
      evLp.await(waitTime);
    }
    now = Util::bootMS();
  }
  if (plist.empty()) { return; }

  ERROR_MSG("Sending SIGKILL to remaining %zu children", plist.size());
  // send sigkill to all remaining
  for (pid_t P : plist) {
    INFO_MSG("SIGKILL %d", P);
    kill(P, SIGKILL);
  }

  INFO_MSG("Last chance for %d children to terminate.", (int)plist.size());
  waitStart = now = Util::bootMS();
  while (!plist.empty() && waitStart + 500 > now) {
    const std::set<pid_t> listcopy = plist;
    for (pid_t P : listcopy) {
      if (!Util::Procs::childRunning(P)) { plist.erase(P); }
    }
    if (!plist.empty() && waitStart + 500 > now) { evLp.await(waitStart + 500 - now); }
    now = Util::bootMS();
  }
  if (plist.empty()) { return; }
  FAIL_MSG("Giving up with %zu processes still running", plist.size());
}

// Joins the reaper thread, if any, before a fork
void Util::Procs::fork_prepare(){
  std::lock_guard<std::mutex> guard(reaperMutex);
  if (handler_set){
    thread_handler = false;
    if (reaper_thread){
      // Send a child signal, to guarantee the thread wakes up immediately
      pthread_kill(reaper_thread->native_handle(), SIGCHLD);
      reaper_thread->join();
      delete reaper_thread;
      reaper_thread = 0;
    }
  }
}

/// Thread that loops until thread_handler is false.
/// Reaps available children and then sleeps for a second.
/// Not done in signal handler so we can use a mutex to prevent race conditions.
void grim_reaper(){
  Util::nameThread("grim_reaper");
  // Block most signals, so we don't catch them in this thread
  sigset_t x;
  sigemptyset(&x);
  sigaddset(&x, SIGUSR1);
  sigaddset(&x, SIGUSR2);
  sigaddset(&x, SIGHUP);
  sigaddset(&x, SIGINT);
  sigaddset(&x, SIGCONT);
  sigaddset(&x, SIGPIPE);
  pthread_sigmask(SIG_SETMASK, &x, 0);

  VERYHIGH_MSG("Grim reaper start");
  while (thread_handler){
    Util::Procs::reap();
    if (thread_handler){Util::sleep(1000);}
  }
  VERYHIGH_MSG("Grim reaper stop");
}

/// Restarts reaper thread if it was joined
void Util::Procs::fork_complete(){
  std::lock_guard<std::mutex> guard(reaperMutex);
  if (handler_set){

    struct sigaction old_action;
    sigaction(SIGCHLD, 0, &old_action);
    if (old_action.sa_handler == childsig_handler){
      thread_handler = true;
      reaper_thread = new std::thread(grim_reaper);
    }
  }
}

/// Sets up exit and childsig handlers.
/// Spawns grim_reaper. exit handler despawns grim_reaper
/// Called by every Start* function.
void Util::Procs::setHandler(){
  std::lock_guard<std::mutex> guard(reaperMutex);
  if (!handler_set){

    struct sigaction old_action;
    sigaction(SIGCHLD, 0, &old_action);
    if (old_action.sa_handler == SIG_DFL || old_action.sa_handler == SIG_IGN){
      MEDIUM_MSG("Setting child signal handler, since signals were default or ignored before");
      thread_handler = true;
      reaper_thread = new std::thread(grim_reaper);
      struct sigaction new_action;
      new_action.sa_handler = childsig_handler;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = 0;
      sigaction(SIGCHLD, &new_action, NULL);
      atexit(exit_handler);
    }else{
      VERYHIGH_MSG("Not setting child signal handler; already handled elsewhere");
    }
    handler_set = true;
  }
}

/// Waits on all child processes, cleaning up internal structures as needed, then exits
void Util::Procs::reap(){
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
    {
      std::lock_guard<std::mutex> guard(plistMutex);
      if (plist.count(ret)){
        HIGH_MSG("Process %d fully terminated with code %d", ret, exitcode);
        plist.erase(ret);
      }else{
        HIGH_MSG("Child process %d exited with code %d", ret, exitcode);
      }
    }
  }
}

/// Runs the given command and returns the stdout output as a string.
/// \param maxWait amount of milliseconds to wait for new output to come in over stdout before aborting
std::string Util::Procs::getOutputOf(const char *const *argv, uint64_t maxWait) {
  int fout = -1;
  uint64_t deadline = Util::bootMS() + maxWait;
  pid_t myProc = StartPiped(argv, 0, &fout, 0);
  Socket::Connection O(-1, fout);
  O.setBlocking(false);
  Util::ResizeablePointer ret;
  Event::Loop evL;
  evL.addSocket(fout, [&ret, &O](void *){
    while (O.spool()){
      O.Received().remove(ret, O.Received().bytes(999999999));
    }
  }, 0);
  while (O){
    uint64_t currTime = Util::bootMS();
    if (currTime >= deadline){
      WARN_MSG("Process execution deadline passed: %" PRIu64 "ms", maxWait);
      Procs::Murder(myProc);
      O.close();
      break;
    }
    evL.await(deadline - currTime);
  }
  if (childRunning(myProc)){Procs::Murder(myProc);}
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

std::string Util::Procs::getLimitedOutputOf(const std::deque<std::string> & argDeq, uint64_t maxWait, uint32_t maxValBytes) {
  char *const *argv = dequeToArgv(argDeq); // Note: Do not edit deque before executing command
  return getLimitedOutputOf(argv, maxWait, maxValBytes);
}

std::string Util::Procs::getOutputOf(const std::deque<std::string> & argDeq, uint64_t maxWait) {
  char *const *argv = dequeToArgv(argDeq); // Note: Do not edit deque before executing command
  return getOutputOf(argv, maxWait);
}

pid_t Util::Procs::StartPiped(const std::deque<std::string> & argDeq, int *fdIn, int *fdOut, int *fdErr) {
  char *const *argv = dequeToArgv(argDeq); // Note: Do not edit deque before executing command
  return Util::Procs::StartPiped(argv, fdIn, fdOut, fdErr);
}

pid_t Util::Procs::StartPiped(const char *const *argv) {
  int fdIn = STDIN_FILENO, fdOut = STDOUT_FILENO, fdErr = STDERR_FILENO;
  return Util::Procs::StartPiped(argv, &fdIn, &fdOut, &fdErr);
}

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg argv Command for this process.
/// \arg fdIn Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg
/// contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be
/// used as fd. \arg fdOut Same as fdIn, but for stdout. \arg fdOut Same as fdIn, but for stderr.
pid_t Util::Procs::StartPiped(const char *const *argv, int *fdIn, int *fdOut, int *fdErr) {
  // NOTE: this function fails if you try and use the same values for all fds
  pid_t pid;
  int pipein[2], pipeout[2], pipeerr[2];
  setHandler();
  posix_spawn_file_actions_t childFdActions;
  posix_spawn_file_actions_init(&childFdActions);
  std::deque<int> fd_unused;

  {
    std::lock_guard<std::mutex> guard(plistMutex);
    for (auto it : Util::Procs::socketList) {
      errno = posix_spawn_file_actions_addclose(&childFdActions, it);
#if defined(_WIN32) || defined(CYGWIN)
      fcntl(it, F_SETFD, FD_CLOEXEC);
#endif
      if (errno) { INFO_MSG("errno closing socket %d: %s", it, strerror(errno)); }
    }
  }

  auto fdNotUnder3 = [&fd_unused](int & fd) {
    while (fd < 3) {
      fd_unused.push_back(fd);
      fd = dup(fd);
    }
  };

  if (fdIn && *fdIn == -1) {
    if (pipe(pipein) < 0) {
      ERROR_MSG("stdin pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
      return 0;
    }
    fdNotUnder3(pipein[0]);
    fdNotUnder3(pipein[1]);
  }

  if (fdOut && *fdOut == -1) {
    if (pipe(pipeout) < 0) {
      ERROR_MSG("stdout pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
      if (*fdIn == -1) {
        close(pipein[0]);
        close(pipein[1]);
      }
      return 0;
    }
    fdNotUnder3(pipeout[0]);
    fdNotUnder3(pipeout[1]);
  }

  if (fdErr && *fdErr == -1) {
    if (pipe(pipeerr) < 0) {
      ERROR_MSG("stderr pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
      if (*fdIn == -1) {
        close(pipein[0]);
        close(pipein[1]);
      }
      if (*fdOut == -1) {
        close(pipeout[0]);
        close(pipeout[1]);
      }
      return 0;
    }
    fdNotUnder3(pipeerr[0]);
    fdNotUnder3(pipeerr[1]);
  }

  while (fd_unused.size()) {
    close(fd_unused.back());
    fd_unused.pop_back();
  }

  std::set<int> fd_close;

  if (!fdIn) {
    posix_spawn_file_actions_addopen(&childFdActions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
  } else if (*fdIn == -1) {
    posix_spawn_file_actions_addclose(&childFdActions, pipein[1]);
    posix_spawn_file_actions_adddup2(&childFdActions, pipein[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&childFdActions, pipein[0]);
  } else if (*fdIn != STDIN_FILENO) {
    posix_spawn_file_actions_adddup2(&childFdActions, *fdIn, STDIN_FILENO);
    fd_close.insert(*fdIn);
  }

  if (!fdOut) {
    posix_spawn_file_actions_addopen(&childFdActions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
  } else if (*fdOut == -1) {
    posix_spawn_file_actions_addclose(&childFdActions, pipeout[0]);
    posix_spawn_file_actions_adddup2(&childFdActions, pipeout[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&childFdActions, pipeout[1]);
  } else if (*fdOut != STDOUT_FILENO) {
    posix_spawn_file_actions_adddup2(&childFdActions, *fdOut, STDOUT_FILENO);
    fd_close.insert(*fdOut);
  }

  if (!fdErr) {
    posix_spawn_file_actions_addopen(&childFdActions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
  } else if (*fdErr == -1) {
    posix_spawn_file_actions_addclose(&childFdActions, pipeerr[0]);
    posix_spawn_file_actions_adddup2(&childFdActions, pipeerr[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&childFdActions, pipeerr[1]);
  } else if (*fdErr != STDERR_FILENO) {
    posix_spawn_file_actions_adddup2(&childFdActions, *fdErr, STDERR_FILENO);
    fd_close.insert(*fdErr);
  }

  for (int fd : fd_close) { posix_spawn_file_actions_addclose(&childFdActions, fd); }
  int ret;
  // do{
  ret = posix_spawnp(&pid, argv[0], &childFdActions, NULL, (char *const *)argv, environ);
  // }while (ret && errno == EINTR);
  posix_spawn_file_actions_destroy(&childFdActions);

  std::stringstream args;
  for (size_t i = 0; i < 30; ++i) {
    if (!argv[i] || !argv[i][0]) { break; }
    args << argv[i] << " ";
  }

  if (ret) { FAIL_MSG("Could not start process %s: %s", args.str().c_str(), strerror(errno)); }

  if (fdIn && *fdIn == -1) {
    close(pipein[0]); // close unused read end
    if (ret) {
      close(pipein[1]);
    } else {
      *fdIn = pipein[1];
    }
  }
  if (fdOut && *fdOut == -1) {
    close(pipeout[1]); // close unused write end
    if (ret) {
      close(pipeout[0]);
    } else {
      *fdOut = pipeout[0];
    }
  }
  if (fdErr && *fdErr == -1) {
    close(pipeerr[1]); // close unused error end
    if (ret) {
      close(pipeerr[0]);
    } else {
      *fdErr = pipeerr[0];
    }
  }

  if (ret) { return 0; }

  {
    std::lock_guard<std::mutex> guard(plistMutex);
    plist.insert(pid);
  }

  HIGH_MSG("Piped process %s started, PID %d", args.str().c_str(), pid);
  return pid;
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to stop.
void Util::Procs::Stop(pid_t name){
  kill(name, SIGTERM);
}

void Util::Procs::hangup(pid_t name) {
  kill(name, SIGHUP);
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
    std::lock_guard<std::mutex> guard(plistMutex);
    listcopy = plist;
  }
  std::set<pid_t>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++){Stop(*it);}
}

/// Returns the number of active child processes.
int Util::Procs::Count(){
  std::lock_guard<std::mutex> guard(plistMutex);
  return plist.size();
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name){
  return (kill(name, 0) == 0);
}

/// Forget about the given PID, keeping it running on shutdown.
void Util::Procs::forget(pid_t pid){
  std::lock_guard<std::mutex> guard(plistMutex);
  plist.erase(pid);
}

/// Remember the given PID, killing it on shutdown.
void Util::Procs::remember(pid_t pid){
  std::lock_guard<std::mutex> guard(plistMutex);
  plist.insert(pid);
}
