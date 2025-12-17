/// \file socket.h
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#pragma once
#include "url.h"
#include "util.h"

#include <arpa/inet.h>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifdef SSL
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/debug.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>

#if !HAVE_UPSTREAM_MBEDTLS_SRTP
#include <mbedtls/net.h>
#else
#include <mbedtls/net_sockets.h>
#endif

#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_MAJOR == 2
#include <mbedtls/certs.h>
#include <mbedtls/config.h>
#else
#include <mbedtls/build_info.h>
#endif

#endif

// for being friendly with Socket::Connection down below
namespace Buffer{
  class user;
}

/// Holds Socket tools.
namespace Socket{
  class Address {
    private:
      Util::ResizeablePointer addr;

    public:
      Address();
      Address(const std::string & rhs);
      Address(const void *rhs, size_t len = 0);
      Address(const Util::ResizeablePointer & rhs);
      Address(const HTTP::URL & url);
      std::string toString() const;
      sa_family_t family() const;
      bool is4in6() const;
      uint16_t port() const;
      std::string host() const;
      operator bool() const;
      const uint8_t *const ipPtr() const;
      operator sockaddr *() const { return (sockaddr *)(const char *)addr; }
      size_t size() const { return addr.size(); }
      void swap(Socket::Address & rhs);
      void assign(const void *ptr, size_t len = 0);
      bool operator<(const Address & rhs) const;
      bool operator==(const Address & rhs) const;
      Address & operator=(const Address & rhs);
      Address & operator=(const void *rhs);
      void reserve();
      void clear();
      std::string binForm() const;
  };

  const Address unsetAddress;

  std::string sockaddrToString(const sockaddr* A);
  void hostBytesToStr(const char *bytes, size_t len, std::string &target);
  bool isBinAddress(const std::string &binAddr, std::string matchTo);
  bool matchIPv6Addr(const std::string &A, const std::string &B, uint8_t prefix);
  bool compareAddress(const sockaddr* A, const sockaddr* B);
  std::string getBinForms(std::string addr);
  std::deque<Socket::Address> getAddrs(std::string addr, uint16_t port, int family = AF_UNSPEC, bool v4MappedResults = true);
  // Returns a list of local addresses
  void getLocal(std::deque<std::string> & addrs);
  /// Returns true if given human-readable address (address, not hostname) is a local address.
  bool isLocal(const std::string &host);
  /// Returns true if given human-readable hostname is a local address.
  bool isLocalhost(const std::string &host);
  bool checkTrueSocket(int sock);
  std::string resolveHostToBestExternalAddrGuess(const std::string &host, int family = AF_UNSPEC,
                                                 const std::string &hint = "");
  bool getSocketName(int fd, std::string &host, uint32_t &port);
  bool getAddrName(const void * sockaddr, std::string &host, uint32_t &port);
  bool getPeerName(int fd, std::string &host, uint32_t &port);
  bool getPeerName(int fd, std::string &host, uint32_t &port, sockaddr * tmpaddr, socklen_t * addrlen);

  /// A buffer made out of std::string objects that can be efficiently read from and written to.
  class Buffer{
  private:
    std::deque<std::string> data;

  public:
    std::string splitter; ///< String to automatically split on if encountered. \n by default
    Buffer();
    unsigned int size();
    unsigned int bytes(unsigned int max) const;
    unsigned int bytesToSplit() const;
    void append(const std::string &newdata);
    void append(const char *newdata, const unsigned int newdatasize);
    void prepend(const std::string &newdata);
    void prepend(const char *newdata, const unsigned int newdatasize);
    std::string &get();
    bool available(unsigned int count);
    bool available(unsigned int count) const;
    void skip(size_t count);
    std::string remove(size_t count);
    void remove(Util::ResizeablePointer & ptr, size_t count);
    std::string copy(size_t count);
    size_t copy(Util::ResizeablePointer & ptr, size_t count);
    void clear();
    bool compact();
  };
  // Buffer

  /// This class is for easy communicating through sockets, either TCP or Unix.
  /// Internally, sSend and sRecv hold the file descriptor to read/write from/to.
  /// If they are not identical and sRecv is closed but sSend still open, reading from sSend will be attempted.
  class Connection{
  protected:
    void clear(); ///< Clears the internal data structure. Meant only for use in constructors.
    bool isTrueSocket;
    int sSend;                      ///< Write end of socket.
    int sRecv;                      ///< Read end of socket.
    std::string remotehost;         ///< Stores remote host address.
    std::string boundaddr;          ///< Stores bound interface address.
    struct sockaddr_in6 remoteaddr; ///< Stores remote host address.
    std::deque<std::function<void(int)>> closeCallbacks;
    uint64_t up;
    uint64_t down;
    long long int conntime;
    Buffer downbuffer; ///< Stores received data (both in blocking and non-blocking modes)
    Buffer upBuffer; ///< In non-blocking mode, buffers outgoing writes
    int iread(void *buffer, int len, int flags = 0);  ///< Incremental read call.
    bool iread(Buffer &buffer, int flags = 0); ///< Incremental write call that is compatible with Socket::Buffer.
    void setBoundAddr();
    std::string lastErr; ///< Stores last error, if any.
    bool isLocked;
    bool chunkedMode;
    bool blocking;
#ifdef SSL
    /// optional extension that uses mbedtls for SSL
    bool sslConnected;
    int ssl_iread(void *buffer, int len, int flags = 0);  ///< Incremental read call.
    unsigned int ssl_iwrite(const void *buffer, int len); ///< Incremental write call.
    mbedtls_net_context *server_fd;
    mbedtls_entropy_context *entropy;
    mbedtls_ctr_drbg_context *ctr_drbg;
    mbedtls_ssl_context *ssl;
    mbedtls_ssl_config *conf;
#endif

