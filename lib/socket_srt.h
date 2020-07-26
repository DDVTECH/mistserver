#pragma once

#include "socket.h"
#include "url.h"

#include <map>
#include <string>

#include <srt/srt.h>

typedef std::map<std::string, int> SockOptVals;
typedef std::map<std::string, std::string> paramList;

namespace Socket{
  std::string interpretSRTMode(const HTTP::URL &u);

  inline bool isFalseString(const std::string &_val){
    return _val == "0" || _val == "no" || _val == "off" || _val == "false";
  }

  inline bool isTrueString(const std::string &_val){
    return _val == "1" || _val == "yes" || _val == "on" || _val == "true";
  }

  sockaddr_in createInetAddr(const std::string &_host, int _port);

  namespace SRT{
    extern bool isInited;
    bool libraryInit();
    bool libraryCleanup();

    // By absence of class enum (c++11), moved enums to a separate namespace
    namespace SockOpt{
      enum Type{STRING = 0, INT, INT64, BOOL, ENUM};
      enum Binding{PRE = 0, POST};
    }// namespace SockOpt
  }// namespace SRT

  class SRTConnection{
  public:
    SRTConnection();
    SRTConnection(SRTSOCKET alreadyConnected){sock = alreadyConnected;}
    SRTConnection(const std::string &_host, int _port, const std::string &_direction = "input",
                  const paramList &_params = paramList());

    void connect(const std::string &_host, int _port, const std::string &_direction = "input",
                 const paramList &_params = paramList());
    void close();
    bool connected() const{return sock != -1;}
    operator bool() const{return connected();}

    void setBlocking(bool blocking); ///< Set this socket to be blocking (true) or nonblocking (false).
    bool isBlocking(); ///< Check if this socket is blocking (true) or nonblocking (false).

    std::string RecvNow();
    void SendNow(const std::string &data);
    void SendNow(const char *data, size_t len);

    SRTSOCKET getSocket(){return sock;}

    int postConfigureSocket();

    std::string getStreamName();

    unsigned int connTime();
    uint64_t dataUp();
    uint64_t dataDown();
    uint64_t packetCount();
    uint64_t packetLostCount();
    uint64_t packetRetransmitCount();

    std::string direction;

    struct sockaddr_in6 remoteaddr;
    std::string remotehost;

  private:
    SRTSOCKET sock;
    CBytePerfMon performanceMonitor;

    std::string host;
    int outgoing_port;
    int32_t prev_pktseq;

    uint32_t chunkTransmitSize;

    // From paramaeter parsing
    std::string adapter;
    std::string modeName;
    int timeout;
    bool tsbpdMode;
    paramList params;

    void initializeEmpty();
    void handleConnectionParameters(const std::string &_host, const paramList &_params);
    int preConfigureSocket();
    std::string configureSocketLoop(SRT::SockOpt::Binding _binding);
    void setupAdapter(const std::string &_host, int _port);

    bool blocking;
  };

  /// This class is for easily setting up listening socket, either TCP or Unix.
  class SRTServer{
  public:
    SRTServer();
    SRTServer(int existingSock);
    SRTServer(int port, std::string hostname, bool nonblock = false, const std::string &_direction = "input");
    SRTConnection accept(bool nonblock = false, const std::string &direction = "input");
    void setBlocking(bool blocking);
    bool connected() const;
    bool isBlocking();
    void close();
    int getSocket();

  private:
    SRTConnection conn;
    std::string direction;
  };

  struct OptionValue{
    std::string s;
    int i;
    int64_t l;
    bool b;

    const void *value;
    size_t size;
  };

  class SocketOption{
  public:
    //{"enforcedencryption", 0, SRTO_ENFORCEDENCRYPTION, SRT::SockOpt::PRE, SRT::SockOpt::BOOL, nullptr},
    SocketOption(const std::string &_name, int _protocol, int _symbol, SRT::SockOpt::Binding _binding,
                 SRT::SockOpt::Type _type, const SockOptVals &_values = SockOptVals())
        : name(_name), protocol(_protocol), symbol(_symbol), binding(_binding), type(_type),
          valmap(_values){};

    std::string name;
    int protocol;
    int symbol;
    SRT::SockOpt::Binding binding;
    SRT::SockOpt::Type type;
    SockOptVals valmap;

    bool apply(int socket, const std::string &value, bool isSrtOpt = true);

    static int setSo(int socket, int protocol, int symbol, const void *data, size_t size, bool isSrtOpt = true);

    bool extract(const std::string &v, OptionValue &val, SRT::SockOpt::Type asType);
  };

  std::vector<SocketOption> srtOptions();
}// namespace Socket
