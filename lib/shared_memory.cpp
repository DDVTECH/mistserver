#include "shared_memory.h"

#include "cache.h"
#include "defines.h"
#include "timing.h"
#include "util.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <unistd.h>

namespace IPC{

  ///\brief Empty semaphore constructor, clears all values
  semaphore::semaphore(){
    mySem = SEM_FAILED;
    isLocked = 0;
  }

  ///\brief Constructs a named semaphore
  ///\param name The name of the semaphore
  ///\param oflag The flags with which to open the semaphore
  ///\param mode The mode in which to create the semaphore, if O_CREAT is given in oflag, ignored
  /// otherwise \param value The initial value of the semaphore if O_CREAT is given in oflag,
  /// ignored otherwise
  semaphore::semaphore(const char *name, int oflag, mode_t mode, unsigned int value, bool noWait){
    mySem = SEM_FAILED;
    isLocked = 0;
    open(name, oflag, mode, value, noWait);
  }

  ///\brief The deconstructor
  semaphore::~semaphore(){close();}

  ///\brief Returns whether we have a valid semaphore
  semaphore::operator bool() const{
    return mySem != SEM_FAILED;
  }

  /// Opens a semaphore, closes currently opened semaphore if needed
  ///\param name The name of the semaphore
  ///\param oflag The flags with which to open the semaphore
  ///\param mode The mode in which to create the semaphore, if O_CREAT is given in oflag, ignored
  /// otherwise \param value The initial value of the semaphore if O_CREAT is given in oflag,
  /// ignored otherwise
  void semaphore::open(const char *sname, int oflag, mode_t mode, unsigned int value, bool noWait){
    close();
    char *name = (char*)sname;
    if (strlen(sname) >= IPC_MAX_LEN) {
      name = (char*)malloc(IPC_MAX_LEN + 1);
      memcpy(name, sname, IPC_MAX_LEN);
      name[IPC_MAX_LEN] = 0;
    }
    int timer = 0;
    while (!(*this) && timer++ < 10){
      if (oflag & O_CREAT){
        mySem = sem_open(name, oflag, mode, value);
#if defined(__APPLE__)
        if (!(*this)){
          if (sem_unlink(name) == 0){
            INFO_MSG("Deleted in-use semaphore: %s", name);
            mySem = sem_open(name, oflag, mode, value);
          }
        }
#endif
      }else{
        mySem = sem_open(name, oflag);
      }
      if (!(*this)){
        if (errno == ENOENT && !noWait){
          Util::wait(Util::expBackoffMs(timer-1, 10, 5000));
        }else{
          break;
        }
      }
    }
    if (*this){myName = (char *)name;}
  }

  ///\brief Returns the current value of the semaphore
  int semaphore::getVal() const{
    int res;
    sem_getvalue(mySem, &res);
    return res;
  }

  /// Returns true if the semaphore is currently locked
  bool semaphore::locked() const {
    return isLocked;
  }

  ///\brief Posts to the semaphore, increases its value by one
  void semaphore::post(){
    if (!*this || !isLocked){
      FAIL_MSG("Attempted to unlock a non-locked semaphore: '%s'!", myName.c_str());
#if DEBUG >= DLVL_DEVEL
      BACKTRACE;
#endif
      return;
    }
    sem_post(mySem);
    --isLocked;
#if DEBUG >= DLVL_DEVEL
    if (!isLocked){
      uint64_t micros = Util::getMicros(lockTime);
      if (micros > 10000){
        INFO_MSG("Semaphore %s was locked for %.3f ms", myName.c_str(), (double)micros / 1000.0);
      }
    }
#endif
  }

  ///\brief Posts to the semaphore, increases its value by count
  void semaphore::post(size_t count){
    for (size_t i = 0; i < count; ++i){post();}
  }

  ///\brief Waits for the semaphore, decreases its value by one
  void semaphore::wait(){
    if (*this){
#if DEBUG >= DLVL_DEVEL
      uint64_t preLockTime = Util::getMicros();
#endif
      int tmp;
      do{tmp = sem_wait(mySem);}while (tmp == -1 && errno == EINTR);
#if DEBUG >= DLVL_DEVEL
      lockTime = Util::getMicros();
      if (lockTime - preLockTime > 50000){
        INFO_MSG("Semaphore %s took %.3f ms to lock", myName.c_str(), (double)(lockTime-preLockTime) / 1000.0);
      }
#endif
      ++isLocked;
    }
  }

  ///\brief Waits for the semaphore, decreases its value by count
  void semaphore::wait(size_t count){
    for (size_t i = 0; i < count; ++i){wait();}
  }


