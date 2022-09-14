#include <iostream>
#include <mist/procs.h>
#include <mist/util.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/timing.h>
#include <unistd.h>

int main(int argc, char **argv){
  int fd = -1;
  Util::printDebugLevel = 10;
  pid_t pid = Util::startConverted(argv + 1, fd);
  INFO_MSG("PID=%u", pid);
  Socket::Connection in(-1, 0);
  Socket::Connection logOut(fd, -1);
  while (Util::Procs::childRunning(pid)){
    if (in.spool()){
      while (in.Received().size()){
        logOut.SendNow(in.Received().get());
        in.Received().get().clear();
      }
    }
    if (!in){
      logOut.close();
    }
    Util::sleep(1000);
  }
  INFO_MSG("Shutting down");
  return 0;
}
