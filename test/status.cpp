#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>

#include <iostream>
#include <unistd.h>

int main(int argc, char **argv) {
  Util::Config cfg;
  cfg.activate();
  uint8_t prevStat = 255;
  while (cfg.is_active) {
    uint8_t currStat = Util::getStreamStatus(argv[1]);
    if (currStat != prevStat) {
      std::cout << "Stream status: ";
      switch (currStat) {
        case STRMSTAT_OFF: std::cout << "Off"; break;
        case STRMSTAT_INIT: std::cout << "Init"; break;
        case STRMSTAT_BOOT: std::cout << "Boot"; break;
        case STRMSTAT_WAIT: std::cout << "Wait"; break;
        case STRMSTAT_READY: std::cout << "Ready"; break;
        case STRMSTAT_SHUTDOWN: std::cout << "Shutdown"; break;
        case STRMSTAT_INVALID: std::cout << "Invalid"; break;
        default: std::cout << "??? (" << currStat << ")";
      }
      std::cout << std::endl;
      prevStat = currStat;
    }
    Util::sleep(200);
  }
  return 0;
}
