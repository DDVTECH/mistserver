#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include "defines.h"
#include "shared_memory.h"

namespace IPC {
  /// Stores a long value of val in network order to the pointer p.
  static void htobl(char * p, long val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
  }

  static void htobll(char * p, long long val) {
    p[0] = (val >> 56) & 0xFF;
    p[1] = (val >> 48) & 0xFF;
    p[2] = (val >> 40) & 0xFF;
    p[3] = (val >> 32) & 0xFF;
    p[4] = (val >> 24) & 0xFF;
    p[5] = (val >> 16) & 0xFF;
    p[6] = (val >> 8) & 0xFF;
    p[7] = val & 0xFF;
  }

  static void btohl(char * p, long & val) {
    val = ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
  }

  static void btohll(char * p, long long & val) {
    val = ((long long)p[0] << 56) | ((long long)p[1] << 48) | ((long long)p[2] << 40) | ((long long)p[3] << 32) | ((long long)p[4] << 24) | ((long long)p[5] << 16) | ((long long)p[6] << 8) | p[7];
  }
  
  sharedPage::sharedPage(std::string name_, unsigned int len_, bool master_, bool autoBackoff) : handle(0), name(name_), len(len_), master(master_), mapped(NULL) {
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    init(name_,len_,master_, autoBackoff);
  }
  sharedPage::sharedPage(const sharedPage & rhs){
    handle = 0;
    name = "";
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }
  sharedPage::operator bool() const {
    return mapped != 0;
  }
  void sharedPage::operator =(sharedPage & rhs){
    init(rhs.name, rhs.len, rhs.master);
    rhs.master = false;//Make sure the memory does not get unlinked
  }
  void sharedPage::init(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    if (mapped && len){
      munmap(mapped,len);
    }
    if(master){
      shm_unlink(name.c_str());
    }
    if (handle > 0){
      close(handle);
    }
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()){
      handle = shm_open(name.c_str(), ( master ? O_CREAT | O_EXCL : 0 )| O_RDWR, ACCESSPERMS);
      if (handle == -1) {
        if (master){
          DEBUG_MSG(DLVL_HIGH, "Overwriting old page for %s", name.c_str());
          handle = shm_open(name.c_str(), O_CREAT | O_RDWR, ACCESSPERMS);
        }else{
          int i = 0;
          while (i < 10 && handle == -1 && autoBackoff){
            i++;
            Util::sleep(1000);
            handle = shm_open(name.c_str(), O_RDWR, ACCESSPERMS);
          }
        }
      }
      if (handle == -1) {
        perror(std::string("shm_open for page " + name + " failed").c_str());
        return;
      }
      if (master){
        if (ftruncate(handle, 0) < 0) {
          perror(std::string("ftruncate to zero for page " + name + " failed").c_str());
          return;
        }
        if (ftruncate(handle, len) < 0) {
          perror(std::string("ftruncate to len for page " + name + " failed").c_str());
          return;
        }
      }else{
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0){
          return;
        }
        len = buffStats.st_size;
      }
      mapped = (char*)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED){
        mapped = 0;
        return;
      }
    }
  }
  sharedPage::~sharedPage(){
    if (mapped && len){
      munmap(mapped,len);
    }
    if(master){
      shm_unlink(name.c_str());
    }
    if (handle > 0){
      close(handle);
    }
  }

  sharedFile::sharedFile(std::string name_, unsigned int len_, bool master_, bool autoBackoff) : handle(0), name(name_), len(len_), master(master_), mapped(NULL) {
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    init(name_,len_,master_, autoBackoff);
  }
  sharedFile::sharedFile(const sharedPage & rhs){
    handle = 0;
    name = "";
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }
  sharedFile::operator bool() const {
    return mapped != 0;
  }
  void sharedFile::operator =(sharedFile & rhs){
    init(rhs.name, rhs.len, rhs.master);
    rhs.master = false;//Make sure the memory does not get unlinked
  }
  void sharedFile::init(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    if (mapped && len){
      munmap(mapped,len);
    }
    if(master){
      unlink(name.c_str());
    }
    if (handle > 0){
      close(handle);
    }
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()){
      /// \todo Use ACCESSPERMS instead of 0600?
      handle = open(name.c_str(), ( master ? O_CREAT | O_TRUNC | O_EXCL : 0 )| O_RDWR, (mode_t)0600);
      if (handle == -1) {
        if (master){
          DEBUG_MSG(DLVL_HIGH, "Overwriting old file for %s", name.c_str());
          handle = open(name.c_str(), O_CREAT | O_TRUNC | O_RDWR, (mode_t)0600);
        }else{
          int i = 0;
          while (i < 10 && handle == -1 && autoBackoff){
            i++;
            Util::sleep(1000);
            handle = open(name.c_str(), O_RDWR, (mode_t)0600);
          }
        }
      }
      if (handle == -1) {
        perror(std::string("open for file " + name + " failed").c_str());
        return;
      }
      if (master){
        if (ftruncate(handle, len) < 0) {
          perror(std::string("ftruncate to len for file " + name + " failed").c_str());
          return;
        }
      }else{
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0){
          return;
        }
        len = buffStats.st_size;
      }
      mapped = (char*)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED){
        mapped = 0;
        return;
      }
    }
  }
  sharedFile::~sharedFile(){
    if (mapped && len){
      munmap(mapped,len);
    }
    if(master){
      unlink(name.c_str());
    }
    if (handle > 0){
      close(handle);
    }
  }
  
  statExchange::statExchange(char * _data) : data(_data) {}

  void statExchange::now(long long int time) {
    htobll(data, time);
  }

  long long int statExchange::now() {
    long long int result;
    btohll(data, result);
    return result;
  }

  void statExchange::time(long time) {
    htobl(data + 8, time);
  }

  long statExchange::time() {
    long result;
    btohl(data + 8, result);
    return result;
  }

  void statExchange::lastSecond(long time) {
    htobl(data + 12, time);
  }

  long statExchange::lastSecond() {
    long result;
    btohl(data + 12, result);
    return result;
  }

  void statExchange::down(long long int bytes) {
    htobll(data + 16, bytes);
  }

  long long int statExchange::down() {
    long long int result;
    btohll(data + 16, result);
    return result;
  }

  void statExchange::up(long long int bytes) {
    htobll(data + 24, bytes);
  }

  long long int statExchange::up() {
    long long int result;
    btohll(data + 24, result);
    return result;
  }

  void statExchange::host(std::string name) {
    memcpy(data + 32, name.c_str(), std::min((int)name.size(), 16));
  }

  std::string statExchange::host() {
    return std::string(data + 32, std::min((int)strlen(data + 32), 16));
  }

  void statExchange::streamName(std::string name) {
    memcpy(data + 48, name.c_str(), std::min((int)name.size(), 20));
  }

  std::string statExchange::streamName() {
    return std::string(data + 48, std::min((int)strlen(data + 48), 20));
  }

  void statExchange::connector(std::string name) {
    memcpy(data + 68, name.c_str(), std::min((int)name.size(), 20));
  }

  std::string statExchange::connector() {
    return std::string(data + 68, std::min((int)strlen(data + 68), 20));
  }


  semGuard::semGuard(sem_t * semaphore) : mySemaphore(semaphore) {
    sem_wait(mySemaphore);
  }

  semGuard::~semGuard() {
    sem_post(mySemaphore);
  }
  
  sharedServer::sharedServer(){
    mySemaphore = 0;
    payLen = 0;
    hasCounter = false;
    amount = 0;
  }

  sharedServer::sharedServer(std::string name, int len, bool withCounter){
    sharedServer();
    init(name, len, withCounter);
  }
    
    
  void sharedServer::init(std::string name, int len, bool withCounter){
    amount = 0;
    if (mySemaphore != SEM_FAILED) {
      sem_close(mySemaphore);
    }
    if (baseName != ""){
      sem_unlink(std::string("/" + baseName).c_str());
    }
    myPages.clear();
    baseName = name;
    payLen = len;
    hasCounter = withCounter;
    mySemaphore = sem_open(std::string("/" + baseName).c_str(), O_CREAT | O_EXCL | O_RDWR, ACCESSPERMS, 1);
    if (mySemaphore == SEM_FAILED) {
      mySemaphore = sem_open(std::string("/" + baseName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    }
    if (mySemaphore == SEM_FAILED) {
      perror("Creating semaphore failed");
      return;
    }
    newPage();
    newPage();
    newPage();
    newPage();
    newPage();
  }

  sharedServer::~sharedServer() {
    if (mySemaphore != SEM_FAILED) {
      sem_close(mySemaphore);
    }
    sem_unlink(std::string("/" + baseName).c_str());
  }

  sharedServer::operator bool() const {
    return myPages.size();
  }

  void sharedServer::newPage() {
    semGuard tmpGuard(mySemaphore);
    sharedPage tmp(std::string(baseName + (char)(myPages.size() + (int)'A')), (4096 << myPages.size()), true);
    myPages.insert(tmp);
    tmp.master = false;
    DEBUG_MSG(DLVL_WARN, "Added a new page: %s", tmp.name.c_str());
  }

  void sharedServer::deletePage() {
    if (myPages.size() == 1) {
      DEBUG_MSG(DLVL_WARN, "Can't remove last page for %s", baseName.c_str());
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPages.erase((*myPages.rbegin()));
  }

  bool sharedServer::isInUse(unsigned int id){
    unsigned int i = 0;
    for (std::set<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      //return if we reached the end
      if (!it->mapped || !it->len){
        return false;
      }
      //not on this page? skip to next.
      if (it->len < (id - i)*payLen){
        i += it->len / payLen;
        continue;
      }
      if (hasCounter){
        //counter? return true if it is non-zero.
        return (it->mapped[(id - i)*payLen] != 0);
      }else{
        //no counter - check the entire size for being all zeroes.
        for (unsigned int j = 0; j < payLen; ++j){
          if (it->mapped[(id-i)*payLen+j]){
            return true;
          }
        }
        return false;
      }
    }
    //only happens if we run out of pages
    return false;
  }
  
  void sharedServer::parseEach(void (*callback)(char * data, size_t len, unsigned int id)) {
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      memset(empty, 0, payLen);
    }
    unsigned int id = 0;
    for (std::set<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      if (!it->mapped || !it->len){
        DEBUG_MSG(DLVL_FAIL, "Something went terribly wrong?");
        break;
      }
      unsigned int offset = 0;
      while (offset + payLen + (hasCounter ? 1 : 0) <= it->len) {
        if (hasCounter){
          if (it->mapped[offset] != 0) {
            int counter = it->mapped[offset];
            //increase the count if needed
            if (id >= amount){
              amount = id+1;
              DEBUG_MSG(DLVL_DEVEL, "Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            callback(it->mapped + offset + 1, payLen, id);
            switch (counter) {
              case 127:
                DEBUG_MSG(DLVL_HIGH, "Client %u requested disconnect", id);
                break;
              case 126:
                DEBUG_MSG(DLVL_HIGH, "Client %u timed out", id);
                break;
              case 255:
                DEBUG_MSG(DLVL_HIGH, "Client %u disconnected on request", id);
                break;
              case 254:
                DEBUG_MSG(DLVL_HIGH, "Client %u disconnect timed out", id);
                break;
              default:
                break;
            }
            if (counter == 127 || counter == 126 || counter == 255 || counter == 254) {
              memset(it->mapped + offset + 1, 0, payLen);
              it->mapped[offset] = 0;
            } else {
              it->mapped[offset] ++;
            }
          }else{
            //stop if we're past the amount counted and we're empty
            if (id >= amount - 1){
              //bring the counter down if this was the last element
              if (id == amount - 1){
                amount = id;
                DEBUG_MSG(DLVL_DEVEL, "Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              return;
            }
          }
        }else{
          if (memcmp(empty, it->mapped + offset, payLen)) {
            //increase the count if needed
            if (id >= amount){
              amount = id+1;
              DEBUG_MSG(DLVL_DEVEL, "Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            callback(it->mapped + offset, payLen, id);
          }else{
            //stop if we're past the amount counted and we're empty
            if (id >= amount - 1){
              //bring the counter down if this was the last element
              if (id == amount - 1){
                amount = id;
                DEBUG_MSG(DLVL_DEVEL, "Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              if (empty){
                free(empty);
              }
              return;
            }
          }
        }
        offset += payLen + (hasCounter ? 1 : 0);
        id ++;
      }
    }
    if (empty){
      free(empty);
    }
  }

  sharedClient::sharedClient() {
    hasCounter = 0;
    payLen = 0;
    offsetOnPage = 0;
  }

  sharedClient::sharedClient(const sharedClient & rhs ) {
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
    mySemaphore = sem_open(std::string("/" + baseName).c_str(), O_RDWR);
    if (mySemaphore == SEM_FAILED) {
      perror("Creating semaphore failed");
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPage.init(rhs.myPage.name,rhs.myPage.len,rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  void sharedClient::operator =(const sharedClient & rhs ) {
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
    mySemaphore = sem_open(std::string("/" + baseName).c_str(), O_RDWR);
    if (mySemaphore == SEM_FAILED) {
      perror("Creating semaphore failed");
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPage.init(rhs.myPage.name,rhs.myPage.len,rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  sharedClient::sharedClient(std::string name, int len, bool withCounter) : baseName(name), payLen(len), offsetOnPage(-1), hasCounter(withCounter) {
    mySemaphore = sem_open(std::string("/" + baseName).c_str(), O_RDWR);
    if (mySemaphore == SEM_FAILED) {
      perror("Creating semaphore failed");
      return;
    }
    semGuard tmpGuard(mySemaphore);
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      if (!empty){
        DEBUG_MSG(DLVL_FAIL, "Failed to allocate %u bytes for empty payload!", payLen);
        return;
      }
      memset(empty, 0, payLen);
    }
    for (char i = 'A'; i <= 'Z'; i++) {
      myPage.init(baseName + i, (4096 << (i - 'A')));
      int offset = 0;
      while (offset + payLen + (hasCounter ? 1 : 0) <= myPage.len) {
        if ((hasCounter && myPage.mapped[offset] == 0) || (!hasCounter && !memcmp(myPage.mapped + offset, empty, payLen))) {
          offsetOnPage = offset;
          if (hasCounter) {
            myPage.mapped[offset] = 1;
          }
          break;
        }
        offset += payLen + (hasCounter ? 1 : 0);
      }
      if (offsetOnPage != -1) {
        break;
      }
    }
    free(empty);
  }

  sharedClient::~sharedClient() {
    if (hasCounter){
      finish();
    }
    if (mySemaphore != SEM_FAILED) {
      sem_close(mySemaphore);
    }
  }

  void sharedClient::write(char * data, int len) {
    if (hasCounter) {
      keepAlive();
    }
    memcpy(myPage.mapped + offsetOnPage + (hasCounter ? 1 : 0), data, std::min(len, payLen));
  }

  void sharedClient::finish() {
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to time-out an element without counters");
      return;
    }
    if (myPage.mapped){
      myPage.mapped[offsetOnPage] = 127;
    }
  }

  void sharedClient::keepAlive() {
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to keep-alive an element without counters");
      return;
    }
    if (myPage.mapped[offsetOnPage] < 128){
      myPage.mapped[offsetOnPage] = 1;
    }else{
      DEBUG_MSG(DLVL_WARN, "Trying to keep-alive an element that needs to timeout, ignoring");
    }
  }

  char * sharedClient::getData() {
    if (!myPage.mapped){return 0;}
    return (myPage.mapped + offsetOnPage + (hasCounter ? 1 : 0));
  }
}
