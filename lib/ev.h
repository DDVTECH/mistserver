/// Event loop library
#include "socket.h"

#include <functional>
#include <map>
#include <set>
#include <stddef.h>
#include <stdint.h>

namespace Event{

  class Loop{
  public:
    Loop();
    ~Loop();
    size_t await(size_t maxMs);
    void addSocket(size_t eId, int sock);
    void addSocket(int sock, std::function<void(void*)> cb, void* userPtr);
    void addSendQueue(Socket::UDPConnection * udpSock);
    void addInterval(std::function<bool()> cb, size_t millis);
    void remove(int sock);
    void setup();
  private:
    int maxPlusOne;
    size_t cbCount;
    uint64_t sockets[64];
    std::function<void(void*)> cbList[32];
    void* argList[32];
    std::set<Socket::UDPConnection *> sendQueues;
    std::set<size_t> pending;

    // Timer related
    std::multimap<uint64_t, size_t> timerTimes; ///< Holds next iteration time for timers
    std::map<size_t, std::function<bool()>> timerFuncs; ///< Holds to-be-ran function for timers
    std::map<size_t, size_t> timerIntervals; ///< Holds interval in millis for timers
    size_t timerCount; ///< Count of the timers ever set; numbers are not reused
  };


} // Event


