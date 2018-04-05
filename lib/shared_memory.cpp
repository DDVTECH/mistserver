#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <unistd.h>
#include <iostream>
#include "defines.h"
#include "shared_memory.h"
#include "stream.h"
#include "procs.h"
#include "bitfields.h"
#include "timing.h"
#include "auth.h"

#if defined(__CYGWIN__) || defined(_WIN32)
#include <windows.h>
#include <aclapi.h>
#include <accctrl.h>
#endif


/// Forces a disconnect to all users.
static void killStatistics(char * data, size_t len, unsigned int id){
  (*(data - 1)) = 60 | ((*(data - 1))&0x80);//Send disconnect message;
}

namespace IPC {

#if defined(__CYGWIN__) || defined(_WIN32)
  static std::map<std::string, sharedPage> preservedPages;
  void preservePage(std::string p) {
    preservedPages[p].init(p, 0, false, false);
  }
  void releasePage(std::string p) {
    preservedPages.erase(p);
  }
#endif

  /// Stores a long value of val in network order to the pointer p.
  static void htobl(char * p, long val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
  }

  /// Stores a short value of val in network order to the pointer p.
  static void htobs(char * p, short val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
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

  /// Reads a short value of p in host order to val.
  static void btohs(char * p, unsigned short & val) {
    val = ((short)p[0] << 8) | p[1];
  }

  /// Reads a long value of p in host order to val.
  static void btohl(char * p, unsigned int & val) {
    val = ((long)p[0] << 24) | ((long)p[1] << 16) | ((long)p[2] << 8) | p[3];
  }

  /// Reads a long long value of p in host order to val.
  static void btohll(char * p, long long & val) {
    val = ((long long)p[0] << 56) | ((long long)p[1] << 48) | ((long long)p[2] << 40) | ((long long)p[3] << 32) | ((long long)p[4] << 24) | ((long long)p[5] << 16) | ((long long)p[6] << 8) | p[7];
  }

  ///\brief Empty semaphore constructor, clears all values
  semaphore::semaphore() {
#if defined(__CYGWIN__) || defined(_WIN32)
    mySem = 0;
#else
    mySem = SEM_FAILED;
#endif
  }

  ///\brief Constructs a named semaphore
  ///\param name The name of the semaphore
  ///\param oflag The flags with which to open the semaphore
  ///\param mode The mode in which to create the semaphore, if O_CREAT is given in oflag, ignored otherwise
  ///\param value The initial value of the semaphore if O_CREAT is given in oflag, ignored otherwise
  semaphore::semaphore(const char * name, int oflag, mode_t mode, unsigned int value, bool noWait) {
#if defined(__CYGWIN__) || defined(_WIN32)
    mySem = 0;
#else
    mySem = SEM_FAILED;
#endif
    open(name, oflag, mode, value, noWait);
  }

  ///\brief The deconstructor
  semaphore::~semaphore() {
    close();
  }


  ///\brief Returns whether we have a valid semaphore
  semaphore::operator bool() const {
#if defined(__CYGWIN__) || defined(_WIN32)
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
  void semaphore::open(const char * name, int oflag, mode_t mode, unsigned int value, bool noWait) {
    close();
    int timer = 0;
    while (!(*this) && timer++ < 10) {
#if defined(__CYGWIN__) || defined(_WIN32)
      std::string semaName = "Global\\";
      semaName += (name+1);
      if (oflag & O_CREAT) {
        if (oflag & O_EXCL) {
          //attempt opening, if succes, close handle and return false;
          HANDLE tmpSem = OpenMutex(SYNCHRONIZE, false, semaName.c_str());
          if (tmpSem) {
            CloseHandle(tmpSem);
            mySem = 0;
            break;
          }
        }
        SECURITY_ATTRIBUTES security = getSecurityAttributes();
        mySem = CreateMutex(&security, true, semaName.c_str());
        if (value){
          ReleaseMutex(mySem);
        }
      } else {
        mySem = OpenMutex(SYNCHRONIZE, false, semaName.c_str());
      }
      if (!(*this)) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND && !noWait){//Error code 2
          Util::wait(500);
        } else {
          break;
        }
      }
#else
      if (oflag & O_CREAT) {
        mySem = sem_open(name, oflag, mode, value);
      } else {
        mySem = sem_open(name, oflag);
      }
      if (!(*this)) {
        if (errno == ENOENT && !noWait) {
          Util::wait(500);
        } else {
          break;
        }
      }
#endif
    }
    if (!(*this)) {
    }
    myName = (char *)name;
  }

