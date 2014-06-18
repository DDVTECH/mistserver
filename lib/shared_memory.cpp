#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>

#include <iostream>
#include "defines.h"
#include "shared_memory.h"
#include "stream.h"

namespace IPC {
  /// Stores a long value of val in network order to the pointer p.
  static void htobl(char * p, long val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
  }

  /// Stores a long long value of val in network order to the pointer p.
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

  /// Reads a long value of p in host order to val.
  static void btohl(char * p, long & val) {
    val = ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
  }

  /// Reads a long long value of p in host order to val.
  static void btohll(char * p, long long & val) {
    val = ((long long)p[0] << 56) | ((long long)p[1] << 48) | ((long long)p[2] << 40) | ((long long)p[3] << 32) | ((long long)p[4] << 24) | ((long long)p[5] << 16) | ((long long)p[6] << 8) | p[7];
  }

  ///\brief Empty semaphore constructor, clears all values
  semaphore::semaphore() {
#ifdef __CYGWIN__
    mySem = 0;
#else
    mySem = SEM_FAILED;
#endif
    myName = 0;
  }

  ///\brief Constructs a named semaphore
  ///\param name The name of the semaphore
  ///\param oflag The flags with which to open the semaphore
  ///\param mode The mode in which to create the semaphore, if O_CREAT is given in oflag, ignored otherwise
  ///\param value The initial value of the semaphore if O_CREAT is given in oflag, ignored otherwise
  semaphore::semaphore(const char * name, int oflag, mode_t mode, unsigned int value) {
#ifdef __CYGWIN__
    mySem = 0;
#else
    mySem = SEM_FAILED;
#endif
    open(name, oflag, mode, value);
  }

  ///\brief The deconstructor
  semaphore::~semaphore() {}

  ///\brief Returns whether we have a valid semaphore
  semaphore::operator bool() const {
#ifdef __CYGWIN__
    return mySem != 0;
#else
    return mySem != SEM_FAILED;
#endif
  }

  ///\brief Opens a semaphore
  ///
  ///Closes currently opened semaphore if needed
  ///\param name The name of the semaphore
  ///\param oflag The flags with which to open the semaphore
  ///\param mode The mode in which to create the semaphore, if O_CREAT is given in oflag, ignored otherwise
  ///\param value The initial value of the semaphore if O_CREAT is given in oflag, ignored otherwise
  void semaphore::open(const char * name, int oflag, mode_t mode, unsigned int value) {
    close();
#ifdef __CYGWIN__
    mySem = CreateSemaphore(0, value, 1 , std::string("Global\\" + std::string(name)).c_str());
#else
    if (oflag & O_CREAT) {
      mySem = sem_open(name, oflag, mode, value);
    } else {
      mySem = sem_open(name, oflag);
    }
#endif
    myName = (char *)name;
  }

  ///\brief Returns the current value of the semaphore
  int semaphore::getVal() const {
#ifdef __CYGWIN__
    LONG res;
    ReleaseSemaphore(mySem, 0, &res);//not really release.... just checking to see if I can get the value this way
#else
    int res;
    sem_getvalue(mySem, &res);
#endif
    return res;
  }

  ///\brief Posts to the semaphore, increases its value by one
  void semaphore::post() {
    if (*this) {
#ifdef __CYGWIN__
      ReleaseSemaphore(mySem, 1, 0);
#else
      sem_post(mySem);
#endif
    }
  }

  ///\brief Waits for the semaphore, decreases its value by one
  void semaphore::wait() {
    if (*this) {
#ifdef __CYGWIN__
      WaitForSingleObject(mySem, INFINITE);
#else
      int tmp;
      do {
        tmp = sem_wait(mySem);
      } while (tmp == -1 && errno == EINTR);
#endif
    }
  }

  ///\brief Tries to wait for the semaphore, returns true if successfull, false otherwise
  bool semaphore::tryWait() {
    bool result;
#ifdef __CYGWIN__
    result = WaitForSingleObject(mySem, 0);//wait at most 1ms
#else
    result = sem_trywait(mySem);
#endif
    return (result == 0);
  }

  ///\brief Closes the currently opened semaphore
  void semaphore::close() {
    if (*this) {
#ifdef __CYGWIN__
      CloseHandle(mySem);
      mySem = 0;
#else
      sem_close(mySem);
      mySem = SEM_FAILED;
#endif
    }
  }