  public:
    // friends
    friend class ::Buffer::user;
    // constructors
    Connection();           ///< Create a new disconnected base socket.
    Connection(int sockNo); ///< Create a new base socket.
    Connection(std::string hostname, int port, bool nonblock, bool with_ssl = false); ///< Create a new TCP socket.
    Connection(std::string adres, bool nonblock = false); ///< Create a new Unix Socket.
    Connection(int write, int read); ///< Simulate a socket using two file descriptors.
    // copy/assignment constructors
    Connection(const Connection &rhs);
    Connection &operator=(const Connection &rhs);
    // destructor
    ~Connection();
    // generic methods
    void open(int sockNo); // Open from existing socket connection.
    void open(std::string host, int port, bool nonblock, bool with_ssl = false, const std::string & hostname = ""); // Open TCP connection.
    void open(std::string adres, bool nonblock = false); // Open Unix connection.
    void open(int write, int read);                      // Open from two existing file descriptors.
#ifdef SSL
    bool sslAccept(mbedtls_ssl_config * sslConf, mbedtls_ctr_drbg_context * dbgCtx);
#endif
    void close();                                        ///< Close connection.
    void drop();                                         ///< Close connection without shutdown.
    bool lock();
    void unlock();
    void onClose(std::function<void(int)> cb);
    void setBlocking(bool blocking); ///< Set this socket to be blocking (true) or nonblocking (false).
    bool isBlocking(); ///< Check if this socket is blocking (true) or nonblocking (false).
    void setChunkedMode(bool isChunked) { chunkedMode = isChunked; }
    bool isChunkedMode() const { return chunkedMode; }
    std::string getHost() const; ///< Gets hostname for connection, if available.
    std::string getBinHost() const;
    void setHost(std::string host); ///< Sets hostname for connection manually.
    std::string getBoundAddress() const;
    int getSocket() const; ///< Returns internal socket number.
    int getPureSocket();    ///< Returns non-piped internal socket number.
    std::string getError(); ///< Returns a string describing the last error that occurred.
    bool connected() const; ///< Returns the connected-state for this socket.
    bool isAddress(const std::string &addr);
    bool isLocal(); ///< Returns true if remote address is a local address.
    // buffered i/o methods
    bool spool(bool strictMode = false);                   ///< Updates the downbufferinternal variables.
    bool peek();                    ///< Clears the downbuffer and fills it with peek
    Buffer &Received();             ///< Returns a reference to the download buffer.
    const Buffer &Received() const; ///< Returns a reference to the download buffer.
    size_t sendingBlocked(size_t max) const;
    void send(const char *data, size_t len);
    void SendNow(const std::string & data);
    void SendNow(const char *data);
    void SendNow(const char *data, size_t len);
    void skipBytes(uint32_t byteCount);
    uint32_t skipCount;
    // unbuffered i/o methods
    unsigned int iwrite(const void *buffer, int len); ///< Incremental write call.
    bool iwrite(std::string &buffer); ///< Write call that is compatible with std::string.
    // stats related methods
    unsigned int connTime(); ///< Returns the time this socket has been connected.
    uint64_t dataUp();       ///< Returns total amount of bytes sent.
    uint64_t dataDown();     ///< Returns total amount of bytes received.
    void resetCounter();     ///< Resets the up/down bytes counter to zero.
    void addUp(const uint32_t i);
    void addDown(const uint32_t i);
    friend class Server;
    bool Error; ///< Set to true if a socket error happened.
    // overloaded operators
    bool operator==(const Connection &B) const;
    bool operator!=(const Connection &B) const;
    operator bool() const;
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class Server{
  private:
    std::string errors; ///< Stores errors that may have occurred.
    int sock;           ///< Internally saved socket number.
    bool IPv6bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv6 socket
    bool IPv4bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv4 socket
    Socket::Address bindAddr;

  public:
    Server();                 ///< Create a new base Server.
    Server(int existingSock); ///< Create a new Server from existing socket.
    Server(int port, std::string hostname, bool nonblock = false); ///< Create a new TCP Server.
    Server(std::string adres, bool nonblock = false);              ///< Create a new Unix Server.
    Connection accept(bool nonblock = false); ///< Accept any waiting connections.
    void setBlocking(bool blocking); ///< Set this socket to be blocking (true) or nonblocking (false).
    bool connected() const;          ///< Returns the connected-state for this socket.
    bool isBlocking(); ///< Check if this socket is blocking (true) or nonblocking (false).
    void close();      ///< Close connection.
    void drop();       ///< Close connection without shutdown.
    int getSocket();   ///< Returns internal socket number.
    const Socket::Address & getBoundAddr() const { return bindAddr; }
  };

