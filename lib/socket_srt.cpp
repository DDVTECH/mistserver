#include "defines.h"
#include "lib/http_parser.h"
#include "socket_srt.h"
#include "json.h"
#include "timing.h"

#include <cstdlib>
#include <sstream>

#define INVALID_SRT_SOCKET -1

namespace Socket{
  namespace SRT{
    bool isInited = false;

    // Both Init and Cleanup functions are called implicitly if not done ourselves.
    // SRT documentation states explicitly that this is unreliable behaviour
    bool libraryInit(){
      if (!isInited){
        int res = srt_startup();
        if (res == -1){ERROR_MSG("Unable to initialize SRT Library!");}
        isInited = (res != -1);
      }
      return isInited;
    }

    bool libraryCleanup(){
      if (isInited){
        srt_cleanup();
        isInited = false;
      }
      return true;
    }
  }// namespace SRT

  template <typename T> std::string asString(const T &val){
    std::stringstream x;
    x << val;
    return x.str();
  }

  sockaddr_in createInetAddr(const std::string &_host, int _port){
    sockaddr_in res;
    memset(&res, 9, sizeof res);
    res.sin_family = AF_INET;
    res.sin_port = htons(_port);

    if (_host != ""){
      if (inet_pton(AF_INET, _host.c_str(), &res.sin_addr) == 1){return res;}
      hostent *he = gethostbyname(_host.c_str());
      if (!he || he->h_addrtype != AF_INET){ERROR_MSG("Host not found %s", _host.c_str());}
      res.sin_addr = *(in_addr *)he->h_addr_list[0];
    }

    return res;
  }

  std::string interpretSRTMode(const std::string &_mode, const std::string &_host, const std::string &_adapter){
    if (_mode == "client" || _mode == "caller"){return "caller";}
    if (_mode == "server" || _mode == "listener"){return "listener";}
    if (_mode == "rendezvouz"){return "rendezvous";}
    if (_mode != "default"){return "";}
    if (_host == ""){return "listener";}
    if (_adapter != ""){return "rendezvous";}
    return "caller";
  }

  std::string interpretSRTMode(const HTTP::URL &u){
    paramList params;
    HTTP::parseVars(u.args, params);
    return interpretSRTMode(params.count("mode") ? params.at("mode") : "default", u.host, "");
  }

  SRTConnection::SRTConnection(){
    initializeEmpty();
    lastGood = Util::bootMS();
  }

  // Copy constructor
  SRTConnection::SRTConnection(const SRTConnection &rhs){
    initializeEmpty();
    *this = rhs;
  }

  // Assignment constructor
  SRTConnection &SRTConnection::operator=(const SRTConnection &rhs){
    close();
    initializeEmpty();
    if (!rhs){return *this;}
    memcpy(&remoteaddr, &(rhs.remoteaddr), sizeof(sockaddr_in6));
    direction = rhs.direction;
    remotehost = rhs.remotehost;
    sock = rhs.sock;
    performanceMonitor = rhs.performanceMonitor;
    host = rhs.host;
    outgoing_port = rhs.outgoing_port;
    prev_pktseq = rhs.prev_pktseq;
    lastGood = rhs.lastGood;
    chunkTransmitSize = rhs.chunkTransmitSize;
    adapter = rhs.adapter;
    modeName = rhs.modeName;
    timeout = rhs.timeout;
    tsbpdMode = rhs.tsbpdMode;
    params = rhs.params;
    blocking = rhs.blocking;
    getBinHost();
    return *this;
  }


  SRTConnection::SRTConnection(const std::string &_host, int _port, const std::string &_direction,
                               const std::map<std::string, std::string> &_params){
    connect(_host, _port, _direction, _params);
  }

  SRTConnection::SRTConnection(SRTSOCKET alreadyConnected){
    initializeEmpty();
    sock = alreadyConnected;
  }

