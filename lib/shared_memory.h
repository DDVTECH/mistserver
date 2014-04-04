#pragma once
#include <string>
#include <set>
#include <semaphore.h>

#include "timing.h"

namespace IPC {
  
  class statExchange {
    public:
      statExchange(char * _data);
      void now(long long int time);
      long long int now();
      void time(long time);
      long time();
      void lastSecond(long time);
      long lastSecond();
      void down(long long int bytes);
      long long int down();
      void up(long long int bytes);
      long long int up();
      void host(std::string name);
      std::string host();
      void streamName(std::string name);
      std::string streamName();
      void connector(std::string name);
      std::string connector();
    private:
      char * data;
  };

  class semGuard {
    public:
      semGuard(sem_t * semaphore);
      ~semGuard();
    private:
      sem_t * mySemaphore;
  };

  class sharedPage{
    public:
      sharedPage(std::string name_ = "", unsigned int len_ = 0, bool master_ = false, bool autoBackoff = true);
      sharedPage(const sharedPage & rhs);
      ~sharedPage();
      operator bool() const;
      void init(std::string name_, unsigned int len_, bool master_ =  false, bool autoBackoff = true);
      void operator =(sharedPage & rhs);
      bool operator < (const sharedPage & rhs) const {
        return name < rhs.name;
      }
      int handle;
      std::string name;
      long long int len;
      bool master;
      char * mapped;
  };

  class sharedFile{
    public:
      sharedFile(std::string name_ = "", unsigned int len_ = 0, bool master_ = false, bool autoBackoff = true);
      sharedFile(const sharedPage & rhs);
      ~sharedFile();
      operator bool() const;
      void init(std::string name_, unsigned int len_, bool master_ =  false, bool autoBackoff = true);
      void operator =(sharedFile & rhs);
      bool operator < (const sharedFile & rhs) const {
        return name < rhs.name;
      }
      int handle;
      std::string name;
      long long int len;
      bool master;
      char * mapped;
  };
  
  class sharedServer{
    public:
      sharedServer();
      sharedServer(std::string name, int len, bool withCounter = false);
      void init(std::string name, int len, bool withCounter = false);
      ~sharedServer();
      void parseEach(void (*callback)(char * data, size_t len, unsigned int id));
      operator bool() const;
      unsigned int amount;
    private:
      bool isInUse(unsigned int id);
      void newPage();
      void deletePage();
      std::string baseName;
      unsigned int payLen;
      std::set<sharedPage> myPages;
      sem_t * mySemaphore;
      bool hasCounter;
  };

  class sharedClient{
    public:
      sharedClient();
      sharedClient(const sharedClient & rhs);
      sharedClient(std::string name, int len, bool withCounter = false);
      void operator = (const sharedClient & rhs);
      ~sharedClient();
      void write(char * data, int len);
      void finish();
      void keepAlive();
      char * getData();
    private:
      std::string baseName;
      sharedPage myPage;
      sem_t * mySemaphore;
      int payLen;
      int offsetOnPage;
      bool hasCounter;
  };
}
