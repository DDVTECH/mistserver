/// Event loop library
#include <stddef.h>
#include <stdint.h>
#include <set>
#include <functional>
#include "socket.h"

namespace Event{

  class Loop{
  public:
    Loop();
    ~Loop();
    size_t await(size_t maxMs);
    void addSocket(size_t eId, int sock);
    void addSocket(int sock, std::function<void(void*)> cb, void* userPtr);
    void addSendQueue(Socket::UDPConnection * udpSock);
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
  };


} // Event