  ///\brief Tries to wait for the semaphore, returns true if successful, false otherwise
  bool semaphore::tryWait(){
    if (!(*this)){return false;}
    int result;
    do{result = sem_trywait(mySem);}while (result == -1 && errno == EINTR);
    isLocked += (result == 0 ? 1 : 0);
    if (isLocked == 1){lockTime = Util::getMicros();}
    return isLocked;
  }

  ///\brief Tries to wait for the semaphore for a given amount of ms, returns true if successful, false
  /// otherwise
  bool semaphore::tryWait(uint64_t ms){
    if (!(*this)){return false;}
    int result;
#if defined(__APPLE__)
    /// \todo (roxlu) test tryWaitOneSecond, shared_memory.cpp
    uint64_t now = Util::getMicros();
    uint64_t timeout = now + (ms * 1000);
    while (now < timeout){
      if (!sem_trywait(mySem)) {
        isLocked = 1;
        return true;
      }
      usleep(100e3);
      now = Util::getMicros();
    }
    return false;
#else
    struct timespec wt;
    wt.tv_sec = ms / 1000;
    wt.tv_nsec = ms % 1000;
    result = sem_timedwait(mySem, &wt);
#endif
    return (isLocked = (!result) ? 1 : 0) > 0;
  }

  ///\brief Tries to wait for the semaphore for a single second, returns true if successful, false
  /// otherwise
  bool semaphore::tryWaitOneSecond(){
    if (!(*this)){return false;}
    int result;
#if defined(__APPLE__)
    /// \todo (roxlu) test tryWaitOneSecond, shared_memory.cpp
    uint64_t now = Util::getMicros();
    uint64_t timeout = now + 1e6;
    result = 1;
    while (result && now < timeout){
      result = sem_trywait(mySem);
      usleep(100e3);
      now = Util::getMicros();
    }
#else
    struct timespec wt;
    wt.tv_sec = 1;
    wt.tv_nsec = 0;
    result = sem_timedwait(mySem, &wt);
#endif
    isLocked += (!result) ? 1 : 0;
    if (isLocked == 1){lockTime = Util::getMicros();}
    return isLocked;
  }

  ///\brief Closes the currently opened semaphore
  void semaphore::close(){
    if (*this){
      while (isLocked){post();}
      sem_close(mySem);
      mySem = SEM_FAILED;
    }
    myName.clear();
  }

  /// Closes the semaphore, without unlocking it first.
  /// Intended to be called from forked child processes, to drop the reference to the semaphore.
  void semaphore::abandon(){
    if (*this){
      sem_close(mySem);
      mySem = SEM_FAILED;
    }
    myName.clear();
  }

  /// Unlinks the previously opened semaphore, closing it (if open) in the process.
  void semaphore::unlink(){
    if (myName.size()){sem_unlink(myName.c_str());}
    if (*this){
      sem_close(mySem);
      mySem = SEM_FAILED;
    }
    myName.clear();
  }

  /// Creates a shared page
  ///\param name The name of the page to be created
  ///\param len The size to make the page
  ///\param master Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  ///\param cache Default to true, the page may also be a temporary file on disk
  sharedPage::sharedPage(const std::string & name, uint64_t len, bool master, bool autoBackoff, bool cache)
    : name(name), len(len), master(master), cache(cache) {
    init(autoBackoff);
  }

