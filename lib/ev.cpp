#include "ev.h"
#include <string.h>
#include <sys/select.h>
#include <errno.h>
#include "timing.h"
#include <signal.h>
#include "procs.h"
#include "defines.h"

bool handlerSet = false;
bool continued = false;
bool childready = false;

void contsig_handler(int n){
  continued = true;
}

void chldsig_handler(int n){
  childready = true;
}

namespace Event{

  Loop::Loop(){
    maxPlusOne = 0;
    cbCount = 0;
    memset(sockets, 0, sizeof(sockets));
  }

  void Loop::setup(){
    if (!handlerSet){
      handlerSet = true;
      struct sigaction new_action;
      new_action.sa_handler = contsig_handler;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = 0;
      sigaction(SIGCONT, &new_action, NULL);

      struct sigaction old_action;
      sigaction(SIGCHLD, 0, &old_action);
      if (old_action.sa_handler == SIG_DFL || old_action.sa_handler == SIG_IGN){
        MEDIUM_MSG("Installing event-looped child signal handler");
        new_action.sa_handler = chldsig_handler;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGCHLD, &new_action, NULL);
      }else{
        VERYHIGH_MSG("Not installing event-looped child signal handler");
      }
    }
  }

  Loop::~Loop(){
  }

  /// Waits for up to maxMs milliseconds for an event to occur, returning the event ID.
  /// If no event occurred, returns zero.
  size_t Loop::await(size_t maxMs){
    if (pending.size()){
      size_t ret = *pending.begin();
      pending.erase(ret);
      return ret;
    }
    if (childready){
      Util::Procs::reap();
      childready = false;
    }
    if (continued){
      continued = false;
      return std::string::npos;
    }
    uint64_t startTime = Util::bootMS();
    struct timeval timeout;
    fd_set rList;
    FD_ZERO(&rList);
    for(size_t i = 0; i < 32; i += 2){
      if (sockets[i]){
        FD_SET(sockets[i+1], &rList);
      }
    }
    uint64_t currPace = Util::getMicros();
    for (auto it : sendQueues){
      uint64_t nextPace = it->timeToNextPace(currPace);
      while (!nextPace){
        it->sendPaced(0);
        nextPace = it->timeToNextPace(currPace);
      }
      if (nextPace < maxMs){maxMs = nextPace;}
    }
    int r = 0;
    do {
      uint64_t waitTime = Util::bootMS();
      if (waitTime >= startTime + maxMs){return 0;}
      waitTime = (startTime + maxMs) - waitTime;
      timeout.tv_sec = waitTime / 1000;
      timeout.tv_usec = (waitTime % 1000) * 1000;
      r = select(maxPlusOne,&rList,0,0,&timeout);
      if (r < 1 && continued){
        continued = false;
        return std::string::npos;
      }
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));
    if (r < 0 && errno == EBADF){
      for(size_t i = 0; i < 32; i += 2){
        if (sockets[i]){
          if (fcntl(sockets[i+1], F_GETFD) < 0){
            FAIL_MSG("File descriptor %" PRIu64 " (returning %" PRIu64 ") became invalid; removing from list", sockets[i+1], sockets[i]);
            sockets[i] = 0;
          }
        }
      }
      return 0;
    }
    if (r < 0){
      WARN_MSG("Event loop error: %s", strerror(errno));
    }
    if (r > 0){
      for(size_t i = 0; i < 32; i += 2){
        if (sockets[i]){
          if (FD_ISSET(sockets[i+1], &rList)){
            if ((sockets[i] & 0xFF000000) == 0xFF000000){
              size_t idx = (sockets[i] & 0x1F);
              if (cbList[idx]){cbList[idx](argList[idx]);}
            }else{
              pending.insert(sockets[i]);
            }
          }
        }
      }
    }
    if (pending.size()){
      size_t ret = *pending.begin();
      pending.erase(ret);
      return ret;
    }
    return 0;
  }

  /// Adds a socket (any kind) to the event loop
  void Loop::addSocket(size_t eId, int sock){
    if (sock >= maxPlusOne){maxPlusOne = sock + 1;}
    for(size_t i = 0; i < 32; i += 2){
      if (!sockets[i]){
        sockets[i] = eId;
        sockets[i+1] = sock;
        return;
      }
    }
  }


  void Loop::addSocket(int sock, std::function<void(void*)> cb, void* userPtr){
    if (sock >= maxPlusOne){maxPlusOne = sock + 1;}
    for(size_t i = 0; i < 32; i += 2){
      if (!sockets[i]){
        sockets[i] = 0xFF000000 + cbCount;
        sockets[i+1] = sock;
        cbList[cbCount] = cb;
        argList[cbCount] = userPtr;
        ++cbCount;
        return;
      }
    }
  }

  void Loop::addSendQueue(Socket::UDPConnection * udpSock){
    if (!udpSock){return;}
    sendQueues.insert(udpSock);
  }

  void Loop::remove(int sock){
    for(size_t i = 0; i < 32; i += 2){
      if (sockets[i+1] == sock){
        if ((sockets[i] & 0xFF000000) == 0xFF000000){
          size_t idx = (sockets[i] & 0x1F);
          cbList[idx] = 0;
          argList[idx] = 0;
        }
        sockets[i] = 0;
        sockets[i+1] = 0;
      }
    }
    for (auto it : sendQueues){
      if (it->getSock() == sock){
        Socket::UDPConnection * ptr = it;
        sendQueues.erase(it);
        delete ptr;
        break;
      }
    }
  }



} // Event