  std::string SRTConnection::getStreamName(){
    int sNameLen = 512;
    char sName[sNameLen];
    int optRes = srt_getsockflag(sock, SRTO_STREAMID, (void *)sName, &sNameLen);
    if (optRes != -1 && sNameLen){return sName;}
    return "";
  }

  std::string SRTConnection::getBinHost(){
    char tmpBuffer[17] = "\000\000\000\000\000\000\000\000\000\000\377\377\000\000\000\000";
    switch (remoteaddr.sin6_family){
    case AF_INET:
      memcpy(tmpBuffer + 12, &(reinterpret_cast<const sockaddr_in *>(&remoteaddr)->sin_addr.s_addr), 4);
      break;
    case AF_INET6: memcpy(tmpBuffer, &(remoteaddr.sin6_addr.s6_addr), 16); break;
    default: memset(tmpBuffer, 0, 16); break;
    }
    return std::string(tmpBuffer, 16);
  }

  size_t SRTConnection::RecvNow(){

    bool blockState = blocking;
    if (!blockState){setBlocking(true);}

    SRT_MSGCTRL mc = srt_msgctrl_default;
    int32_t receivedBytes = srt_recvmsg2(sock, recvbuf, 5000, &mc);

    //if (prev_pktseq != 0 && (mc.pktseq - prev_pktseq > 1)){WARN_MSG("Packet lost");}
    prev_pktseq = mc.pktseq;

    if (!blockState){setBlocking(blockState);}
    if (receivedBytes == -1){
      int err = srt_getlasterror(0);
      if (err == SRT_ECONNLOST){
        close();
        return 0;
      }
      if (err == SRT_ENOCONN){
        if (Util::bootMS() > lastGood + 5000){
          ERROR_MSG("SRT connection timed out - closing");
          close();
        }
        return 0;
      }
      ERROR_MSG("Unable to receive data over socket: %s", srt_getlasterror_str());
      if (srt_getsockstate(sock) != SRTS_CONNECTED){close();}
      return 0;
    }
    if (receivedBytes == 0){
      close();
    }else{
      lastGood = Util::bootMS();
    }

    srt_bstats(sock, &performanceMonitor, false);
    return receivedBytes;
  }

  ///Attempts a read, obeying the current blocking setting.
  ///May result in socket being disconnected when connection was lost during read.
  ///Returns amount of bytes actually read
  size_t SRTConnection::Recv(){
    SRT_MSGCTRL mc = srt_msgctrl_default;
    int32_t receivedBytes = srt_recvmsg2(sock, recvbuf, 5000, &mc);
    prev_pktseq = mc.pktseq;
    if (receivedBytes == -1){
      int err = srt_getlasterror(0);
      if (err == SRT_EASYNCRCV){return 0;}
      if (err == SRT_ECONNLOST){
        close();
        return 0;
      }
      if (err == SRT_ENOCONN){
        if (Util::bootMS() > lastGood + 5000){
          ERROR_MSG("SRT connection timed out - closing");
          close();
        }
        return 0;
      }
      ERROR_MSG("Unable to receive data over socket: %s", srt_getlasterror_str());
      if (srt_getsockstate(sock) != SRTS_CONNECTED){close();}
      return 0;
    }
    if (receivedBytes == 0){
      close();
    }else{
      lastGood = Util::bootMS();
    }
    srt_bstats(sock, &performanceMonitor, false);
    return receivedBytes;
  }