  ///\brief Returns the current value of the semaphore
  int semaphore::getVal() const {
#if defined(__CYGWIN__) || defined(_WIN32)
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
#if defined(__CYGWIN__) || defined(_WIN32)
      ReleaseMutex(mySem);
#else
      sem_post(mySem);
#endif
    }
  }

  ///\brief Waits for the semaphore, decreases its value by one
  void semaphore::wait() {
    if (*this) {
#if defined(__CYGWIN__) || defined(_WIN32)
      WaitForSingleObject(mySem, INFINITE);
#else
      int tmp;
      do {
        tmp = sem_wait(mySem);
      } while (tmp == -1 && errno == EINTR);
#endif
    }
  }

  ///\brief Tries to wait for the semaphore, returns true if successful, false otherwise
  bool semaphore::tryWait() {
    if (!(*this)){return false;}
    int result;
#if defined(__CYGWIN__) || defined(_WIN32)
    result = WaitForSingleObject(mySem, 0);//wait at most 1ms
    if (result == 0x80) {
      WARN_MSG("Consistency error caught on semaphore %s", myName.c_str());
      result = 0;
    }
#else
    do {
      result = sem_trywait(mySem);
    } while (result == -1 && errno == EINTR);
#endif
    return (result == 0);
  }

  ///\brief Tries to wait for the semaphore for a single second, returns true if successful, false otherwise
  bool semaphore::tryWaitOneSecond() {
    if (!(*this)){return false;}
    int result;
#if defined(__CYGWIN__) || defined(_WIN32)
    result = WaitForSingleObject(mySem, 1000);//wait at most 1s
    if (result == 0x80) {
      WARN_MSG("Consistency error caught on semaphore %s", myName.c_str());
      result = 0;
    }
#elif defined(__APPLE__)
    /// \todo (roxlu) test tryWaitOneSecond, shared_memory.cpp
    long long unsigned int now = Util::getMicros();
    long long unsigned int timeout = now + 1e6;
    while (now < timeout) {
      if (0 == sem_trywait(mySem)) {
        return true;
      }
      usleep(100e3);
      now = Util::getMicros();
    }
    return false;
#else
    struct timespec wt;
    wt.tv_sec = 1;
    wt.tv_nsec = 0;
    result = sem_timedwait(mySem, &wt);
#endif
    return (result == 0);
  }

  ///\brief Closes the currently opened semaphore
  void semaphore::close() {
    if (*this) {
#if defined(__CYGWIN__) || defined(_WIN32)
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
#if !defined(__CYGWIN__) && !defined(_WIN32)
    if (myName.size()) {
      sem_unlink(myName.c_str());
    }
#endif
    myName.clear();
  }


#if defined(__CYGWIN__) || defined(_WIN32)
  SECURITY_ATTRIBUTES semaphore::getSecurityAttributes() {
    ///\todo We really should clean this up sometime probably
    ///We currently have everything static, because the result basically depends on everything
    static SECURITY_ATTRIBUTES result;
    static bool resultValid = false;
    static SECURITY_DESCRIPTOR securityDescriptor;
    if (resultValid) {
      return result;
    }

    InitializeSecurityDescriptor(&securityDescriptor, SECURITY_DESCRIPTOR_REVISION);
    if (!SetSecurityDescriptorDacl(&securityDescriptor, TRUE, NULL, FALSE)){
      FAIL_MSG("Failed to set pSecurityDescriptor: %u", GetLastError());
      return result;
    }

    result.nLength = sizeof(SECURITY_ATTRIBUTES);
    result.lpSecurityDescriptor = &securityDescriptor;
    result.bInheritHandle = FALSE;

    resultValid = true;
    return result;
  }
#endif

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
    close();
  }

  /// Returns true if the open file still exists.
  /// \TODO Not implemented under Windows.
  bool sharedPage::exists(){
#if defined(__CYGWIN__) || defined(_WIN32)
    return true;//Not implemented under Windows...
#else
#ifdef SHM_ENABLED
    struct stat sb;
    if (fstat(handle, &sb)){return false;}
    return (sb.st_nlink > 0);
#else
    return true;
#endif
#endif
  }

