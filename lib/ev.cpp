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

  Loop::Loop() {
    timerCount = 0;
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
    while (timerTimes.size() && timerTimes.begin()->first <= startTime) {
      auto it = timerTimes.begin();
      uint64_t t = it->first;
      size_t tid = it->second;

      timerTimes.erase(it);
      VERYHIGH_MSG("Firing timer %zu", tid);
      size_t incr = timerFuncs[tid]();
      if (incr) {
        // Reschedule the timer
        timerTimes.insert({t + incr, tid});
      } else {
        // Drop the timer
        VERYHIGH_MSG("Stopping timer %zu", tid);
        timerFuncs.erase(tid);
      }
    }
    if (timerTimes.size()) {
      auto it = timerTimes.begin();
      if (maxMs > it->first - startTime) { maxMs = it->first - startTime; }
    }
    struct timeval timeout;
    int maxPlusOne = 0;
    fd_set rList, sList;
    FD_ZERO(&rList);
    FD_ZERO(&sList);
    for (auto & S : recvSockets) {
      if (S.first < 1024) {
        FD_SET(S.first, &rList);
        if (S.first >= maxPlusOne) { maxPlusOne = S.first + 1; }
      }
    }
    for (auto & S : sendSockets) {
      if (S.first < 1024 && S.second.checkFunc(S.second.cbArg)) {
        FD_SET(S.first, &sList);
        if (S.first >= maxPlusOne) { maxPlusOne = S.first + 1; }
      }
    }
    if (!maxPlusOne) {
      Util::sleep(maxMs);
      if (continued) {
        continued = false;
        return std::string::npos;
      }
      if (childready) {
        Util::Procs::reap();
        childready = false;
      }
      return 0;
    }
    uint64_t currPace = Util::getMicros();
    for (auto it : sendQueues){
      if (it->finTimer) {
#ifdef SSL
        if (it->finTimer <= startTime) { it->handshake(); }
#endif
        if (it->finTimer && it->finTimer > startTime && it->finTimer - startTime < maxMs) {
          maxMs = it->finTimer - startTime;
        }
      }
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
      r = select(maxPlusOne, &rList, &sList, 0, &timeout);
      if (r < 1 && continued){
        continued = false;
        return std::string::npos;
      }
      if (childready) {
        Util::Procs::reap();
        childready = false;
      }
    } while (r < 0 && (errno == EINTR || errno == EAGAIN));
    if (r < 0 && errno == EBADF){
      std::set<int> toErase;
      for (auto & S : recvSockets) {
        if (S.first < 1024 && fcntl(S.first, F_GETFD) < 0) {
          WARN_MSG("File descriptor %d (returning %zu) became invalid; removing from list", S.first, S.second.retVal);
          toErase.insert(S.first);
        }
      }
      for (auto & i : toErase) { recvSockets.erase(i); }
      toErase.clear();
      for (auto & S : sendSockets) {
        if (S.first < 1024 && fcntl(S.first, F_GETFD) < 0) {
          WARN_MSG("File descriptor %d (returning %zu) became invalid; removing from list", S.first, S.second.retVal);
          toErase.insert(S.first);
        }
      }
      for (auto & i : toErase) { sendSockets.erase(i); }
      return 0;
    }
    if (r < 0){
      WARN_MSG("Event loop error: %s", strerror(errno));
    }
    if (r > 0){
      // Callbacks are collected all at once, then executed afterwards.
      // This is needed because callbacks could alter the sockets list.
      // Check ready for reading
      std::set<int> toExec;
      for (auto & S : recvSockets) {
        if (S.first < 1024 && FD_ISSET(S.first, &rList)) {
          if (S.second.cbFunc) {
            toExec.insert(S.first);
          } else {
            pending.insert(S.second.retVal);
          }
        }
      }
      // We do another lookup for each callback in the list, since it's possible it
      // was removed or overwritten by another callback.
      for (auto & E : toExec) {
        auto iter = recvSockets.find(E);
        if (iter != recvSockets.end()) { iter->second.cbFunc(iter->second.cbArg); }
      }

      toExec.clear();

      // Check ready for sending
      for (auto & S : sendSockets) {
        if (S.first < 1024 && FD_ISSET(S.first, &sList)) {
          if (S.second.cbFunc) {
            toExec.insert(S.first);
          } else {
            pending.insert(S.second.retVal);
          }
        }
      }
      // We do another lookup for each callback in the list, since it's possible it
      // was removed or overwritten by another callback.
      for (auto & E : toExec) {
        auto iter = sendSockets.find(E);
        if (iter != sendSockets.end()) { iter->second.cbFunc(iter->second.cbArg); }
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
    if (recvSockets.count(sock)) {
      FAIL_MSG("Cannot add handler for socket %d: another handler is already installed!", sock);
      return;
    }
    recvSockets[sock].retVal = eId;
  }


  void Loop::addSocket(int sock, std::function<void(void*)> cb, void* userPtr){
    if (recvSockets.count(sock)) {
      FAIL_MSG("Cannot add handler for socket %d: another handler is already installed!", sock);
      return;
    }
    recvSockets[sock].cbFunc = cb;
    recvSockets[sock].cbArg = userPtr;
  }

  void Loop::addSendSocket(int sock, std::function<void(void *)> cb, std::function<bool(void *)> checker, void *userPtr) {
    if (sendSockets.count(sock)) {
      FAIL_MSG("Cannot add handler for socket %d: another handler is already installed!", sock);
      return;
    }
    sendSockets[sock].cbFunc = cb;
    sendSockets[sock].checkFunc = checker;
    sendSockets[sock].cbArg = userPtr;
  }

  void Loop::addSendQueue(Socket::UDPConnection * udpSock){
    if (!udpSock){return;}
    sendQueues.insert(udpSock);
  }

  void Loop::remove(int sock){
    recvSockets.erase(sock);
    sendSockets.erase(sock);
    for (auto it : sendQueues){
      if (it->getSock() == sock){
        Socket::UDPConnection * ptr = it;
        sendQueues.erase(it);
        delete ptr;
        break;
      }
    }
  }

  size_t Loop::addInterval(std::function<size_t()> cb, size_t millis) {
    if (!millis) { return std::string::npos; }
    timerFuncs[timerCount] = cb;
    timerTimes.insert({Util::bootMS() + millis, timerCount});
    return timerCount++;
  }

  void Loop::removeInterval(size_t id) {
    timerFuncs.erase(id);
    for (auto it = timerTimes.cbegin(); it != timerTimes.cend(); ++it) {
      if (it->second == id) {
        timerTimes.erase(it);
        return;
      }
    }
  }

  void Loop::rescheduleInterval(size_t id, size_t millis) {
    uint64_t now = Util::bootMS();
    bool found = false;
    for (auto it = timerTimes.begin(); it != timerTimes.end(); ++it) {
      if (it->second == id) {
        timerTimes.erase(it);
        found = true;
        break;
      }
    }
    if (!found) {
      WARN_MSG("Could not reschedule non-scheduled timer %zu!", id);
      return;
    }
    timerTimes.insert({now + millis, id});
  }

} // Event

