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

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  mistIn conv(&conf);
  if (conf.parseArgs(argc, argv)) {
    sem_t * playerLock = sem_open(std::string("/lock_" + conf.getString("streamname")).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (sem_trywait(playerLock) == -1){
      DEBUG_MSG(DLVL_DEVEL, "A player for stream %s is already running", conf.getString("streamname").c_str());
      return 1;
    }
    conf.activate();
    while (conf.is_active){
      int pid = fork();
      if (pid == 0){
        sem_close(playerLock);
        return conv.run();
      }
      if (pid == -1){
        DEBUG_MSG(DLVL_FAIL, "Unable to spawn player process");
        sem_post(playerLock);
        return 2;
      }
      //wait for the process to exit
      int status;
      while (waitpid(pid, &status, 0) != pid && errno == EINTR) continue;
      //clean up the semaphore by waiting for it, if it's non-zero
      sem_t * waiting = sem_open(std::string("/wait_" + conf.getString("streamname")).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 0);
      if (waiting == SEM_FAILED){
        DEBUG_MSG(DLVL_FAIL, "Failed to open semaphore - cancelling");
        return -1;
      }
      int sem_val = 0;
      sem_getvalue(waiting, &sem_val);
      while (sem_val){
        while (sem_wait(waiting) == -1 && errno == EINTR) continue;
        sem_getvalue(waiting, &sem_val);
      }
      sem_close(waiting);
      //if the exit was clean, don't restart it
      if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)){
        DEBUG_MSG(DLVL_DEVEL, "Finished player succesfully");
        break;
      }
    }
    sem_post(playerLock);
    sem_close(playerLock);
  }
  return 0;
}


