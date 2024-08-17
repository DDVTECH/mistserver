#include "defines.h"
#include "shared_memory.h"
#include "stream.h"
#include "timing.h"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
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

  ///\brief Opens a semaphore
  ///
  /// Closes currently opened semaphore if needed
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
      if (0 == sem_trywait(mySem)){
        isLocked = true;
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
    return (isLocked = (result == 0));
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
    isLocked += (result == 0 ? 1 : 0);
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

  /// brief Creates a shared page
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  sharedPage::sharedPage(const std::string &name_, uint64_t len_, bool master_, bool autoBackoff){
    handle = 0;
    len = 0;
    master = false;
    mapped = 0;
    init(name_, len_, master_, autoBackoff);
  }

  ///\brief Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedPage::sharedPage(const sharedPage &rhs){
    handle = 0;
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }

  ///\brief Default destructor
  sharedPage::~sharedPage(){close();}

#ifdef SHM_ENABLED

  /// Returns true if the open file still exists.
  bool sharedPage::exists(){
    struct stat sb;
    if (fstat(handle, &sb)){return false;}
    return (sb.st_nlink > 0);
  }

  ///\brief Unmaps a shared page if allowed
  void sharedPage::unmap(){
    if (mapped){
      munmap(mapped, len);
      mapped = 0;
      len = 0;
    }
  }

  ///\brief Closes a shared page if allowed
  void sharedPage::close(){
    unmap();
    if (handle > 0){
      INSANE_MSG("Closing page %s in %s mode", name.c_str(), master ? "master" : "client");
      ::close(handle);
      if (master && name != ""){shm_unlink(name.c_str());}
      handle = 0;
    }
  }

  ///\brief Returns whether the shared page is valid or not
  sharedPage::operator bool() const{return mapped != 0;}

  ///\brief Assignment operator
  void sharedPage::operator=(sharedPage &rhs){
    init(rhs.name, rhs.len, rhs.master);
    /// \todo This is bad. The assignment operator changes the rhs value? What the hell?
    rhs.master = false; // Make sure the memory does not get unlinked
  }

  ///\brief Initialize a page, de-initialize before if needed
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  void sharedPage::init(const std::string &name_, uint64_t len_, bool master_, bool autoBackoff){
    close();
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()){
      INSANE_MSG("Opening page %s in %s mode %s auto-backoff", name.c_str(),
                 master ? "master" : "client", autoBackoff ? "with" : "without");
      handle = shm_open(name.c_str(), (master ? O_CREAT | O_EXCL : 0) | O_RDWR, ACCESSPERMS);
      if (handle == -1){
        if (master){
          if (len > 1){ERROR_MSG("Overwriting old page for %s", name.c_str());}
          handle = shm_open(name.c_str(), O_CREAT | O_RDWR, ACCESSPERMS);
        }else{
          int i = 0;
          while (i < 11 && handle == -1 && autoBackoff){
            i++;
            Util::wait(Util::expBackoffMs(i-1, 10, 10000));
            handle = shm_open(name.c_str(), O_RDWR, ACCESSPERMS);
          }
        }
      }
      if (handle == -1){
        if (!master_ && autoBackoff){
          HIGH_MSG("shm_open for page %s failed: %s", name.c_str(), strerror(errno));
        }
        return;
      }
      if (handle >= 0 && handle < 3){
        int tmpHandle = fcntl(handle, F_DUPFD, 3);
        if (tmpHandle >= 3){
          DONTEVEN_MSG("Remapped handle for page %s from %d to %d!", name.c_str(), handle, tmpHandle);
          ::close(handle);
          handle = tmpHandle;
        }
      }
      if (master){
        if (ftruncate(handle, len) < 0){
          FAIL_MSG("truncate to %" PRIu64 " for page %s failed: %s", len, name.c_str(), strerror(errno));
          return;
        }
      }else{
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0){return;}
        len = buffStats.st_size;
        if (!len){
          mapped = 0;
          return;
        }
      }
      mapped = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED){
        FAIL_MSG("mmap for page %s failed: %s", name.c_str(), strerror(errno));
        mapped = 0;
        return;
      }
    }
  }