  ///\brief Unlinks the previously opened semaphore
  void semaphore::unlink() {
    close();
#ifndef __CYGWIN__
    if (myName) {
      sem_unlink(myName);
    }
#endif
    myName = 0;
  }


#if !defined __APPLE__
  ///\brief Unmaps a shared page if allowed
  void sharedPage::unmap() {
    if (mapped && len) {
#ifdef __CYGWIN__
      UnmapViewOfFile(mapped);
#else
      munmap(mapped, len);
#endif
      mapped = 0;
      len = 0;
    }
  }

  ///\brief Closes a shared page if allowed
  void sharedPage::close() {
    unmap();
    if (handle > 0) {
#ifdef __CYGWIN__
      CloseHandle(handle);
#else
      ::close(handle);
      if (master && name != "") {
        shm_unlink(name.c_str());
      }
#endif
      handle = 0;
    }
  }


  ///brief Creates a shared page
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  sharedPage::sharedPage(std::string name_, unsigned int len_, bool master_, bool autoBackoff) : handle(0), len(0), master(false), mapped(0) {
    init(name_, len_, master_, autoBackoff);
  }

  ///\brief Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedPage::sharedPage(const sharedPage & rhs) {
    handle = 0;
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }

  ///\brief Returns whether the shared page is valid or not
  sharedPage::operator bool() const {
    return mapped != 0;
  }

  ///\brief Assignment operator
  void sharedPage::operator =(sharedPage & rhs) {
    init(rhs.name, rhs.len, rhs.master);
    rhs.master = false;//Make sure the memory does not get unlinked
  }


  ///\brief Initialize a page, de-initialize before if needed
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  void sharedPage::init(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    close();
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()) {
#ifdef __CYGWIN__
      if (master) {
        handle = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, len, name.c_str());
      } else {
        int i = 0;
        do {
          if (i != 0) {
            Util::sleep(1000);
          }
          handle = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
          i++;
        } while (i < 10 && !handle && autoBackoff);
      }
      if (!handle) {
        DEBUG_MSG(DLVL_FAIL, "%s for page %s failed: %s", (master ? "CreateFileMapping" : "OpenFileMapping"), name.c_str(), strerror(errno));
        return;
      }
      mapped = (char *)MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
      if (!mapped) {
        return;
      }
#else
      handle = shm_open(name.c_str(), (master ? O_CREAT | O_EXCL : 0) | O_RDWR, ACCESSPERMS);
      if (handle == -1) {
        if (master) {
          DEBUG_MSG(DLVL_HIGH, "Overwriting old page for %s", name.c_str());
          handle = shm_open(name.c_str(), O_CREAT | O_RDWR, ACCESSPERMS);
        } else {
          int i = 0;
          while (i < 10 && handle == -1 && autoBackoff) {
            i++;
            Util::sleep(1000);
            handle = shm_open(name.c_str(), O_RDWR, ACCESSPERMS);
          }
        }
      }
      if (handle == -1) {
        DEBUG_MSG(DLVL_FAIL, "shm_open for page %s failed: %s", name.c_str(), strerror(errno));
        return;
      }
      if (master) {
        if (ftruncate(handle, 0) < 0) {
          DEBUG_MSG(DLVL_FAIL, "truncate to zero for page %s failed: %s", name.c_str(), strerror(errno));
          return;
        }
        if (ftruncate(handle, len) < 0) {
          DEBUG_MSG(DLVL_FAIL, "truncate to %lld for page %s failed: %s", len, name.c_str(), strerror(errno));
          return;
        }
      } else {
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0) {
          return;
        }
        len = buffStats.st_size;
      }
      mapped = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED) {
        mapped = 0;
        return;
      }
#endif
    }
  }

  ///\brief Default destructor
  sharedPage::~sharedPage() {
    close();
  }

#else

  ///brief Creates a shared page
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  sharedPage::sharedPage(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    handle = 0;
    len = 0;
    master = false;
    mapped = 0;
    init(name_, len_, master_, autoBackoff);
  }

  ///\brief Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedPage::sharedPage(const sharedPage & rhs) {
    handle = 0;
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }

  ///\brief Default destructor
  sharedPage::~sharedPage() {
    unmap();
    close(handle);
    if (master && name != "") {
      unlink(name.c_str());
    }
  }

