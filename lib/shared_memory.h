#pragma once
#include "defines.h"

#include <semaphore.h>
#include <string>
#include <sys/stat.h>

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

  ///\brief A class for handling shared memory pages.
  class sharedPage{
  public:
    sharedPage(const std::string & name = "", uint64_t len = 0, bool master = false, bool autoBackoff = true, bool cache = false);
    sharedPage(const sharedPage &rhs);
    sharedPage(const int fd);
    ~sharedPage();

    operator bool() const;
    bool operator<(const sharedPage & rhs) const;
    sharedPage & operator=(sharedPage && rhs);

    void init(const std::string & name, uint64_t len, bool master = false, bool autoBackoff = true, bool cache = false);
    void init(bool autoBackoff = true);
    void unmap();
    void close();
    bool exists();

    int handle{-1};
    std::string name;
    uint64_t len{0};
    bool master{false};
    char *mapped{nullptr};
    bool isCache() const { return cache; }

  private:
    bool cache{false};
  };
}// namespace IPC
