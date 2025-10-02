#pragma once
#include <string>
#include <sys/stat.h>

#include "defines.h"

#include <semaphore.h>

#ifndef ACCESSPERMS
#define ACCESSPERMS (S_IRWXU | S_IRWXG | S_IRWXO)
#endif

#define STAT_EX_SIZE 177
#define PLAY_EX_SIZE 2 + 6 * SIMUL_TRACKS

namespace IPC{

  ///\brief A class used for the abstraction of semaphores
  class semaphore{
  public:
    semaphore();
    semaphore(const char *name, int oflag, mode_t mode = 0, unsigned int value = 0, bool noWait = false);
    ~semaphore();
    operator bool() const;
    void open(const char *name, int oflag, mode_t mode = 0, unsigned int value = 0, bool noWait = false);
    int getVal() const;
    void post();
    void wait();
    void post(size_t count);
    void wait(size_t count);
    bool tryWait();
    bool tryWait(uint64_t ms);
    bool tryWaitOneSecond();
    bool locked() const;
    void close();
    void abandon();
    void unlink();

  private:
    sem_t *mySem;
    unsigned int isLocked;
    uint64_t lockTime;
    std::string myName;
  };

  ///\brief A class used as a semaphore guard
  class semGuard{
  public:
    semGuard(semaphore *thisSemaphore);
    ~semGuard();

  private:
    ///\brief The semaphore to guard.
    semaphore *mySemaphore;
  };

  ///\brief A class for managing shared files.
  class sharedFile{
  public:
    sharedFile(const std::string &name_ = "", uint64_t len_ = 0, bool master_ = false, bool autoBackoff = true);
    sharedFile(const sharedFile &rhs);
    ~sharedFile();
    operator bool() const;
    void init(const std::string &name_, uint64_t len_, bool master_ = false, bool autoBackoff = true);
    void operator=(sharedFile &rhs);
    bool operator<(const sharedFile &rhs) const{return name < rhs.name;}
    void close();
    void unmap();
    bool exists();
    ///\brief The fd handle of the opened shared file
    int handle;
    ///\brief The name of the opened shared file
    std::string name;
    ///\brief The size in bytes of the opened shared file
    uint64_t len;
    ///\brief Whether this class should unlink the shared file upon deletion or not
    bool master;
    ///\brief A pointer to the payload of the file file
    char *mapped;
  };

#ifdef SHM_ENABLED
  ///\brief A class for managing shared memory pages.
  class sharedPage{
  public:
    sharedPage(const std::string &name_ = "", uint64_t len_ = 0, bool master_ = false, bool autoBackoff = true);
    sharedPage(const sharedPage &rhs);
    ~sharedPage();
    operator bool() const;
    void init(const std::string &name_, uint64_t len_, bool master_ = false, bool autoBackoff = true);
    void operator=(sharedPage &rhs);
    bool operator<(const sharedPage &rhs) const{return name < rhs.name;}
    void unmap();
    void close();
    bool exists();
    ///\brief The fd handle of the opened shared memory page
    int handle;
    ///\brief The name of the opened shared memory page
    std::string name;
    ///\brief The size in bytes of the opened shared memory page
    uint64_t len;
    ///\brief Whether this class should unlink the shared memory upon deletion or not
    bool master;
    ///\brief A pointer to the payload of the page
    char *mapped;
  };
#else
  ///\brief A class for handling shared memory pages.
  /// Uses shared files at its backbone, defined for portability
  class sharedPage : public sharedFile{
  public:
    sharedPage(const std::string &name_ = "", uint64_t len_ = 0, bool master_ = false, bool autoBackoff = true);
    sharedPage(const sharedPage &rhs);
    ~sharedPage();
  };
#endif
}// namespace IPC
