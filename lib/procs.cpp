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

std::set<pid_t> Util::Procs::plist;
bool Util::Procs::handler_set = false;



static bool childRunning(pid_t p) {
  pid_t ret = waitpid(p, 0, WNOHANG);
  if (ret == p) {
    return false;
  }
  if (ret < 0 && errno == EINTR) {
    return childRunning(p);
  }
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
void Util::Procs::exit_handler() {
  int waiting = 0;
  std::set<pid_t> listcopy = plist;
  std::set<pid_t>::iterator it;
  if (listcopy.empty()) {
    return;
  }

  //wait up to 0.5 second for applications to shut down
  while (!listcopy.empty() && waiting <= 25) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }

  DEBUG_MSG(DLVL_DEVEL, "Sending SIGINT to remaining %d children", (int)listcopy.size());
  //send sigint to all remaining
  if (!listcopy.empty()) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      DEBUG_MSG(DLVL_DEVEL, "SIGINT %d", *it);
      kill(*it, SIGINT);
    }
  }

  DEBUG_MSG(DLVL_DEVEL, "Waiting up to 5 seconds for %d children to terminate.", (int)listcopy.size());
  waiting = 0;
  //wait up to 5 seconds for applications to shut down
  while (!listcopy.empty() && waiting <= 250) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }

  DEBUG_MSG(DLVL_DEVEL, "Sending SIGKILL to remaining %d children", (int)listcopy.size());
  //send sigkill to all remaining
  if (!listcopy.empty()) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      DEBUG_MSG(DLVL_DEVEL, "SIGKILL %d", *it);
      kill(*it, SIGKILL);
    }
  }

  DEBUG_MSG(DLVL_DEVEL, "Waiting up to a second for %d children to terminate.", (int)listcopy.size());
  waiting = 0;
  //wait up to 1 second for applications to shut down
  while (!listcopy.empty() && waiting <= 50) {
    for (it = listcopy.begin(); it != listcopy.end(); it++) {
      if (!childRunning(*it)) {
        listcopy.erase(it);
        break;
      }
      if (!listcopy.empty()) {
        Util::sleep(20);
        ++waiting;
      }
    }
  }
  if (listcopy.empty()) {
    return;
  }
  DEBUG_MSG(DLVL_DEVEL, "Giving up with %d children left.", (int)listcopy.size());

}

/// Sets up exit and childsig handlers.
/// Called by every Start* function.
void Util::Procs::setHandler() {
  if (!handler_set) {
    struct sigaction new_action;
    new_action.sa_handler = childsig_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGCHLD, &new_action, NULL);
    atexit(exit_handler);
    handler_set = true;
  }
}


/// Used internally to capture child signals and update plist.
void Util::Procs::childsig_handler(int signum) {
  if (signum != SIGCHLD) {
    return;
  }
  int status;
  pid_t ret = -1;
  while (ret != 0) {
    ret = waitpid(-1, &status, WNOHANG);
    if (ret <= 0) { //ignore, would block otherwise
      if (ret == 0 || errno != EINTR) {
        return;
      }
      continue;
    }
    int exitcode;
    if (WIFEXITED(status)) {
      exitcode = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      exitcode = -WTERMSIG(status);
    } else { // not possible
      return;
    }

    plist.erase(ret);
#if DEBUG >= DLVL_HIGH
    if (!isActive(pname)) {
      DEBUG_MSG(DLVL_HIGH, "Process %d fully terminated", ret);
    } else {
      DEBUG_MSG(DLVL_HIGH, "Child process %d exited", ret);
    }
#endif

  }
}


/// Runs the given command and returns the stdout output as a string.
std::string Util::Procs::getOutputOf(char * const * argv) {
  std::string ret;
  int fin = 0, fout = -1, ferr = 0;
  pid_t myProc = StartPiped(argv, &fin, &fout, &ferr);
  while (isActive(myProc)) {
    Util::sleep(100);
  }
  FILE * outFile = fdopen(fout, "r");
  char * fileBuf = 0;
  size_t fileBufLen = 0;
  while (!(feof(outFile) || ferror(outFile)) && (getline(&fileBuf, &fileBufLen, outFile) != -1)) {
    ret += fileBuf;
  }
  fclose(outFile);
  free(fileBuf);
  return ret;
}


///This function prepares a deque for getOutputOf and automatically inserts a NULL at the end of the char* const*
char* const* Util::Procs::dequeToArgv(std::deque<std::string> & argDeq){
  char** ret = (char**)malloc((argDeq.size()+1)*sizeof(char*));
  for (int i = 0; i<argDeq.size(); i++){
    ret[i] = (char*)argDeq[i].c_str();
  }
  ret[argDeq.size()] = NULL;
  return ret;
}

std::string Util::Procs::getOutputOf(std::deque<std::string> & argDeq){
  std::string ret;
  char* const* argv = dequeToArgv(argDeq);//Note: Do not edit deque before executing command
  ret = getOutputOf(argv);
  return ret;
}

pid_t Util::Procs::StartPiped(std::deque<std::string> & argDeq, int * fdin, int * fdout, int * fderr) {
  pid_t ret;
  char* const* argv = dequeToArgv(argDeq);//Note: Do not edit deque before executing command
  ret = Util::Procs::StartPiped(argv, fdin, fdout, fderr); 
  return ret;
}