#endif

  /// brief Creates a shared file
  ///\param name_ The name of the file to be created
  ///\param len_ The size to make the file
  ///\param master_ Whether to create or merely open the file
  ///\param autoBackoff When only opening the file, wait for it to appear or fail
  sharedFile::sharedFile(const std::string &name_, uint64_t len_, bool master_, bool autoBackoff)
      : handle(0), name(name_), len(len_), master(master_), mapped(NULL){
    handle = 0;
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    init(name_, len_, master_, autoBackoff);
  }

  ///\brief Creates a copy of a shared page
  ///\param rhs The page to copy
  sharedFile::sharedFile(const sharedFile &rhs){
    handle = 0;
    name = "";
    len = 0;
    master = false;
    mapped = 0;
    init(rhs.name, rhs.len, rhs.master);
  }

  ///\brief Returns whether the shared file is valid or not
  sharedFile::operator bool() const{return mapped != 0;}

  ///\brief Assignment operator
  void sharedFile::operator=(sharedFile &rhs){
    init(rhs.name, rhs.len, rhs.master);
    rhs.master = false; // Make sure the memory does not get unlinked
  }

  ///\brief Unmaps a shared file if allowed
  void sharedFile::unmap(){
    if (mapped && len){
      munmap(mapped, len);
      mapped = 0;
      len = 0;
    }
  }

  /// Unmaps, closes and unlinks (if master and name is set) the shared file.
  void sharedFile::close(){
    unmap();
    if (handle > 0){
      ::close(handle);
      if (master && name != ""){unlink(std::string(Util::getTmpFolder() + name).c_str());}
      handle = 0;
    }
  }

  /// Returns true if the open file still exists.
  bool sharedFile::exists(){
    struct stat sb;
    if (fstat(handle, &sb)){return false;}
    return (sb.st_nlink > 0);
  }

  ///\brief Initialize a page, de-initialize before if needed
  ///\param name_ The name of the page to be created
  ///\param len_ The size to make the page
  ///\param master_ Whether to create or merely open the page
  ///\param autoBackoff When only opening the page, wait for it to appear or fail
  void sharedFile::init(const std::string &name_, uint64_t len_, bool master_, bool autoBackoff){
    close();
    name = name_;
    len = len_;
    master = master_;
    mapped = 0;
    if (name.size()){
      /// \todo Use ACCESSPERMS instead of 0600?
      handle = open(std::string(Util::getTmpFolder() + name).c_str(),
                    (master ? O_CREAT | O_TRUNC | O_EXCL : 0) | O_RDWR, (mode_t)0600);
      if (handle == -1){
        if (master){
          HIGH_MSG("Overwriting old file for %s", name.c_str());
          handle = open(std::string(Util::getTmpFolder() + name).c_str(),
                        O_CREAT | O_TRUNC | O_RDWR, (mode_t)0600);
        }else{
          int i = 0;
          while (i < 11 && handle == -1 && autoBackoff){
            i++;
            Util::wait(Util::expBackoffMs(i-1, 10, 10000));
            handle = open(std::string(Util::getTmpFolder() + name).c_str(), O_RDWR, (mode_t)0600);
          }
        }
      }
      if (handle == -1){
        HIGH_MSG("shf_open for page %s failed: %s", name.c_str(), strerror(errno));
        return;
      }
      if (master){
        if (ftruncate(handle, len) < 0){
          INFO_MSG("ftruncate to len for shf page %s failed: %s", name.c_str(), strerror(errno));
          return;
        }
      }else{
        struct stat buffStats;
        int xRes = fstat(handle, &buffStats);
        if (xRes < 0){return;}
        len = buffStats.st_size;
      }
      mapped = (char *)mmap(0, len, PROT_READ | PROT_WRITE, MAP_SHARED, handle, 0);
      if (mapped == MAP_FAILED){
        mapped = 0;
        return;
      }
    }
  }

  ///\brief Default destructor
  sharedFile::~sharedFile(){close();}

  ///\brief Creates a semaphore guard, locks the semaphore on call
  semGuard::semGuard(semaphore *thisSemaphore) : mySemaphore(thisSemaphore){mySemaphore->wait();}

  ///\brief Destructs a semaphore guard, unlocks the semaphore on call
  semGuard::~semGuard(){mySemaphore->post();}
}// namespace IPC