  void SRTConnection::connect(const std::string &_host, int _port, const std::string &_direction,
                              const std::map<std::string, std::string> &_params){
    initializeEmpty();

    direction = _direction;

    handleConnectionParameters(_host, _params);

    HIGH_MSG("Opening SRT connection %s in %s mode on %s:%d", modeName.c_str(), direction.c_str(),
             _host.c_str(), _port);

    sock = srt_create_socket();
    if (sock == SRT_ERROR){
      ERROR_MSG("Error creating an SRT socket");
      return;
    }
    if (modeName == "rendezvous"){
      bool v = true;
      srt_setsockopt(sock, 0, SRTO_RENDEZVOUS, &v, sizeof v);
    }
    if (preConfigureSocket() == SRT_ERROR){
      ERROR_MSG("Error configuring SRT socket");
      return;
    }

    if (modeName == "caller"){
      if (outgoing_port){setupAdapter("", outgoing_port);}

      sockaddr_in sa = createInetAddr(_host, _port);
      memcpy(&remoteaddr, &sa, sizeof(sockaddr_in));
      sockaddr *psa = (sockaddr *)&sa;

      HIGH_MSG("Going to connect sock %d", sock);
      if (srt_connect(sock, psa, sizeof sa) == SRT_ERROR){
        srt_close(sock);
        sock = -1;
        ERROR_MSG("Can't connect SRT Socket");
        return;
      }
      HIGH_MSG("Connected sock %d", sock);

      if (postConfigureSocket() == SRT_ERROR){
        ERROR_MSG("Error during postconfigure socket");
        return;
      }
      INFO_MSG("Caller SRT socket %" PRId32 " success targetting %s:%u", sock, _host.c_str(), _port);
      lastGood = Util::bootMS();
      return;
    }
    if (modeName == "listener"){
      HIGH_MSG("Going to bind a server on %s:%u", _host.c_str(), _port);

      sockaddr_in sa = createInetAddr(_host, _port);
      sockaddr *psa = (sockaddr *)&sa;

      if (srt_bind(sock, psa, sizeof sa) == SRT_ERROR){
        srt_close(sock);
        sock = -1;
        ERROR_MSG("Can't connect SRT Socket: %s", srt_getlasterror_str());
        return;
      }
      if (srt_listen(sock, 100) == SRT_ERROR){
        srt_close(sock);
        sock = -1;
        ERROR_MSG("Can not listen on Socket");
      }
      INFO_MSG("Listener SRT socket sucess @ %s:%u", _host.c_str(), _port);
      lastGood = Util::bootMS();
      return;
    }
    if (modeName == "rendezvous"){
      int outport = (outgoing_port ? outgoing_port : _port);
      HIGH_MSG("Going to bind a server on %s:%u", _host.c_str(), _port);

      sockaddr_in sa = createInetAddr(_host, outport);
      sockaddr *psa = (sockaddr *)&sa;

      if (srt_bind(sock, psa, sizeof sa) == SRT_ERROR){
        srt_close(sock);
        sock = -1;
        ERROR_MSG("Can't connect SRT Socket");
        return;
      }

      sockaddr_in sb = createInetAddr(_host, outport);
      sockaddr *psb = (sockaddr *)&sb;

      if (srt_connect(sock, psb, sizeof sb) == SRT_ERROR){
        srt_close(sock);
        sock = -1;
        ERROR_MSG("Can't connect SRT Socket");
        return;
      }

      if (postConfigureSocket() == SRT_ERROR){
        ERROR_MSG("Error during postconfigure socket");
        return;
      }
      INFO_MSG("Rendezvous SRT socket sucess @ %s:%u", _host.c_str(), _port);
      lastGood = Util::bootMS();
      return;
    }
    ERROR_MSG("Invalid mode parameter. Use 'client' or 'server'");
  }

  void SRTConnection::setupAdapter(const std::string &_host, int _port){
    sockaddr_in localsa = createInetAddr(_host, _port);
    sockaddr *psa = (sockaddr *)&localsa;
    if (srt_bind(sock, psa, sizeof localsa) == SRT_ERROR){
      ERROR_MSG("Unable to bind socket to %s:%u", _host.c_str(), _port);
    }
  }

  void SRTConnection::SendNow(const std::string &data){SendNow(data.data(), data.size());}