#ifdef SHM_ENABLED
  ///\brief Unmaps a shared page if allowed
  void sharedPage::unmap() {
    if (mapped && len) {
#if defined(__CYGWIN__) || defined(_WIN32)
      //under Cygwin, the mapped location is shifted by 4 to contain the page size.
      UnmapViewOfFile(mapped - 4);
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
      INSANE_MSG("Closing page %s in %s mode", name.c_str(), master ? "master" : "client");
#if defined(__CYGWIN__) || defined(_WIN32)
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

  ///\brief Returns whether the shared page is valid or not
  sharedPage::operator bool() const {
    return mapped != 0;
  }

  ///\brief Assignment operator
  void sharedPage::operator =(sharedPage & rhs) {
    init(rhs.name, rhs.len, rhs.master);
    /// \todo This is bad. The assignment operator changes the rhs value? What the hell?
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
      INSANE_MSG("Opening page %s in %s mode %s auto-backoff", name.c_str(), master ? "master" : "client", autoBackoff ? "with" : "without");
#if defined(__CYGWIN__) || defined(_WIN32)
      if (master) {
        //Under cygwin, all pages are 4 bytes longer than claimed.
        handle = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, len + 4, name.c_str());
      } else {
        int i = 0;
        do {
          if (i != 0) {
            Util::wait(1000);
          }
          handle = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
          i++;
        } while (i < 10 && !handle && autoBackoff);
      }
      if (!handle) {
        MEDIUM_MSG("%s for page %s failed with error code %u", (master ? "CreateFileMapping" : "OpenFileMapping"), name.c_str(), GetLastError());
        return;
      }
      mapped = (char *)MapViewOfFile(handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
      if (!mapped) {
        FAIL_MSG("MapViewOfFile for page %s failed with error code %u", name.c_str(), GetLastError());
        return;
      }
      //Under cygwin, the extra 4 bytes contain the real size of the page.
      if (master) {
        ((unsigned int *)mapped)[0] = len_;
      } else {
        len = ((unsigned int *)mapped)[0];
      }
      //Now shift by those 4 bytes.
      mapped += 4;
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
            Util::wait(1000);
            handle = shm_open(name.c_str(), O_RDWR, ACCESSPERMS);
          }
        }
      }
      if (handle == -1) {
        if (!master_ && autoBackoff) {
          HIGH_MSG("shm_open for page %s failed: %s", name.c_str(), strerror(errno));
        }
        return;
      }
      if (master) {
        if (ftruncate(handle, len) < 0) {
          FAIL_MSG("truncate to %lld for page %s failed: %s", len, name.c_str(), strerror(errno));
          return;
        }
      } else {
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0) {
          return;
        }
        len = buffStats.st_size;
        if (!len){
          mapped = 0;
          return;
        }
      }
      mapped = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED) {
        FAIL_MSG("mmap for page %s failed: %s", name.c_str(), strerror(errno));
        mapped = 0;
        return;
      }