  class UDPConnection{
  private:
    void init(bool nonblock, int family = AF_INET6);
    int sock;                   ///< Internally saved socket number
    std::string remotehost;     ///< Stores remote host address
    Socket::Address destAddr; ///< Destination address
    Socket::Address recvAddr; ///< Local address
    Socket::Address bindAddr; ///< Bound address
    unsigned int up;            ///< Amount of bytes transferred up
    unsigned int down;          ///< Amount of bytes transferred down
    int family;                 ///< Current socket address family
    std::string boundMulti;
    void checkRecvBuf();
    std::deque<Util::ResizeablePointer> paceQueue;
    uint64_t lastPace;
    int recvInterface;
    bool hasReceiveData;
    bool isBlocking;
    bool isConnected;
    bool ignoreSendErrors;
    bool pretendReceive; ///< If true, will pretend to have just received the current data buffer on new Receive() call
    bool onData();
   
    // dTLS-related members
    bool hasDTLS; ///< True if dTLS is enabled
    std::deque<Util::ResizeablePointer> nextDTLSRead;
#ifdef SSL
    mbedtls_entropy_context entropy_ctx;
    mbedtls_ctr_drbg_context rand_ctx;
    mbedtls_ssl_context ssl_ctx;
    mbedtls_ssl_config ssl_conf;
    mbedtls_ssl_cookie_ctx cookie_ctx;
    mbedtls_timing_delay_context timer_ctx;
#endif

  public:
    Util::ResizeablePointer data;
    UDPConnection(const UDPConnection &o);
    UDPConnection(bool nonblock = false);
    UDPConnection(const void * dest, size_t destLen, const void * loc, size_t locLen);
    UDPConnection(const Socket::Address & remote, const Socket::Address & local = unsetAddress);
    ~UDPConnection();
    void assimilate(int sock);
    bool operator==(const UDPConnection& b) const;
    bool equals(const Socket::Address & remote, const Socket::Address & local) const;
    operator bool() const;
#ifdef SSL
    void initDTLS(mbedtls_x509_crt *cert, mbedtls_pk_context *key, bool asClient = false);
    void deinitDTLS();
    bool handshakeComplete();
    int dTLSRead(unsigned char *buf, size_t len);
    int dTLSWrite(const unsigned char *buf, size_t len);
    void dTLSReset();
    void handshake();
#endif
    uint64_t intTimer;
    uint64_t finTimer;
    bool wasEncrypted;
    void close();
    int getSock();
    void swapSocket(UDPConnection & other);
    uint16_t bind(int port, std::string iface = "", const std::string &multicastAddress = "");
    uint16_t bind(const Socket::Address *addr, const std::string & multicastAddress = "");
    void setMulticastTTL(size_t ttl);
    bool connect();
    void setBlocking(bool blocking);
    void setIgnoreSendErrors(bool ign);
    void allocateDestination(bool setPktInfo = true);
    void setAddresses(void * dest, size_t destLen, void * loc, size_t locLen);
    void SetDestination(std::string hostname, uint32_t port);
    bool setDestination(const sockaddr *addr, const size_t size);
    bool setDestination(const Socket::Address & addr);
    const Socket::Address & getRemoteAddr() const;
    const Socket::Address & getLocalAddr() const;
    const Socket::Address & getBoundAddr() const;
    bool Receive();
    void SendNow(const std::string &data);
    void SendNow(const char *data);
    void SendNow(const char *data, size_t len);
    void SendNow(const char *sdata, size_t len, sockaddr * dAddr, size_t dAddrLen);
    void sendPaced(const char * data, size_t len, bool encrypt = true);
    void sendPaced(uint64_t uSendWindow);
    size_t timeToNextPace(uint64_t uTime = 0);
    void setSocketFamily(int AF_TYPE);


#ifdef SSL
    // dTLS-related public members
    std::string cipher, remote_key, local_key, remote_salt, local_salt;
#if HAVE_UPSTREAM_MBEDTLS_SRTP
    unsigned char master_secret[48];
    unsigned char randbytes[64];
    mbedtls_tls_prf_types tls_prf_type;
#endif
#endif
  };
}// namespace Socket

std::ostream & operator<<(std::ostream & o, const Socket::Address & a);