  void SRTConnection::SendNow(const char *data, size_t len){
    srt_clearlasterror();
    int res = srt_sendmsg2(sock, data, len, NULL);

    if (res == SRT_ERROR){
      int err = srt_getlasterror(0);
      //Do not report normal connection lost errors
      if (err == SRT_ECONNLOST){
        close();
        return;
      }
      if (err == SRT_ENOCONN){
        if (Util::bootMS() > lastGood + 5000){
          ERROR_MSG("SRT connection timed out - closing");
          close();
        }
        return;
      }
//      ERROR_MSG("Unable to send data over socket %" PRId32 ": %s", sock, srt_getlasterror_str());
      if (srt_getsockstate(sock) != SRTS_CONNECTED){close();}
    }else{
      lastGood = Util::bootMS();
    }
    srt_bstats(sock, &performanceMonitor, false);
  }

  unsigned int SRTConnection::connTime(){
    srt_bstats(sock, &performanceMonitor, false);
    return performanceMonitor.msTimeStamp / 1000;
  }

  uint64_t SRTConnection::dataUp(){return performanceMonitor.byteSentTotal;}

  uint64_t SRTConnection::dataDown(){return performanceMonitor.byteRecvTotal;}

  uint64_t SRTConnection::packetCount(){
    return (direction == "output" ? performanceMonitor.pktSentTotal : performanceMonitor.pktRecvTotal);
  }

  uint64_t SRTConnection::packetLostCount(){
    return (direction == "output" ? performanceMonitor.pktSndLossTotal : performanceMonitor.pktRcvLossTotal);
  }

  uint64_t SRTConnection::packetRetransmitCount(){
    //\todo This should be updated with pktRcvRetransTotal on the retrieving end once srt has implemented this.
    return (direction == "output" ? performanceMonitor.pktRetransTotal : 0);
  }

  void SRTConnection::initializeEmpty(){
    memset(&performanceMonitor, 0, sizeof(performanceMonitor));
    prev_pktseq = 0;
    sock = SRT_INVALID_SOCK;
    outgoing_port = 0;
    chunkTransmitSize = 1316;
    blocking = false;
    timeout = 0;
  }

  void SRTConnection::setBlocking(bool _blocking){
    if (_blocking == blocking){return;}
    // If we have an error setting the new blocking state, the state is unchanged so we return early.
    if (srt_setsockopt(sock, 0, SRTO_SNDSYN, &_blocking, sizeof _blocking) == -1){return;}
    if (srt_setsockopt(sock, 0, SRTO_RCVSYN, &_blocking, sizeof _blocking) == -1){return;}
    blocking = _blocking;
  }

  bool SRTConnection::isBlocking(){return blocking;}

  void SRTConnection::handleConnectionParameters(const std::string &_host,
                                                 const std::map<std::string, std::string> &_params){
    params = _params;
    DONTEVEN_MSG("SRT Received parameters: ");
    for (std::map<std::string, std::string>::const_iterator it = params.begin(); it != params.end(); it++){
      DONTEVEN_MSG("  %s: %s", it->first.c_str(), it->second.c_str());
    }

    adapter = (params.count("adapter") ? params.at("adapter") : "");

    modeName = interpretSRTMode((params.count("mode") ? params.at("mode") : "default"), _host, adapter);
    if (modeName == ""){
      ERROR_MSG("Invalid SRT mode encountered");
      return;
    }

    // Using strtol because the original code uses base 0 -> automatic detection of octal and hexadecimal systems.
    timeout = (params.count("timeout") ? strtol(params.at("timeout").c_str(), 0, 0) : 0);

    if (adapter == "" && modeName == "listener"){adapter = _host;}

    tsbpdMode = (params.count("tsbpd") && JSON::Value(params.at("tsbpd")).asBool());

    outgoing_port = (params.count("port") ? strtol(params.at("port").c_str(), 0, 0) : 0);

    if ((!params.count("transtype") || params.at("transtype") != "file") && chunkTransmitSize > SRT_LIVE_DEF_PLSIZE){
      if (chunkTransmitSize > SRT_LIVE_MAX_PLSIZE){
        ERROR_MSG("Chunk size in live mode exceeds 1456 bytes!");
        return;
      }
    }
    params["payloadsize"] = asString(chunkTransmitSize);
    //This line forces the transmission type to live if unset.
    //Live is actually the default, but not explicitly setting the option means
    //that all other defaults do not get applied either, which is bad.
    if (!params.count("transtype")){params["transtype"] = "live";}
  }

