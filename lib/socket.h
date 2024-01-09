/// \file socket.h
/// A handy Socket wrapper library.
/// Written by Jaron Vietor in 2010 for DDVTech

#pragma once
#include <arpa/inet.h>
#include <deque>
#include <errno.h>
#include <fcntl.h>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "util.h"

#ifdef SSL
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/debug.h"
#include "mbedtls/entropy.h"
#include "mbedtls/error.h"
#include "mbedtls/net.h"
#include "mbedtls/ssl.h"
#endif

#include "util.h"

// for being friendly with Socket::Connection down below
namespace Buffer{
  class user;
}

/// Holds Socket tools.
namespace Socket{

  void hostBytesToStr(const char *bytes, size_t len, std::string &target);
  bool isBinAddress(const std::string &binAddr, std::string matchTo);
  bool matchIPv6Addr(const std::string &A, const std::string &B, uint8_t prefix);
  std::string getBinForms(std::string addr);
  /// Returns true if given human-readable address (address, not hostname) is a local address.
  bool isLocal(const std::string &host);
  /// Returns true if given human-readable hostname is a local address.
  bool isLocalhost(const std::string &host);
  bool checkTrueSocket(int sock);
  std::string resolveHostToBestExternalAddrGuess(const std::string &host, int family = AF_UNSPEC,
                                                 const std::string &hint = "");
  bool getSocketName(int fd, std::string &host, uint32_t &port);
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
    unsigned int bytes(unsigned int max);
    unsigned int bytesToSplit();
    void append(const std::string &newdata);
    void append(const char *newdata, const unsigned int newdatasize);
    void prepend(const std::string &newdata);
    void prepend(const char *newdata, const unsigned int newdatasize);
    std::string &get();
    bool available(unsigned int count);
    bool available(unsigned int count) const;
    std::string remove(unsigned int count);
    void remove(Util::ResizeablePointer & ptr, unsigned int count);
    std::string copy(unsigned int count);
    void clear();
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
    uint64_t up;
    uint64_t down;
    long long int conntime;
    Buffer downbuffer;                                ///< Stores temporary data coming in.
    int iread(void *buffer, int len, int flags = 0);  ///< Incremental read call.
    bool iread(Buffer &buffer, int flags = 0); ///< Incremental write call that is compatible with Socket::Buffer.
    void setBoundAddr();

  protected:
    std::string lastErr; ///< Stores last error, if any.
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
    void open(std::string hostname, int port, bool nonblock, bool with_ssl = false); // Open TCP connection.
    void open(std::string adres, bool nonblock = false); // Open Unix connection.
    void open(int write, int read);                      // Open from two existing file descriptors.
    void close();                                        ///< Close connection.
    void drop();                                         ///< Close connection without shutdown.
    void setBlocking(bool blocking); ///< Set this socket to be blocking (true) or nonblocking (false).
    bool isBlocking(); ///< Check if this socket is blocking (true) or nonblocking (false).
    std::string getHost() const; ///< Gets hostname for connection, if available.
    std::string getBinHost() const;
    void setHost(std::string host); ///< Sets hostname for connection manually.
    std::string getBoundAddress() const;
    int getSocket();        ///< Returns internal socket number.
    int getPureSocket();    ///< Returns non-piped internal socket number.
    std::string getError(); ///< Returns a string describing the last error that occured.
    bool connected() const; ///< Returns the connected-state for this socket.
    bool isAddress(const std::string &addr);
    bool isLocal(); ///< Returns true if remote address is a local address.
    // buffered i/o methods
    bool spool(bool strictMode = false);                   ///< Updates the downbufferinternal variables.
    bool peek();                    ///< Clears the downbuffer and fills it with peek
    Buffer &Received();             ///< Returns a reference to the download buffer.
    const Buffer &Received() const; ///< Returns a reference to the download buffer.
    void SendNow(const std::string &data); ///< Will not buffer anything but always send right away. Blocks.
    void SendNow(const char *data); ///< Will not buffer anything but always send right away. Blocks.
    void SendNow(const char *data,
                 size_t len); ///< Will not buffer anything but always send right away. Blocks.
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
    bool Error;    ///< Set to true if a socket error happened.
    bool Blocking; ///< Set to true if a socket is currently or wants to be blocking.
    // overloaded operators
    bool operator==(const Connection &B) const;
    bool operator!=(const Connection &B) const;
    operator bool() const;
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class Server{
  private:
    std::string errors; ///< Stores errors that may have occured.
    int sock;           ///< Internally saved socket number.
    bool IPv6bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv6 socket
    bool IPv4bind(int port, std::string hostname, bool nonblock); ///< Attempt to bind an IPv4 socket
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
  };

  class UDPConnection{
  private:
    int sock;                   ///< Internally saved socket number.
    std::string remotehost;     ///< Stores remote host address
    void *destAddr;             ///< Destination address pointer.
    unsigned int destAddr_size; ///< Size of the destination address pointer.
    unsigned int up;            ///< Amount of bytes transferred up.
    unsigned int down;          ///< Amount of bytes transferred down.
    int family;                 ///< Current socket address family
    std::string boundAddr, boundMulti;
    int boundPort;
    void checkRecvBuf();
    std::deque<Util::ResizeablePointer> paceQueue;
    uint64_t lastPace;

  public:
    Util::ResizeablePointer data;
    UDPConnection(const UDPConnection &o);
    UDPConnection(bool nonblock = false);
    ~UDPConnection();
    void close();
    int getSock();
    uint16_t bind(int port, std::string iface = "", const std::string &multicastAddress = "");
    void setBlocking(bool blocking);
    void allocateDestination();
    void SetDestination(std::string hostname, uint32_t port);
    void GetDestination(std::string &hostname, uint32_t &port);
    std::string getBinDestination();
    const void * getDestAddr(){return destAddr;}
    size_t getDestAddrLen(){return destAddr_size;}
    std::string getBoundAddress();
    uint32_t getDestPort() const;
    bool Receive();
    void SendNow(const std::string &data);
    void SendNow(const char *data);
    void SendNow(const char *data, size_t len);
    void sendPaced(const char * data, size_t len);
    void sendPaced(uint64_t uSendWindow);
    void setSocketFamily(int AF_TYPE);
  };
}// namespace Socket