/// Starts a new process with given fds if the name is not already active.
/// \return 0 if process was not started, process PID otherwise.
/// \arg argv Command for this process.
/// \arg fdin Standard input file descriptor. If null, /dev/null is assumed. Otherwise, if arg contains -1, a new fd is automatically allocated and written into this arg. Then the arg will be used as fd.
/// \arg fdout Same as fdin, but for stdout.
/// \arg fdout Same as fdin, but for stderr.
pid_t Util::Procs::StartPiped(char * const * argv, int * fdin, int * fdout, int * fderr) {
  pid_t pid;
  int pipein[2], pipeout[2], pipeerr[2];
  //DEBUG_MSG(DLVL_DEVEL, "setHandler");
  setHandler();
  if (fdin && *fdin == -1 && pipe(pipein) < 0) {
    DEBUG_MSG(DLVL_ERROR, "stdin pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    return 0;
  }
  if (fdout && *fdout == -1 && pipe(pipeout) < 0) {
    DEBUG_MSG(DLVL_ERROR, "stdout pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    if (*fdin == -1) {
      close(pipein[0]);
      close(pipein[1]);
    }
    return 0;
  }
  if (fderr && *fderr == -1 && pipe(pipeerr) < 0) {
    DEBUG_MSG(DLVL_ERROR, "stderr pipe creation failed for process %s, reason: %s", argv[0], strerror(errno));
    if (*fdin == -1) {
      close(pipein[0]);
      close(pipein[1]);
    }
    if (*fdout == -1) {
      close(pipeout[0]);
      close(pipeout[1]);
    }
    return 0;
  }
  int devnull = -1;
  if (!fdin || !fdout || !fderr) {
    devnull = open("/dev/null", O_RDWR);
    if (devnull == -1) {
      DEBUG_MSG(DLVL_ERROR, "Could not open /dev/null for process %s, reason: %s", argv[0], strerror(errno));
      if (*fdin == -1) {
        close(pipein[0]);
        close(pipein[1]);
      }
      if (*fdout == -1) {
        close(pipeout[0]);
        close(pipeout[1]);
      }
      if (*fderr == -1) {
        close(pipeerr[0]);
        close(pipeerr[1]);
      }
      return 0;
    }
  }
  pid = fork();
  if (pid == 0) { //child
    if (!fdin) {
      dup2(devnull, STDIN_FILENO);
    } else if (*fdin == -1) {
      close(pipein[1]); // close unused write end
      dup2(pipein[0], STDIN_FILENO);
      close(pipein[0]);
    } else if (*fdin != STDIN_FILENO) {
      dup2(*fdin, STDIN_FILENO);
    }
    if (!fdout) {
      dup2(devnull, STDOUT_FILENO);
    } else if (*fdout == -1) {
      close(pipeout[0]); // close unused read end
      dup2(pipeout[1], STDOUT_FILENO);
      close(pipeout[1]);
    } else if (*fdout != STDOUT_FILENO) {
      dup2(*fdout, STDOUT_FILENO);
    }
    if (!fderr) {
      dup2(devnull, STDERR_FILENO);
    } else if (*fderr == -1) {
      close(pipeerr[0]); // close unused read end
      dup2(pipeerr[1], STDERR_FILENO);
      close(pipeerr[1]);
    } else if (*fderr != STDERR_FILENO) {
      dup2(*fderr, STDERR_FILENO);
    }
    if (fdin && *fdin != -1 && *fdin != STDIN_FILENO) {
      close(*fdin);
    }
    if (fdout && *fdout != -1 && *fdout != STDOUT_FILENO) {
      close(*fdout);
    }
    if (fderr && *fderr != -1 && *fderr != STDERR_FILENO) {
      close(*fderr);
    }
    if (devnull != -1) {
      close(devnull);
    }
    execvp(argv[0], argv);
    DEBUG_MSG(DLVL_ERROR, "execvp failed for process %s, reason: %s", argv[0], strerror(errno));
    exit(42);
  } else if (pid == -1) {
    DEBUG_MSG(DLVL_ERROR, "fork failed for process %s, reason: %s", argv[0], strerror(errno));
    if (fdin && *fdin == -1) {
      close(pipein[0]);
      close(pipein[1]);
    }
    if (fdout && *fdout == -1) {
      close(pipeout[0]);
      close(pipeout[1]);
    }
    if (fderr && *fderr == -1) {
      close(pipeerr[0]);
      close(pipeerr[1]);
    }
    if (devnull != -1) {
      close(devnull);
    }
    return 0;
  } else { //parent
    plist.insert(pid);
    DEBUG_MSG(DLVL_HIGH, "Piped process %s started, PID %d", argv[0], pid);
    if (devnull != -1) {
      close(devnull);
    }
    if (fdin && *fdin == -1) {
      close(pipein[0]); // close unused end end
      *fdin = pipein[1];
    }
    if (fdout && *fdout == -1) {
      close(pipeout[1]); // close unused write end
      *fdout = pipeout[0];
    }
    if (fderr && *fderr == -1) {
      close(pipeerr[1]); // close unused write end
      *fderr = pipeerr[0];
    }
  }
  return pid;
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to stop.
void Util::Procs::Stop(pid_t name) {
  kill(name, SIGTERM);  
}

/// Stops the process with this pid, if running.
/// \arg name The PID of the process to murder.
void Util::Procs::Murder(pid_t name) {
  kill(name, SIGKILL);  
}

/// (Attempts to) stop all running child processes.
void Util::Procs::StopAll() {
  std::set<pid_t> listcopy = plist;
  std::set<pid_t>::iterator it;
  for (it = listcopy.begin(); it != listcopy.end(); it++) {
    Stop(*it);
  }
}

/// Returns the number of active child processes.
int Util::Procs::Count() {
  return plist.size();
}

/// Returns true if a process with this PID is currently active.
bool Util::Procs::isActive(pid_t name) {
  return (plist.count(name) == 1) && (kill(name, 0) == 0);
}

