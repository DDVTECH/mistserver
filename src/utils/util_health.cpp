#include <mist/ev.h>
#include <mist/procs.h>
#include <mist/stream.h>
#include <mist/url.h>
#include <mist/util.h>

#include <iostream>

int main(int argc, char **argv) {
  bool degraded = false;

  // UDP API check
  {
    std::string udpApi = Util::getGlobalConfig("udpApi");
    HTTP::URL UDPAddr(udpApi);
    if (UDPAddr.protocol != "udp") {
      std::cerr << "Local UDP API address not defined; can't send command to MistController!" << std::endl;
      degraded = true;
    } else {
      { // Check version
        Socket::UDPConnection uSock;
        uSock.allocateDestination();
        uSock.SetDestination(UDPAddr.host, UDPAddr.getPort());
        uSock.SendNow(R"({"version":true})");
        uint64_t checkStart = Util::bootMS();
        bool success = false;
        Event::Loop evLp;
        evLp.setup();
        evLp.addSocket(uSock.getSock(), [&](void *) {
          if (uSock.Receive()) {
            JSON::Value resp = JSON::fromString(uSock.data, uSock.data.size());
            if (resp.isMember("version")) {
              const JSON::Value & V = resp["version"];
              success = true;
              std::cout << "Reached " APPNAME " version " << V["version"] << " (" << V["release"] << ") built on "
                        << V["date"] << ", " << V["time"] << std::endl;
            }
          }
        }, 0);
        while (!success && checkStart + 5000 > Util::bootMS()) { evLp.await(5000); }
        if (!success) {
          std::cerr << "Cannot reach local UDP API port at " << UDPAddr.host << ":" << UDPAddr.getPort() << std::endl;
          degraded = true;
        }
      }
      { // Check load
        Socket::UDPConnection uSock;
        uSock.allocateDestination();
        uSock.SetDestination(UDPAddr.host, UDPAddr.getPort());
        uSock.SendNow(R"({"load":true})");
        uint64_t checkStart = Util::bootMS();
        bool success = false;
        Event::Loop evLp;
        evLp.setup();
        evLp.addSocket(uSock.getSock(), [&](void *) {
          if (uSock.Receive()) {
            JSON::Value resp = JSON::fromString(uSock.data, uSock.data.size());
            if (resp.isMember("load")) {
              success = true;
              const JSON::Value & L = resp["load"];
              if (!L.isMember("cpu") || L["cpu"].asInt() > 90) {
                std::cerr << "CPU usage is " << L["cpu"] << "%!" << std::endl;
                degraded = true;
              } else if (!L.isMember("mem") || L["mem"].asInt() > 90) {
                std::cerr << "RAM usage is " << L["mem"] << "%!" << std::endl;
                degraded = true;
              } else if (!L.isMember("shm") || L["shm"].asInt() > 90) {
                std::cerr << "SHM usage is " << L["shm"] << "%!" << std::endl;
                degraded = true;
              } else {
                std::cout << "CPU " << L["cpu"] << "%, RAM " << L["mem"] << "%, SHM " << L["shm"] << "%" << std::endl;
              }
            }
          }
        }, 0);
        while (!success && checkStart + 5000 > Util::bootMS()) { evLp.await(5000); }
        if (!success) {
          std::cerr << "Cannot get system load information!" << std::endl;
          degraded = true;
        }
      }
    }
  }

  // Check listening protocol status
  {
    IPC::sharedPage f(SHM_CONNECTORS, 4096, false, false);
    const Util::RelAccX A(f.mapped, false);
    size_t upCount = 0;
    if (A.isReady()) {
      for (uint32_t i = 0; i < A.getRCount(); ++i) {
        JSON::Value C = JSON::fromString(A.getPointer("cmd", i));
        uint64_t pid = A.getInt("pid", i);
        if (!Util::Procs::isActive(pid)) {
          std::cerr << C["connector"] << " process is not up!" << std::endl;
          degraded = true;
        } else {
          ++upCount;
        }
      }
    }
    if (!upCount) {
      std::cerr << "No listening ports up!" << std::endl;
      degraded = true;
    } else {
      std::cout << upCount << " ports listening for connections" << std::endl;
    }
  }

  return degraded ? 1 : 0;
}