  int SRTConnection::preConfigureSocket(){
    bool no = false;
    if (!tsbpdMode){
      if (srt_setsockopt(sock, 0, SRTO_TSBPDMODE, &no, sizeof no) == -1){return -1;}
    }
    if (srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no) == -1){return -1;}

    if (params.count("linger")){
      linger lin;
      lin.l_linger = atoi(params.at("linger").c_str());
      lin.l_onoff = lin.l_linger > 0 ? 1 : 0;
      srt_setsockopt(sock, 0, SRTO_LINGER, &lin, sizeof(linger));
    }

    std::string errMsg = configureSocketLoop(SRT::SockOpt::PRE);
    if (errMsg.size()){
      WARN_MSG("Failed to set the following options: %s", errMsg.c_str());
      return SRT_ERROR;
    }

    if (direction == "output"){
      int v = 1;
      if (srt_setsockopt(sock, 0, SRTO_SENDER, &v, sizeof v) == SRT_ERROR){return SRT_ERROR;}
    }

    return 0;
  }

  int SRTConnection::postConfigureSocket(){
    bool no = false;
    if (srt_setsockopt(sock, 0, SRTO_SNDSYN, &no, sizeof no) == -1){return -1;}
    if (srt_setsockopt(sock, 0, SRTO_RCVSYN, &no, sizeof no) == -1){return -1;}
    if (timeout){
      if (srt_setsockopt(sock, 0, SRTO_SNDTIMEO, &timeout, sizeof timeout) == -1){return -1;}
      if (srt_setsockopt(sock, 0, SRTO_RCVTIMEO, &timeout, sizeof timeout) == -1){return -1;}
    }
    std::string errMsg = configureSocketLoop(SRT::SockOpt::POST);
    if (errMsg.size()){
      WARN_MSG("Failed to set the following options: %s", errMsg.c_str());
      return SRT_ERROR;
    }
    return 0;
  }

  std::string SRTConnection::configureSocketLoop(SRT::SockOpt::Binding _binding){
    std::string errMsg;

    std::vector<SocketOption> allSrtOptions = srtOptions();
    for (std::vector<SocketOption>::iterator it = allSrtOptions.begin(); it != allSrtOptions.end(); it++){
      if (it->binding == _binding && params.count(it->name)){
        std::string value = params.at(it->name);
        if (!it->apply(sock, value)){errMsg += it->name + " ";}
      }
    }
    return errMsg;
  }

  void SRTConnection::close(){
    if (sock != -1){
      srt_close(sock);
      sock = -1;
    }
  }

  SRTServer::SRTServer(){}

  SRTServer::SRTServer(int fromSock){conn = SRTConnection(fromSock);}

  SRTServer::SRTServer(int port, std::string hostname, std::map<std::string, std::string> _params, bool nonblock, const std::string &_direction){
    // We always create a server as listening
    _params["mode"] = "listener";
    if (hostname == ""){hostname = "0.0.0.0";}
    conn.connect(hostname, port, _direction, _params);
    conn.setBlocking(true);
    if (!conn){
      ERROR_MSG("Unable to create socket");
      return;
    }
  }

  SRTConnection SRTServer::accept(bool nonblock, const std::string &direction){
    if (!conn){return SRTConnection();}
    struct sockaddr_in6 tmpaddr;
    int len = sizeof(tmpaddr);

    SRTConnection r(srt_accept(conn.getSocket(), (sockaddr *)&tmpaddr, &len));
    if (!r){
      if (conn.getSocket() != -1 && srt_getlasterror(0) != SRT_EASYNCRCV){
        FAIL_MSG("Error during accept: %s. Closing server socket %d.", srt_getlasterror_str(), conn.getSocket());
        close();
      }
      return r;
    }

    r.direction = direction;
    r.params = conn.params;
    r.postConfigureSocket();
    r.setBlocking(!nonblock);
    static char addrconv[INET6_ADDRSTRLEN];

    memcpy(&(r.remoteaddr), &tmpaddr, sizeof(tmpaddr));
    if (tmpaddr.sin6_family == AF_INET6){
      r.remotehost = inet_ntop(AF_INET6, &(tmpaddr.sin6_addr), addrconv, INET6_ADDRSTRLEN);
      HIGH_MSG("IPv6 addr [%s]", r.remotehost.c_str());
    }
    if (tmpaddr.sin6_family == AF_INET){
      r.remotehost = inet_ntop(AF_INET, &(((sockaddr_in *)&tmpaddr)->sin_addr), addrconv, INET6_ADDRSTRLEN);
      HIGH_MSG("IPv4 addr [%s]", r.remotehost.c_str());
    }
    INFO_MSG("Accepted a socket coming from %s", r.remotehost.c_str());
    r.getBinHost();
    return r;
  }

  void SRTServer::setBlocking(bool blocking){conn.setBlocking(blocking);}

  bool SRTServer::isBlocking(){return (conn ? conn.isBlocking() : false);}

  void SRTServer::close(){conn.close();}

  bool SRTServer::connected() const{return conn.connected();}

  int SRTServer::getSocket(){return conn.getSocket();}

  inline int SocketOption::setSo(int socket, int proto, int sym, const void *data, size_t size, bool isSrtOpt){
    if (isSrtOpt){return srt_setsockopt(socket, 0, SRT_SOCKOPT(sym), data, (int)size);}
    return ::setsockopt(socket, proto, sym, (const char *)data, (int)size);
  }

  bool SocketOption::extract(const std::string &v, OptionValue &val, SRT::SockOpt::Type asType){
    switch (asType){
    case SRT::SockOpt::STRING:
      val.s = v;
      val.value = val.s.data();
      val.size = val.s.size();
      break;
    case SRT::SockOpt::INT:
    case SRT::SockOpt::INT64:{
      int64_t tmp = strtol(v.c_str(), 0, 0);
      if (tmp == 0 && (!v.size() || v[0] != '0')){return false;}
      if (asType == SRT::SockOpt::INT){
        val.i = tmp;
        val.value = &val.i;
        val.size = sizeof(val.i);
      }else{
        val.l = tmp;
        val.value = &val.l;
        val.size = sizeof(val.l);
      }
    }break;
    case SRT::SockOpt::BOOL:{
      val.b = JSON::Value(v).asBool();
      val.value = &val.b;
      val.size = sizeof val.b;
    }break;
    case SRT::SockOpt::ENUM:{
      // Search value in the map. If found, set to o.
      SockOptVals::const_iterator p = valmap.find(v);
      if (p != valmap.end()){
        val.i = p->second;
        val.value = &val.i;
        val.size = sizeof val.i;
        return true;
      }
      // Fallback: try interpreting it as integer.
      return extract(v, val, SRT::SockOpt::INT);
    }
    }

    return true;
  }

  bool SocketOption::apply(int socket, const std::string &value, bool isSrtOpt){
    OptionValue o;
    int result = -1;
    if (extract(value, o, type)){
      result = setSo(socket, protocol, symbol, o.value, o.size, isSrtOpt);
    }
    return result != -1;
  }

  const std::map<std::string, int> enummap_transtype;

  std::vector<SocketOption> srtOptions(){

    static std::map<std::string, int> enummap_transtype;
    if (!enummap_transtype.size()){
      enummap_transtype["live"] = SRTT_LIVE;
      enummap_transtype["file"] = SRTT_FILE;
    }

    static std::vector<SocketOption> res;
    if (res.size()){return res;}
    res.push_back(SocketOption("transtype", 0, SRTO_TRANSTYPE, SRT::SockOpt::PRE,
                               SRT::SockOpt::ENUM, enummap_transtype));
    res.push_back(SocketOption("maxbw", 0, SRTO_MAXBW, SRT::SockOpt::PRE, SRT::SockOpt::INT64));
    res.push_back(SocketOption("pbkeylen", 0, SRTO_PBKEYLEN, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("passphrase", 0, SRTO_PASSPHRASE, SRT::SockOpt::PRE, SRT::SockOpt::STRING));

    res.push_back(SocketOption("mss", 0, SRTO_MSS, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("fc", 0, SRTO_FC, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("sndbuf", 0, SRTO_SNDBUF, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("rcvbuf", 0, SRTO_RCVBUF, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    // linger option is handled outside of the common loop, therefore commented out.
    // res.push_back(SocketOption( "linger", 0, SRTO_LINGER, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("ipttl", 0, SRTO_IPTTL, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("iptos", 0, SRTO_IPTOS, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("inputbw", 0, SRTO_INPUTBW, SRT::SockOpt::POST, SRT::SockOpt::INT64));
    res.push_back(SocketOption("oheadbw", 0, SRTO_OHEADBW, SRT::SockOpt::POST, SRT::SockOpt::INT));
    res.push_back(SocketOption("latency", 0, SRTO_LATENCY, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("tsbpdmode", 0, SRTO_TSBPDMODE, SRT::SockOpt::PRE, SRT::SockOpt::BOOL));
    res.push_back(SocketOption("tlpktdrop", 0, SRTO_TLPKTDROP, SRT::SockOpt::PRE, SRT::SockOpt::BOOL));
    res.push_back(SocketOption("snddropdelay", 0, SRTO_SNDDROPDELAY, SRT::SockOpt::POST, SRT::SockOpt::INT));
    res.push_back(SocketOption("nakreport", 0, SRTO_NAKREPORT, SRT::SockOpt::PRE, SRT::SockOpt::BOOL));
    res.push_back(SocketOption("conntimeo", 0, SRTO_CONNTIMEO, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("lossmaxttl", 0, SRTO_LOSSMAXTTL, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("rcvlatency", 0, SRTO_RCVLATENCY, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("peerlatency", 0, SRTO_PEERLATENCY, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("minversion", 0, SRTO_MINVERSION, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("streamid", 0, SRTO_STREAMID, SRT::SockOpt::PRE, SRT::SockOpt::STRING));
    res.push_back(SocketOption("congestion", 0, SRTO_CONGESTION, SRT::SockOpt::PRE, SRT::SockOpt::STRING));
    res.push_back(SocketOption("messageapi", 0, SRTO_MESSAGEAPI, SRT::SockOpt::PRE, SRT::SockOpt::BOOL));
    //    res.push_back(SocketOption("payloadsize", 0, SRTO_PAYLOADSIZE, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("kmrefreshrate", 0, SRTO_KMREFRESHRATE, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("kmpreannounce", 0, SRTO_KMPREANNOUNCE, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("enforcedencryption", 0, SRTO_ENFORCEDENCRYPTION, SRT::SockOpt::PRE,
                               SRT::SockOpt::BOOL));
    res.push_back(SocketOption("peeridletimeo", 0, SRTO_PEERIDLETIMEO, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    res.push_back(SocketOption("packetfilter", 0, SRTO_PACKETFILTER, SRT::SockOpt::PRE, SRT::SockOpt::STRING));
    // res.push_back(SocketOption( "groupconnect", 0, SRTO_GROUPCONNECT, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    // res.push_back(SocketOption( "groupstabtimeo", 0, SRTO_GROUPSTABTIMEO, SRT::SockOpt::PRE, SRT::SockOpt::INT));
    return res;
  }
}// namespace Socket
