#pragma once
#include <string>
#include <set>

#include "timing.h"

#ifdef __CYGWIN__
#include <windows.h>
#else
#include <semaphore.h>
#endif

namespace IPC {
  
  ///\brief A class used for the exchange of statistics over shared memory.
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
      ///\brief The payload for the stat exchange
      /// - 8 byte - now (timestamp of last statistics)
      /// - 4 byte - time (duration of the current connection) 
      /// - 4 byte - lastSecond (last second of content viewed)
      /// - 8 byte - down (Number of bytes received from peer)
      /// - 8 byte - up (Number of bytes sent to peer)
      /// - 16 byte - host (ip address of the peer)
      /// - 20 byte - streamName (name of the stream peer is viewing)
      /// - 20 byte - connector (name of the connector the peer is using) 
      char * data;
  };

  ///\brief A class used for the abstraction of semaphores
  class semaphore {
    public:
      semaphore();
      semaphore(const char * name, int oflag, mode_t mode, unsigned int value);
      ~semaphore();
      operator bool() const;
      void open(const char * name, int oflag, mode_t mode = 0, unsigned int value = 0);
      int getVal() const;
      void post();
      void wait();
      bool tryWait();
      void close();
      void unlink();
    private:
#ifdef __CYGWIN__
      HANDLE mySem;
#else
      sem_t * mySem;
#endif
      char * myName;
  };

  ///\brief A class used as a semaphore guard
  class semGuard {
    public:
      semGuard(semaphore thisSemaphore);
      ~semGuard();
    private:
      ///\brief The semaphore to guard.
      semaphore mySemaphore;
  };

#if !defined __APPLE__
  ///\brief A class for managing shared memory pages.
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
      void unmap();
      void close();
  #ifdef __CYGWIN__
      ///\brief The handle of the opened shared memory page
      HANDLE handle;
  #else
      ///\brief The fd handle of the opened shared memory page
      int handle;
  #endif
      ///\brief The name of the opened shared memory page
      std::string name;
      ///\brief The size in bytes of the opened shared memory page
      long long int len;
      ///\brief Whether this class should unlink the shared memory upon deletion or not
      bool master;
      ///\brief A pointer to the payload of the page
      char * mapped;
  };
#else
  class sharedPage;
#endif

#if !defined __APPLE__
  ///\brief A class for managing shared files in the same form as shared memory pages
#else
  ///\brief A class for managing shared files.
#endif
  class sharedFile{
    public:
      sharedFile(std::string name_ = "", unsigned int len_ = 0, bool master_ = false, bool autoBackoff = true);
      sharedFile(const sharedFile & rhs);
      ~sharedFile();
      operator bool() const;
      void init(std::string name_, unsigned int len_, bool master_ =  false, bool autoBackoff = true);
      void operator =(sharedFile & rhs);
      bool operator < (const sharedFile & rhs) const {
        return name < rhs.name;
      }
      void unmap();
      ///\brief The fd handle of the opened shared file
      int handle;
      ///\brief The name of the opened shared file
      std::string name;
      ///\brief The size in bytes of the opened shared file
      long long int len;
      ///\brief Whether this class should unlink the shared file upon deletion or not
      bool master;
      ///\brief A pointer to the payload of the file file
      char * mapped;
  };

#ifdef __APPLE__
  ///\brief A class for handling shared memory pages.
  ///
  ///Uses shared files at its backbone, defined for portability
  class sharedPage: public sharedFile{
    public:
      sharedPage(std::string name_ = "", unsigned int len_ = 0, bool master_ = false, bool autoBackoff = true);
      sharedPage(const sharedPage & rhs);
      ~sharedPage();
  };
#endif

  ///\brief The server part of a server/client model for shared memory.
  ///
  ///The server manages the shared memory pages, and allocates new pages when needed.
  ///
  ///Pages are created with a basename + index, where index is in the range of 'A' - 'Z'
  ///Each time a page is nearly full, the next page is created with a size double to the previous one.
  ///
  ///Clients should allocate payLen bytes at a time, possibly with the addition of a counter.
  ///If no such length can be allocated, the next page should be tried, and so on.
  class sharedServer{
    public:
      sharedServer();
      sharedServer(std::string name, int len, bool withCounter = false);
      void init(std::string name, int len, bool withCounter = false);
      ~sharedServer();
      void parseEach(void (*callback)(char * data, size_t len, unsigned int id));
      operator bool() const;
      ///\brief The amount of connected clients
      unsigned int amount;
    private:
      bool isInUse(unsigned int id);
      void newPage();
      void deletePage();
      ///\brief The basename of the shared pages.
      std::string baseName;
      ///\brief The length of each consecutive piece of payload
      unsigned int payLen;
      ///\brief The set of sharedPage structures to manage the actual memory
      std::set<sharedPage> myPages;
      ///\brief A semaphore that is locked upon creation and deletion of the page, to ensure no new data is allocated during this step.
      semaphore mySemaphore;
      ///\brief Whether the payload has a counter, if so, it is added in front of the payload
      bool hasCounter;
  };

  ///\brief The client part of a server/client model for shared memory.
  ///
  ///The server manages the shared memory pages, and allocates new pages when needed.
  ///
  ///Pages are created with a basename + index, where index is in the range of 'A' - 'Z'
  ///Each time a page is nearly full, the next page is created with a size double to the previous one.
  ///
  ///Clients should allocate payLen bytes at a time, possibly with the addition of a counter.
  ///If no such length can be allocated, the next page should be tried, and so on.
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
      ///\brief The basename of the shared pages.
      std::string baseName;
      ///\brief The shared page this client has reserved a space on.
      sharedPage myPage;
      ///\brief A semaphore that is locked upon trying to allocate space on a page
      semaphore mySemaphore;
      ///\brief The size in bytes of the opened page
      int payLen;
      ///\brief The offset of the payload reserved for this client within the opened page
      int offsetOnPage;
      ///\brief Whether the payload has a counter, if so, it is added in front of the payload
      bool hasCounter;
  };
}