#endif

  ///brief Creates a shared file
  ///\param name_ The name of the file to be created
  ///\param len_ The size to make the file
  ///\param master_ Whether to create or merely open the file
  ///\param autoBackoff When only opening the file, wait for it to appear or fail
  sharedFile::sharedFile(std::string name_, unsigned int len_, bool master_, bool autoBackoff) : handle(0), name(name_), len(len_), master(master_), mapped(NULL) {
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    init(name_, len_, master_, autoBackoff);
  }


  ///\brief Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedFile::sharedFile(const sharedFile & rhs) {
    handle = 0;
    name = "";
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }

  ///\brief Returns whether the shared file is valid or not
  sharedFile::operator bool() const {
    return mapped != 0;
  }

  ///\brief Assignment operator
  void sharedFile::operator =(sharedFile & rhs) {
    init(rhs.name, rhs.len, rhs.master);
    rhs.master = false;//Make sure the memory does not get unlinked
  }

  ///\brief Unmaps a shared file if allowed
  void sharedFile::unmap() {
    if (mapped && len) {
      munmap(mapped, len);
      mapped = 0;
      len = 0;
    }
  }

  ///\brief Initialize a page, de-initialize before if needed
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  void sharedFile::init(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    unmap();
    if (master) {
      unlink(name.c_str());
    }
    if (handle > 0) {
      close(handle);
    }
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()) {
      /// \todo Use ACCESSPERMS instead of 0600?
      handle = open(std::string(Util::getTmpFolder() + name).c_str(), (master ? O_CREAT | O_TRUNC | O_EXCL : 0) | O_RDWR, (mode_t)0600);
      if (handle == -1) {
        if (master) {
          DEBUG_MSG(DLVL_HIGH, "Overwriting old file for %s", name.c_str());
          handle = open(std::string(Util::getTmpFolder() + name).c_str(), O_CREAT | O_TRUNC | O_RDWR, (mode_t)0600);
        } else {
          int i = 0;
          while (i < 10 && handle == -1 && autoBackoff) {
            i++;
            Util::sleep(1000);
            handle = open(std::string(Util::getTmpFolder() + name).c_str(), O_RDWR, (mode_t)0600);
          }
        }
      }
      if (handle == -1) {
        perror(std::string("open for file " + name + " failed").c_str());
        return;
      }
      if (master) {
        if (ftruncate(handle, len) < 0) {
          perror(std::string("ftruncate to len for file " + name + " failed").c_str());
          return;
        }
      } else {
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0) {
          return;
        }
        len = buffStats.st_size;
      }
      mapped = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED) {
        mapped = 0;
        return;
      }
    }
  }

  ///\brief Default destructor
  sharedFile::~sharedFile() {
    unmap();
    if (master) {
      unlink(name.c_str());
    }
    if (handle > 0) {
      close(handle);
    }
  }


  ///\brief StatExchange constructor, sets the datapointer to the given value
  statExchange::statExchange(char * _data) : data(_data) {}

  ///\brief Sets timestamp of the current stats
  void statExchange::now(long long int time) {
    htobll(data, time);
  }

  ///\brief Gets timestamp of the current stats
  long long int statExchange::now() {
    long long int result;
    btohll(data, result);
    return result;
  }

  ///\brief Sets time currently connected
  void statExchange::time(long time) {
    htobl(data + 8, time);
  }

  ///\brief Gets time currently connected
  long statExchange::time() {
    long result;
    btohl(data + 8, result);
    return result;
  }

  ///\brief Sets the last viewing second of this user
  void statExchange::lastSecond(long time) {
    htobl(data + 12, time);
  }

  ///\brief Gets the last viewing second of this user
  long statExchange::lastSecond() {
    long result;
    btohl(data + 12, result);
    return result;
  }

  ///\brief Sets the amount of bytes received
  void statExchange::down(long long int bytes) {
    htobll(data + 16, bytes);
  }

  ///\brief Gets the amount of bytes received
  long long int statExchange::down() {
    long long int result;
    btohll(data + 16, result);
    return result;
  }

  ///\brief Sets the amount of bytes sent
  void statExchange::up(long long int bytes) {
    htobll(data + 24, bytes);
  }

  ///\brief Gets the amount of bytes sent
  long long int statExchange::up() {
    long long int result;
    btohll(data + 24, result);
    return result;
  }

  ///\brief Sets the host of this connection
  void statExchange::host(std::string name) {
    memcpy(data + 32, name.c_str(), std::min((int)name.size(), 16));
  }

  ///\brief Gets the host of this connection
  std::string statExchange::host() {
    return std::string(data + 32, std::min((int)strlen(data + 32), 16));
  }

  ///\brief Sets the name of the stream this user is viewing
  void statExchange::streamName(std::string name) {
    memcpy(data + 48, name.c_str(), std::min((int)name.size(), 20));
  }

  ///\brief Gets the name of the stream this user is viewing
  std::string statExchange::streamName() {
    return std::string(data + 48, std::min((int)strlen(data + 48), 20));
  }

  ///\brief Sets the name of the connector through which this user is viewing
  void statExchange::connector(std::string name) {
    memcpy(data + 68, name.c_str(), std::min((int)name.size(), 20));
  }

  ///\brief Gets the name of the connector through which this user is viewing
  std::string statExchange::connector() {
    return std::string(data + 68, std::min((int)strlen(data + 68), 20));
  }

  ///\brief Creates a semaphore guard, locks the semaphore on call
  semGuard::semGuard(semaphore thisSemaphore) : mySemaphore(thisSemaphore) {
    mySemaphore.wait();
  }

  ///\brief Destructs a semaphore guard, unlocks the semaphore on call
  semGuard::~semGuard() {
    mySemaphore.post();
  }

  ///\brief Default constructor, erases all the values
  sharedServer::sharedServer() {
    payLen = 0;
    hasCounter = false;
    amount = 0;
  }

  ///\brief Desired constructor, initializes after cleaning.
  ///\param name The basename of this server
  ///\param len The lenght of the payload
  ///\param withCounter Whether the content should have a counter
  sharedServer::sharedServer(std::string name, int len, bool withCounter) {
    sharedServer();
    init(name, len, withCounter);
  }

  ///\brief Initialize the server
  ///\param name The basename of this server
  ///\param len The lenght of the payload
  ///\param withCounter Whether the content should have a counter
  void sharedServer::init(std::string name, int len, bool withCounter) {
    amount = 0;
    if (mySemaphore) {
      mySemaphore.close();
    }
    if (baseName != "") {
      mySemaphore.unlink();
    }
    myPages.clear();
    baseName = name;
    payLen = len;
    hasCounter = withCounter;
    mySemaphore.open(std::string("/" + baseName).c_str(), O_CREAT | O_EXCL | O_RDWR, ACCESSPERMS, 1);
    if (!mySemaphore) {
      mySemaphore.open(std::string("/" + baseName).c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    }
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    newPage();
    newPage();
    newPage();
    newPage();
    newPage();
  }

  ///\brief The deconstructor
  sharedServer::~sharedServer() {
    mySemaphore.close();
    mySemaphore.unlink();
  }

  ///\brief Determines whether a sharedServer is valid
  sharedServer::operator bool() const {
    return myPages.size();
  }

  ///\brief Creates the next page with the correct size
  void sharedServer::newPage() {
    semGuard tmpGuard(mySemaphore);
    sharedPage tmp(std::string(baseName + (char)(myPages.size() + (int)'A')), (4096 << myPages.size()), true);
    myPages.insert(tmp);
    tmp.master = false;
    DEBUG_MSG(DLVL_MEDIUM, "Added a new page: %s", tmp.name.c_str());
  }

  ///\brief Deletes the highest allocated page
  void sharedServer::deletePage() {
    if (myPages.size() == 1) {
      DEBUG_MSG(DLVL_WARN, "Can't remove last page for %s", baseName.c_str());
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPages.erase((*myPages.rbegin()));
  }

  ///\brief Determines whether an id is currently in use or not
  bool sharedServer::isInUse(unsigned int id) {
    unsigned int i = 0;
    for (std::set<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      //return if we reached the end
      if (!it->mapped || !it->len) {
        return false;
      }
      //not on this page? skip to next.
      if (it->len < (id - i)*payLen) {
        i += it->len / payLen;
        continue;
      }
      if (hasCounter) {
        //counter? return true if it is non-zero.
        return (it->mapped[(id - i) * payLen] != 0);
      } else {
        //no counter - check the entire size for being all zeroes.
        for (unsigned int j = 0; j < payLen; ++j) {
          if (it->mapped[(id - i)*payLen + j]) {
            return true;
          }
        }
        return false;
      }
    }
    //only happens if we run out of pages
    return false;
  }

  ///\brief Parse each of the possible payload pieces, and runs a callback on it if in use.
  void sharedServer::parseEach(void (*callback)(char * data, size_t len, unsigned int id)) {
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      memset(empty, 0, payLen);
    }
    unsigned int id = 0;
    for (std::set<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      if (!it->mapped || !it->len) {
        DEBUG_MSG(DLVL_FAIL, "Something went terribly wrong?");
        break;
      }
      unsigned int offset = 0;
      while (offset + payLen + (hasCounter ? 1 : 0) <= it->len) {
        if (hasCounter) {
          if (it->mapped[offset] != 0) {
            int counter = it->mapped[offset];
            //increase the count if needed
            if (id >= amount) {
              amount = id + 1;
              DEBUG_MSG(DLVL_VERYHIGH, "Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            callback(it->mapped + offset + 1, payLen, id);
            switch (counter) {
              case 127:
                DEBUG_MSG(DLVL_HIGH, "Client %u requested disconnect", id);
                break;
              case 126:
                DEBUG_MSG(DLVL_WARN, "Client %u timed out", id);
                break;
              case 255:
                DEBUG_MSG(DLVL_HIGH, "Client %u disconnected on request", id);
                break;
              case 254:
                DEBUG_MSG(DLVL_WARN, "Client %u disconnect timed out", id);
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
          } else {
            //stop if we're past the amount counted and we're empty
            if (id >= amount - 1) {
              //bring the counter down if this was the last element
              if (id == amount - 1) {
                amount = id;
                DEBUG_MSG(DLVL_VERYHIGH, "Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              return;
            }
          }
        } else {
          if (memcmp(empty, it->mapped + offset, payLen)) {
            //increase the count if needed
            if (id >= amount) {
              amount = id + 1;
              DEBUG_MSG(DLVL_VERYHIGH, "Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            callback(it->mapped + offset, payLen, id);
          } else {
            //stop if we're past the amount counted and we're empty
            if (id >= amount - 1) {
              //bring the counter down if this was the last element
              if (id == amount - 1) {
                amount = id;
                DEBUG_MSG(DLVL_VERYHIGH, "Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              if (empty) {
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
    if (empty) {
      free(empty);
    }
  }

  ///\brief Creates an empty shared client
  sharedClient::sharedClient() {
    hasCounter = 0;
    payLen = 0;
    offsetOnPage = 0;
  }

  ///\brief Copy constructor for sharedClients
  ///\param rhs The client ro copy
  sharedClient::sharedClient(const sharedClient & rhs) {
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPage.init(rhs.myPage.name, rhs.myPage.len, rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  ///\brief Assignment operator
  void sharedClient::operator =(const sharedClient & rhs) {
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    semGuard tmpGuard(mySemaphore);
    myPage.init(rhs.myPage.name, rhs.myPage.len, rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  ///\brief SharedClient Constructor, allocates space on the correct page.
  ///\param name The basename of the server to connect to
  ///\param len The size of the payload to allocate
  ///\param withCounter Whether or not this payload has a counter
  sharedClient::sharedClient(std::string name, int len, bool withCounter) : baseName(name), payLen(len), offsetOnPage(-1), hasCounter(withCounter) {
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(std::string("/" + baseName).c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    semGuard tmpGuard(mySemaphore);
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      if (!empty) {
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

  ///\brief The deconstructor
  sharedClient::~sharedClient() {
    if (hasCounter) {
      finish();
    }
    mySemaphore.close();
  }

  ///\brief Writes data to the shared data
  void sharedClient::write(char * data, int len) {
    if (hasCounter) {
      keepAlive();
    }
    memcpy(myPage.mapped + offsetOnPage + (hasCounter ? 1 : 0), data, std::min(len, payLen));
  }

  ///\brief Indicate that the process is done using this piece of memory, set the counter to finished
  void sharedClient::finish() {
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to time-out an element without counters");
      return;
    }
    if (myPage.mapped) {
      myPage.mapped[offsetOnPage] = 127;
    }
  }

  ///\brief Re-initialize the counter
  void sharedClient::keepAlive() {
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to keep-alive an element without counters");
      return;
    }
    if (myPage.mapped[offsetOnPage] < 128) {
      myPage.mapped[offsetOnPage] = 1;
    } else {
      DEBUG_MSG(DLVL_WARN, "Trying to keep-alive an element that needs to timeout, ignoring");
    }
  }

  ///\brief Get a pointer to the data of this client
  char * sharedClient::getData() {
    if (!myPage.mapped) {
      return 0;
    }
    return (myPage.mapped + offsetOnPage + (hasCounter ? 1 : 0));
  }
}