  /// Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedPage::sharedPage(const sharedPage & rhs) : name(rhs.name), len(rhs.len), master(rhs.master), cache(rhs.cache) {
    init();
  }

  /// Maps an existing file descriptor as if it were a page
  ///\param fd the file descriptor
  sharedPage::sharedPage(const int fd) {
    if (fd == -1) return;
    handle = fd;

    struct stat buffStats;
    int xRes = fstat(handle, &buffStats);
    if (xRes < 0) return;
    len = buffStats.st_size;

    mapped = (char *)mmap(0, len, PROT_READ, MAP_SHARED, handle, 0);
  }

  /// Default destructor
  sharedPage::~sharedPage(){close();}

  /// Returns whether the shared file is valid or not
  sharedPage::operator bool() const {
    return mapped != 0;
  }

  /// Comparison operator overload
  bool sharedPage::operator<(const sharedPage & rhs) const {
    return name < rhs.name;
  }

  /// Move operator overload which re-inits to a new page
  sharedPage & sharedPage::operator=(sharedPage && rhs) {
    if (this == &rhs) return *this;
    close();

    handle = rhs.handle;
    name = std::move(rhs.name);
    len = rhs.len;
    master = rhs.master;
    mapped = rhs.mapped;
    cache = rhs.cache;

    // Avoid memory becoming unlinked
    rhs.handle = -1;
    rhs.mapped = 0;
    rhs.master = false;
    rhs.len = 0;
    return *this;
  }

  /// Returns true if the open file still exists.
  bool sharedPage::exists(){
    struct stat sb;
    return fstat(handle, &sb) ? false : (sb.st_nlink > 0);
  }

  /// Unmaps a shared page if allowed
  void sharedPage::unmap(){
    if (!mapped) return;
    munmap(mapped, len);
    mapped = 0;
    len = 0;
  }

  /// Closes a shared page if allowed
  void sharedPage::close(){
    unmap();
    if (handle < 0) return;
    INSANE_MSG("Closing page %s in %s mode", name.c_str(), master ? "master" : "client");
    ::close(handle);
    handle = -1;

    // Master mode removes the page upon close
    if (master && name != "") {
      if (!cache) {
#ifdef SHM_ENABLED
        shm_unlink(name.c_str());
#else
        unlink(name.c_str());
#endif
      }
    }
  }

  /// Initialises a shared page based on the passed parameters
  void sharedPage::init(const std::string & name_, uint64_t len_, bool master_, bool autoBackoff, bool cache_) {
    close();
    name = name_;
    len = len_;
    master = master_;
    cache = cache_;
    init(autoBackoff);
  }

  /// Initialises a shared page based on the passed parameters
  void sharedPage::init(bool autoBackoff) {
    mapped = 0;
    if (!name.size()) return;
    INSANE_MSG("Opening %s %s in %s mode %s auto-backoff", cache ? "file" : "page", name.c_str(),
               master ? "master" : "client", autoBackoff ? "with" : "without");

    // Lambda for calling the appropriate open function
    auto fnOpen = [&](int oflag, mode_t mode = ACCESSPERMS) -> int {
      if (cache) {
        int fd = Cache::cacheRead(name, autoBackoff);
        if (fd != -1) return fd;
      }

#ifdef SHM_ENABLED
      int fd = shm_open(name.c_str(), oflag, mode);
#else
      int fd = open(std::string(Util::getTmpFolder() + name).c_str(), master ? oflag | O_TRUNC : oflag, mode);
#endif
      // If cache read failed, make sure to set it as such
      if (fd != -1) cache = false;
      return fd;
    };

    // Attempt to open the file with exponential backoff
    handle = fnOpen((master ? O_CREAT | O_EXCL : 0) | O_RDWR);
    if (handle == -1) {
      if (master) {
        if (len > 1) ERROR_MSG("Overwriting old %s for %s", cache ? "file" : "page", name.c_str());
        handle = fnOpen(O_CREAT | O_RDWR);
      } else {
        int i = 0;
        while (i < 11 && handle == -1 && autoBackoff) {
          i++;
          Util::wait(Util::expBackoffMs(i - 1, 10, 10000));
          handle = fnOpen(O_RDWR);
        }
      }
    }

    // Could not open the file
    if (handle == -1) {
      if (!master && autoBackoff) { HIGH_MSG("Open for page %s failed: %s", name.c_str(), strerror(errno)); }
      return;
    }

    // Somehow the page acquired the fd of one of the standard streams, remap it
    if (handle >= 0 && handle < 3) {
      int tmpHandle = fcntl(handle, F_DUPFD, 3);
      if (tmpHandle >= 3) {
        DONTEVEN_MSG("Remapped handle for page %s from %d to %d!", name.c_str(), handle, tmpHandle);
        ::close(handle);
        handle = tmpHandle;
      }
    }

    // Truncate (master) or acquire (attacher) the appropriate size for mmap
    if (master) {
      if (ftruncate(handle, len) < 0) {
        FAIL_MSG("Truncate to %" PRIu64 " for %s %s failed: %s", len, cache ? "file" : "page", name.c_str(), strerror(errno));
        return;
      }
    } else {
      struct stat buffStats;
      int xRes = fstat(handle, &buffStats);
      if (xRes < 0) return;
      len = buffStats.st_size;
      if (!len) {
        mapped = 0;
        return;
      }
    }

    // Finally, map the page into virtual memory
    mapped = (char *)mmap(0, len, cache ? PROT_READ : PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
    if (mapped == MAP_FAILED) {
      FAIL_MSG("Call mmap for page %s failed: %s", name.c_str(), strerror(errno));
      mapped = 0;
      return;
    }
  }

  ///\brief Creates a semaphore guard, locks the semaphore on call
  semGuard::semGuard(semaphore *thisSemaphore) : mySemaphore(thisSemaphore){mySemaphore->wait();}

  ///\brief Destructs a semaphore guard, unlocks the semaphore on call
  semGuard::~semGuard(){mySemaphore->post();}
}// namespace IPC