#endif
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

  /// Unmaps, closes and unlinks (if master and name is set) the shared file.
  void sharedFile::close() {
    unmap();
    if (handle > 0) {
      ::close(handle);
      if (master && name != "") {
        unlink(name.c_str());
      }
      handle = 0;
    }
  }

  ///\brief Initialize a page, de-initialize before if needed
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  void sharedFile::init(std::string name_, unsigned int len_, bool master_, bool autoBackoff) {
    close();
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
            Util::wait(1000);
            handle = open(std::string(Util::getTmpFolder() + name).c_str(), O_RDWR, (mode_t)0600);
          }
        }
      }
      if (handle == -1) {
        HIGH_MSG("shf_open for page %s failed: %s", name.c_str(), strerror(errno));
        return;
      }
      if (master) {
        if (ftruncate(handle, len) < 0) {
          INFO_MSG("ftruncate to len for shf page %s failed: %s", name.c_str(), strerror(errno));
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
    close();
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

  /// Calculates session ID from CRC, stream name, connector and host.
  std::string statExchange::getSessId(){
    return Secure::md5(data+32, 140);
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
    if (name.size() < 16) {
      memset(data + 32, 0, 16);
    }
    memcpy(data + 32, name.c_str(), std::min((int)name.size(), 16));
  }

  ///\brief Gets the host of this connection
  std::string statExchange::host() {
    return std::string(data + 32, 16);
  }

  ///\brief Sets the name of the stream this user is viewing
  void statExchange::streamName(std::string name) {
    size_t splitChar = name.find_first_of("+ ");
    if (splitChar != std::string::npos) {
      name[splitChar] = '+';
    }
    snprintf(data+48, 100, "%s", name.c_str());
  }

  ///\brief Gets the name of the stream this user is viewing
  std::string statExchange::streamName() {
    return std::string(data + 48, strnlen(data + 48, 100));
  }

  ///\brief Sets the name of the connector through which this user is viewing
  void statExchange::connector(std::string name) {
    snprintf(data+148, 20, "%s", name.c_str());
  }

  ///\brief Gets the name of the connector through which this user is viewing
  std::string statExchange::connector() {
    return std::string(data + 148, std::min((int)strlen(data + 148), 20));
  }

  ///\brief Sets checksum field
  void statExchange::crc(unsigned int sum) {
    htobl(data + 168, sum);
  }

  ///\brief Gets checksum field
  unsigned int statExchange::crc() {
    unsigned int result;
    btohl(data + 168, result);
    return result;
  }

  ///\brief Sets checksum field
  void statExchange::setSync(char s) {
    data[172] = s;
  }

  ///\brief Gets checksum field
  char statExchange::getSync() {
    return data[172];
  }

  ///\brief Gets PID field
  uint32_t statExchange::getPID() {
    return *(uint32_t*)(data+173);
  }

  ///\brief Creates a semaphore guard, locks the semaphore on call
  semGuard::semGuard(semaphore * thisSemaphore) : mySemaphore(thisSemaphore) {
    mySemaphore->wait();
  }

  ///\brief Destructs a semaphore guard, unlocks the semaphore on call
  semGuard::~semGuard() {
    mySemaphore->post();
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
    if (mySemaphore) {
      mySemaphore.close();
    }
    if (baseName != "") {
      mySemaphore.unlink();
    }
    myPages.clear();
    baseName = "/" + name;
    payLen = len;
    hasCounter = withCounter;
    mySemaphore.open(baseName.c_str(), O_CREAT | O_EXCL | O_RDWR, ACCESSPERMS, 1);
    if (!mySemaphore) {
      mySemaphore.open(baseName.c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);
    }
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    if (!mySemaphore.tryWaitOneSecond()){
      WARN_MSG("Force unlocking sharedServer semaphore to prevent deadlock");
    }
    mySemaphore.post();
    semGuard tmpGuard(&mySemaphore);
    amount = 0;
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

  ///Sets all currently loaded memory pages to non-master, so they are not cleaned up on destruction, but left behind.
  ///Useful for doing rolling updates and such.
  void sharedServer::abandon(){
    if (!myPages.size()){return;}
    VERYHIGH_MSG("Abandoning %llu memory pages, leaving them behind on purpose", myPages.size());
    for (std::deque<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      (*it).master = false;
    }
  }

  ///\brief Creates the next page with the correct size
  void sharedServer::newPage() {
    sharedPage tmp(std::string(baseName.substr(1) + (char)(myPages.size() + (int)'A')), std::min(((8192 * 2) << myPages.size()), (32 * 1024 * 1024)), false, false);
    if (!tmp.mapped){
      tmp.init(std::string(baseName.substr(1) + (char)(myPages.size() + (int)'A')), std::min(((8192 * 2) << myPages.size()), (32 * 1024 * 1024)), true);
      tmp.master = false;
    }
    myPages.push_back(tmp);
    myPages.back().master = true;
    VERYHIGH_MSG("Created a new page: %s", tmp.name.c_str());
    amount += (32 * 1024 * 1024)*myPages.size();//assume maximum load - we don't want to miss any entries
  }

  ///\brief Deletes the highest allocated page
  void sharedServer::deletePage() {
    if (myPages.size() == 1) {
      DEBUG_MSG(DLVL_WARN, "Can't remove last page for %s", baseName.c_str());
      return;
    }
    myPages.pop_back();
  }

  ///\brief Determines whether an id is currently in use or not
  bool sharedServer::isInUse(unsigned int id) {
    unsigned int i = 0;
    for (std::deque<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
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

  ///Disconnect all connected users, waits at most 2.5 seconds until completed
  void sharedServer::finishEach(){
    if (!hasCounter){
      return;
    }
    unsigned int c = 0;//to prevent eternal loops
    do{
      parseEach(killStatistics);
      Util::wait(250);
    }while(amount>1 && c++ < 10);
  }

  ///Returns a pointer to the data for the given index.
  ///Returns null on error or if index is empty.
  char * sharedServer::getIndex(unsigned int requestId){
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      memset(empty, 0, payLen);
    }
    unsigned int id = 0;
    for (std::deque<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      if (!it->mapped || !it->len) {
        DEBUG_MSG(DLVL_FAIL, "Something went terribly wrong?");
        return 0;
      }
      unsigned int offset = 0;
      while (offset + payLen + (hasCounter ? 1 : 0) <= it->len) {
        if (id == requestId){
          if (hasCounter) {
            if (it->mapped[offset] != 0) {
              return it->mapped + offset + 1;
            }else{
              return 0;
            }
          } else {
            if (memcmp(empty, it->mapped + offset, payLen)) {
              return it->mapped + offset;
            }else{
              return 0;
            }
          }
        }
        offset += payLen + (hasCounter ? 1 : 0);
        id ++;
      }
    }
    return 0;
  }

  ///\brief Parse each of the possible payload pieces, and runs a callback on it if in use.
  void sharedServer::parseEach(void (*activeCallback)(char * data, size_t len, unsigned int id), void (*disconCallback)(char * data, size_t len, unsigned int id)) {
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      memset(empty, 0, payLen);
    }
    unsigned int id = 0;
    unsigned int userCount = 0;
    unsigned int emptyCount = 0;
    unsigned int lastFilled = 0;
    connectedUsers = 0;
    for (std::deque<sharedPage>::iterator it = myPages.begin(); it != myPages.end(); it++) {
      if (!it->mapped || !it->len) {
        DEBUG_MSG(DLVL_FAIL, "Something went terribly wrong?");
        break;
      }
      userCount = 0;
      unsigned int offset = 0;
      while (offset + payLen + (hasCounter ? 1 : 0) <= it->len) {
        if (hasCounter) {
          if (it->mapped[offset] != 0) {
            char * counter = it->mapped + offset;
            //increase the count if needed
            ++userCount;
            if (*counter & 0x80){
              connectedUsers++;
            }
            char countNum = (*counter) & 0x7F;
            lastFilled = id;
            if (id >= amount) {
              amount = id + 1;
              VERYHIGH_MSG("Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            uint32_t tmpPID = *((uint32_t *)(it->mapped + 1 + offset + payLen - 4));
            if (tmpPID > 1 && it->master && !Util::Procs::isRunning(tmpPID) && !(countNum == 126 || countNum == 127)){
              WARN_MSG("process disappeared, timing out. (pid %lu)", tmpPID);
              *counter = 125 | (0x80 & (*counter)); //if process is already dead, instant timeout.
            }
            activeCallback(it->mapped + offset + 1, payLen, id);
            switch (countNum) {
              case 127:
                HIGH_MSG("Client %u requested disconnect", id);
                break;
              case 126:
                HIGH_MSG("Client %u timed out", id);
                break;
              default:
#ifndef NOCRASHCHECK
                if (tmpPID > 1 && it->master) {
                  if (countNum > 10 && countNum < 60) {
                    if (countNum < 30) {
                      if (countNum > 15) {
                        WARN_MSG("Process %d is unresponsive", tmpPID);
                      }
                      Util::Procs::Stop(tmpPID); //soft kill
                    } else {
                      ERROR_MSG("Killing unresponsive process %d", tmpPID);
                      Util::Procs::Murder(tmpPID); //improved kill
                    }
                  }
                  if (countNum > 70) {
                    if (countNum < 90) {
                      if (countNum > 75) {
                        WARN_MSG("Stopping process %d is unresponsive", tmpPID);
                      }
                      Util::Procs::Stop(tmpPID); //soft kill
                    } else {
                      ERROR_MSG("Killing unresponsive stopping process %d", tmpPID);
                      Util::Procs::Murder(tmpPID); //improved kill
                    }
                  }
                }
#endif
                break;
            }
            if (countNum == 127 || countNum == 126){
              semGuard tmpGuard(&mySemaphore);
              if (disconCallback){
                disconCallback(counter + 1, payLen, id);
              }
              memset(counter + 1, 0, payLen);
              *counter = 0;
            } else {
              ++(*counter);
            }
          } else {
            //stop if we're past the amount counted and we're empty
            if (id >= amount) {
              //bring the counter down if this was the last element
              if (lastFilled+1 < amount) {
                amount = lastFilled+1;
                VERYHIGH_MSG("Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              break;
            }
          }
        } else {
          if (memcmp(empty, it->mapped + offset, payLen)) {
            ++userCount;
            //increase the count if needed
            lastFilled = id;
            if (id >= amount) {
              amount = id + 1;
              VERYHIGH_MSG("Shared memory %s is now at count %u", baseName.c_str(), amount);
            }
            activeCallback(it->mapped + offset, payLen, id);
          } else {
            //stop if we're past the amount counted and we're empty
            if (id >= amount) {
              //bring the counter down if this was the last element
              if (lastFilled+1 < amount) {
                amount = lastFilled+1;
                VERYHIGH_MSG("Shared memory %s is now at count %u", baseName.c_str(), amount);
              }
              //stop, we're guaranteed no more pages are full at this point
              if (empty) {
                free(empty);
              }
              break;
            }
          }
        }
        offset += payLen + (hasCounter ? 1 : 0);
        id ++;
      }
      if (userCount == 0) {
        ++emptyCount;
      } else {
        emptyCount = 0;
        std::deque<sharedPage>::iterator tIt = it;
        if (++tIt == myPages.end()){
          bool unsetMaster = !(it->master);
          semGuard tmpGuard(&mySemaphore);
          newPage();
          if (unsetMaster){
            (myPages.end()-1)->master = false;
          }
          it = myPages.end() - 2;
        }
      }
    }

    if (emptyCount > 1) {
      semGuard tmpGuard(&mySemaphore);
      deletePage();
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
    countAsViewer= true;
  }


  ///\brief Copy constructor for sharedClients
  ///\param rhs The client ro copy
  sharedClient::sharedClient(const sharedClient & rhs) {
    countAsViewer = rhs.countAsViewer;
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(baseName.c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(baseName.c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore failed: %s", strerror(errno));
      return;
    }
    myPage.init(rhs.myPage.name, rhs.myPage.len, rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  ///\brief Assignment operator
  void sharedClient::operator =(const sharedClient & rhs) {
    countAsViewer = rhs.countAsViewer;
    baseName = rhs.baseName;
    payLen = rhs.payLen;
    hasCounter = rhs.hasCounter;
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(baseName.c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(baseName.c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating copy of semaphore %s failed: %s", baseName.c_str(), strerror(errno));
      return;
    }
    myPage.init(rhs.myPage.name, rhs.myPage.len, rhs.myPage.master);
    offsetOnPage = rhs.offsetOnPage;
  }

  ///\brief SharedClient Constructor, allocates space on the correct page.
  ///\param name The basename of the server to connect to
  ///\param len The size of the payload to allocate
  ///\param withCounter Whether or not this payload has a counter
  sharedClient::sharedClient(std::string name, int len, bool withCounter) : baseName("/" + name), payLen(len), offsetOnPage(-1), hasCounter(withCounter) {
    countAsViewer = true;
#ifdef __APPLE__
    //note: O_CREAT is only needed for mac, probably
    mySemaphore.open(baseName.c_str(), O_RDWR | O_CREAT, 0);
#else
    mySemaphore.open(baseName.c_str(), O_RDWR);
#endif
    if (!mySemaphore) {
      DEBUG_MSG(DLVL_FAIL, "Creating semaphore %s failed: %s", baseName.c_str(), strerror(errno));
      return;
    }
    //Empty is used to compare for emptyness. This is not needed when the page uses a counter
    char * empty = 0;
    if (!hasCounter) {
      empty = (char *)malloc(payLen * sizeof(char));
      if (!empty) {
        DEBUG_MSG(DLVL_FAIL, "Failed to allocate %u bytes for empty payload!", payLen);
        return;
      }
      memset(empty, 0, payLen);
    }
    uint32_t attempts = 0;
    while (offsetOnPage == -1 && (++attempts) < 20) {
      for (char i = 'A'; i <= 'Z'; i++) {
        myPage.init(baseName.substr(1) + i, (4096 << (i - 'A')), false, false);
        if (!myPage.mapped) {
          break;
        }
        int offset = 0;
        while (offset + payLen + (hasCounter ? 1 : 0) <= myPage.len) {
          if ((hasCounter && myPage.mapped[offset] == 0) || (!hasCounter && !memcmp(myPage.mapped + offset, empty, payLen))) {
            semGuard tmpGuard(&mySemaphore);
            if ((hasCounter && myPage.mapped[offset] == 0) || (!hasCounter && !memcmp(myPage.mapped + offset, empty, payLen))) {
              offsetOnPage = offset;
              if (hasCounter) {
                myPage.mapped[offset] = 1;
                *((uint32_t *)(myPage.mapped + 1 + offset + len - 4)) = getpid();
                HIGH_MSG("sharedClient received ID %d", offsetOnPage/(payLen+1));
              }
              break;
            }
          }
          offset += payLen + (hasCounter ? 1 : 0);
        }
        if (offsetOnPage != -1) {
          break;
        }
      }
      if (offsetOnPage == -1) {
        Util::wait(500);
      }
    }
    if (offsetOnPage == -1){
      FAIL_MSG("Could not register on page for %s", baseName.c_str());
      myPage.close();
    }
    if (empty) {
      free(empty);
    }
  }

  ///\brief The deconstructor
  sharedClient::~sharedClient() {
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
    if (!myPage.mapped) {
      return;
    }
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to time-out an element without counters");
      myPage.close();
      return;
    }
    semGuard tmpGuard(&mySemaphore);
    myPage.mapped[offsetOnPage] = 126 | (countAsViewer?0x80:0);
    HIGH_MSG("sharedClient finished ID %d", offsetOnPage/(payLen+1));
    myPage.close();
  }

  ///\brief Re-initialize the counter
  void sharedClient::keepAlive() {
    if (!hasCounter) {
      DEBUG_MSG(DLVL_WARN, "Trying to keep-alive an element without counters");
      return;
    }
    if (isAlive()){
      myPage.mapped[offsetOnPage] = (countAsViewer ? 0x81 : 0x01);
    }
  }

  bool sharedClient::isAlive() {
    if (!hasCounter) {
      return (myPage.mapped != 0);
    }
    if (myPage.mapped){
      return (myPage.mapped[offsetOnPage] & 0x7F) < 60;
    }
    return false;
  }

  ///\brief Get a pointer to the data of this client
  char * sharedClient::getData() {
    if (!myPage.mapped) {
      return 0;
    }
    return (myPage.mapped + offsetOnPage + (hasCounter ? 1 : 0));
  }
  
  int sharedClient::getCounter() {
    if (!hasCounter){
      return -1;
    }
    if (!myPage.mapped) {
      return 0;
    }
    return *(myPage.mapped + offsetOnPage);
  }

  userConnection::userConnection(char * _data) {
    data = _data;
    if (!data){
      WARN_MSG("userConnection created with null pointer!");
    }
  }

  unsigned long userConnection::getTrackId(size_t offset) const {
    if (offset >= SIMUL_TRACKS) {
      WARN_MSG("Trying to get track id for entry %lu, while there are only %d entries allowed", offset, SIMUL_TRACKS);
      return 0;
    }
    return Bit::btohl(data + (offset * 6));
  }

  void userConnection::setTrackId(size_t offset, unsigned long trackId) const {
    if (offset >= SIMUL_TRACKS) {
      WARN_MSG("Trying to set track id for entry %lu, while there are only %d entries allowed", offset, SIMUL_TRACKS);
      return;
    }
    Bit::htobl(data + (offset * 6), trackId);

  }

  unsigned long userConnection::getKeynum(size_t offset) const {
    if (offset >= SIMUL_TRACKS) {
      WARN_MSG("Trying to get keynum for entry %lu, while there are only %d entries allowed", offset, SIMUL_TRACKS);
      return 0;
    }
    return Bit::btohs(data + (offset * 6) + 4);
  }

  void userConnection::setKeynum(size_t offset, unsigned long keynum) {
    if (offset >= SIMUL_TRACKS) {
      WARN_MSG("Trying to set keynum for entry %lu, while there are only %d entries allowed", offset, SIMUL_TRACKS);
      return;
    }
    Bit::htobs(data + (offset * 6) + 4, keynum);

  }
}

