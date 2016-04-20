#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>

#include INPUTTYPE 
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/procs.h>

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0]);
  mistIn conv(&conf);
  if (conf.parseArgs(argc, argv)) {
    std::string streamName = conf.getString("streamname");
    conv.argumentsParsed();
#ifndef INPUT_NOLOCK
    IPC::semaphore playerLock;
    if (streamName.size()){
      playerLock.open(std::string("/lock_" + streamName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
      if (!playerLock.tryWait()){
        DEBUG_MSG(DLVL_DEVEL, "A player for stream %s is already running", streamName.c_str());
        return 1;
      }
    }
#endif
    conf.activate();
    while (conf.is_active){
      pid_t pid = fork();
      if (pid == 0){
#ifndef INPUT_NOLOCK
        playerLock.close();
#endif
        return conv.run();
      }
      if (pid == -1){
        DEBUG_MSG(DLVL_FAIL, "Unable to spawn player process");
#ifndef INPUT_NOLOCK
        playerLock.post();
#endif
        return 2;
      }
      //wait for the process to exit
      int status;
      while (waitpid(pid, &status, 0) != pid && errno == EINTR){
        if (!conf.is_active){
          DEBUG_MSG(DLVL_DEVEL, "Shutting down input for stream %s because of signal interrupt...", streamName.c_str());
          Util::Procs::Stop(pid);
        }
        continue;
      }
      //if the exit was clean, don't restart it
      if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)){
        DEBUG_MSG(DLVL_MEDIUM, "Input for stream %s shut down cleanly", streamName.c_str());
        break;
      }
#if DEBUG >= DLVL_DEVEL
      WARN_MSG("Aborting autoclean; this is a development build.");
#else
      conv.onCrash();
#endif
      if (DEBUG >= DLVL_DEVEL){
        DEBUG_MSG(DLVL_DEVEL, "Input for stream %s uncleanly shut down! Aborting restart; this is a development build.", streamName.c_str());
        break;
      }else{
        DEBUG_MSG(DLVL_DEVEL, "Input for stream %s uncleanly shut down! Restarting...", streamName.c_str());
      }
    }
#ifndef INPUT_NOLOCK
    playerLock.post();
    playerLock.close();
#endif
  }
  return 0;
}

