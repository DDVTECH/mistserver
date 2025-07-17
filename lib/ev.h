/// Event loop library
#pragma once
#include "socket.h"

#include <functional>
#include <map>
#include <set>
#include <stddef.h>
#include <stdint.h>

namespace Event{

  class Handler {
    public:
      size_t retVal;
      std::function<void(void *)> cbFunc;
      std::function<bool(void *)> checkFunc;
      void *cbArg;
  };

  class Loop{
  public:
    Loop();
    ~Loop();
    size_t await(size_t maxMs);
    void addSocket(size_t eId, int sock);
    void addSocket(int sock, std::function<void(void*)> cb, void* userPtr);
    void addSendSocket(int sock, std::function<void(void *)> cb, std::function<bool(void *)> checker, void *userPtr);
    void addSendQueue(Socket::UDPConnection * udpSock);
    size_t addInterval(std::function<size_t()> cb, size_t millis);
    void removeInterval(size_t id);
    void rescheduleInterval(size_t id, size_t millis);
    void remove(int sock);
    void setup();

  private:
    std::set<Socket::UDPConnection *> sendQueues;

    // File descriptor related
    std::map<int, Handler> recvSockets;
    std::map<int, Handler> sendSockets;
    std::set<size_t> pending;

    // Timer related
    std::multimap<uint64_t, size_t> timerTimes; ///< Holds next iteration time for timers
    std::map<size_t, std::function<size_t()>> timerFuncs; ///< Holds to-be-ran function for timers
    size_t timerCount; ///< Count of the timers ever set; numbers are not reused
  };


} // Event


