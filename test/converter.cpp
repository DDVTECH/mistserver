#include <iostream>
#include <mist/procs.h>
#include <mist/defines.h>
#include <mist/timing.h>
#include <unistd.h>

int main(int argc, char **argv){
  int fd = 0;
  Util::printDebugLevel = 10;
  pid_t pid = Util::Procs::startConverted(argv + 1, &fd);
  while (Util::Procs::isRunning(pid)){
    Util::sleep(1000);
  }
  INFO_MSG("Shutting down");
  return 0;
}
