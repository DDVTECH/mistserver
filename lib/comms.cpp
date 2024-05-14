#include "auth.h"
#include "comms.h"
#include "defines.h"
#include "stream.h"
#include "procs.h"
#include "timing.h"
#include <fcntl.h>
#include <string.h>
#include <sstream>
#include "config.h"

namespace Comms{
  uint8_t sessionViewerMode = SESS_BUNDLE_DEFAULT_VIEWER;
  uint8_t sessionInputMode = SESS_BUNDLE_DEFAULT_OTHER;
  uint8_t sessionOutputMode = SESS_BUNDLE_DEFAULT_OTHER;
  uint8_t sessionUnspecifiedMode = 0;
  uint8_t sessionStreamInfoMode = SESS_DEFAULT_STREAM_INFO_MODE;
  uint8_t tknMode = SESS_TKN_DEFAULT_MODE;
  uint8_t defaultCommFlags = 0;

  /// \brief Refreshes the session configuration if the last update was more than 5 seconds ago
  void sessionConfigCache(){
    static uint64_t lastUpdate = 0;
    if (Util::bootSecs() > lastUpdate + 5){
      VERYHIGH_MSG("Updating session config");
      JSON::Value tmpVal = Util::getGlobalConfig("sessionViewerMode");
      if (!tmpVal.isNull()){ sessionViewerMode = tmpVal.asInt(); }
      tmpVal = Util::getGlobalConfig("sessionInputMode");
      if (!tmpVal.isNull()){ sessionInputMode = tmpVal.asInt(); }
      tmpVal = Util::getGlobalConfig("sessionOutputMode");
      if (!tmpVal.isNull()){ sessionOutputMode = tmpVal.asInt(); }
      tmpVal = Util::getGlobalConfig("sessionUnspecifiedMode");
      if (!tmpVal.isNull()){ sessionUnspecifiedMode = tmpVal.asInt(); }
      tmpVal = Util::getGlobalConfig("sessionStreamInfoMode");
      if (!tmpVal.isNull()){ sessionStreamInfoMode = tmpVal.asInt(); }
      tmpVal = Util::getGlobalConfig("tknMode");
      if (!tmpVal.isNull()){ tknMode = tmpVal.asInt(); }
      lastUpdate = Util::bootSecs();
    }
  }

  Comms::Comms(){
    index = INVALID_RECORD_INDEX;
    currentSize = 0;
    master = false;
  }

  Comms::~Comms(){
    if (index != INVALID_RECORD_INDEX && status){
      setStatus(COMM_STATUS_DISCONNECT | getStatus());
    }
    if (master){
      if (dataPage.mapped){
        finishAll();
        dataPage.master = true;
      }
      sem.unlink();
    }
    sem.close();
  }

  void Comms::addFields(){
    dataAccX.addField("status", RAX_UINT);
    dataAccX.addField("pid", RAX_64UINT);
  }

  void Comms::nullFields(){
    setPid(getpid());
  }

  void Comms::fieldAccess(){
    status = dataAccX.getFieldAccX("status");
    pid = dataAccX.getFieldAccX("pid");
  }

  size_t Comms::recordCount() const{
    if (!master){return index + 1;}
    return dataAccX.getRCount();
  }

  uint8_t Comms::getStatus() const{return status.uint(index);}
  uint8_t Comms::getStatus(size_t idx) const{return (master ? status.uint(idx) : 0);}
  void Comms::setStatus(uint8_t _status){status.set(_status, index);}
  void Comms::setStatus(uint8_t _status, size_t idx){
    if (!master){return;}
    status.set(_status, idx);
  }

  uint32_t Comms::getPid() const{return pid.uint(index);}
  uint32_t Comms::getPid(size_t idx) const{return (master ? pid.uint(idx) : 0);}
  void Comms::setPid(uint32_t _pid){pid.set(_pid, index);}
  void Comms::setPid(uint32_t _pid, size_t idx){
    if (!master){return;}
    pid.set(_pid, idx);
  }

  void Comms::finishAll(){
    if (!master){return;}
    size_t c = 0;
    bool keepGoing = true;
    do{
      keepGoing = false;
      for (size_t i = 0; i < recordCount(); i++){
        if (getStatus(i) == COMM_STATUS_INVALID || (getStatus(i) & COMM_STATUS_DISCONNECT)){continue;}
        uint64_t cPid = getPid(i);
        if (cPid > 1 && !(getStatus(i) & COMM_STATUS_NOKILL)){
          Util::Procs::Stop(cPid); // soft kill
          keepGoing = true;
        }
        setStatus(COMM_STATUS_REQDISCONNECT | getStatus(i), i);
      }
      if (keepGoing){Util::sleep(250);}
    }while (keepGoing && ++c < 8);
  }

  Comms::operator bool() const{
    if (master){return dataPage;}
    return dataPage && (getStatus() != COMM_STATUS_INVALID) && !(getStatus() & COMM_STATUS_DISCONNECT);
  }

  void Comms::setMaster(bool _master){
    master = _master;
    dataPage.master = _master;
  }

  void Comms::reload(const std::string & prefix, size_t baseSize, bool _master, bool reIssue){
    master = _master;
    if (!currentSize){currentSize = baseSize;}

    if (master){
      dataPage.init(prefix, currentSize, false, false);
      if (dataPage){
        dataPage.master = true;
        dataAccX = Util::RelAccX(dataPage.mapped);
        fieldAccess();
      }else{
        dataPage.init(prefix, currentSize, true);
        dataAccX = Util::RelAccX(dataPage.mapped, false);
        addFields();
        fieldAccess();
        size_t reqCount = (currentSize - dataAccX.getOffset()) / dataAccX.getRSize();
        dataAccX.setRCount(reqCount);
        dataAccX.setPresent(reqCount);
        dataAccX.setReady();
      }
      return;
    }

    dataPage.init(prefix, currentSize, false);
    if (!dataPage){
      WARN_MSG("Unable to open page %s", prefix.c_str());
      return;
    }
    dataAccX = Util::RelAccX(dataPage.mapped);
    if (dataAccX.isExit()){
      dataPage.close();
      return;
    }
    fieldAccess();
    if (index == INVALID_RECORD_INDEX || reIssue){
      size_t reqCount = dataAccX.getRCount();
      for (index = 0; index < reqCount; ++index){
        if (getStatus() == COMM_STATUS_INVALID){
          IPC::semGuard G(&sem);
          if (getStatus() != COMM_STATUS_INVALID){continue;}
          nullFields();
          setStatus(COMM_STATUS_ACTIVE | defaultCommFlags);
          break;
        }
      }
      if (index >= reqCount){
        FAIL_MSG("Could not register entry on comm page!");
        dataPage.close();
      }
    }
  }

  Sessions::Sessions() : Connections(){sem.open(SEM_STATISTICS, O_CREAT | O_RDWR, ACCESSPERMS, 1);}

  void Sessions::reload(bool _master, bool reIssue){
    Comms::reload(COMMS_STATISTICS, COMMS_STATISTICS_INITSIZE, _master, reIssue);
  }

  std::string Sessions::getSessId() const{return sessId.string(index);}
  std::string Sessions::getSessId(size_t idx) const{return (master ? sessId.string(idx) : 0);}
  void Sessions::setSessId(std::string _sid){sessId.set(_sid, index);}
  void Sessions::setSessId(std::string _sid, size_t idx){
    if (!master){return;}
    sessId.set(_sid, idx);
  }

  bool Sessions::sessIdExists(std::string _sid){
    for (size_t i = 0; i < recordCount(); i++){
      if (getStatus(i) == COMM_STATUS_INVALID || (getStatus(i) & COMM_STATUS_DISCONNECT)){continue;}
      if (getSessId(i) == _sid){
        if (Util::Procs::isRunning(getPid(i))){
          return true;
        }
      }
    }
    return false;
  }

  void Sessions::addFields(){
    Connections::addFields();
    dataAccX.addField("tags", RAX_STRING, 512);
    dataAccX.addField("sessid", RAX_STRING, 80);
  }

  void Sessions::nullFields(){
    Connections::nullFields();
    setSessId("");
    setTags("");
  }

  void Sessions::fieldAccess(){
    Connections::fieldAccess();
    tags = dataAccX.getFieldAccX("tags");
    sessId = dataAccX.getFieldAccX("sessid");
  }

  std::string Sessions::getTags() const{return tags.string(index);}
  std::string Sessions::getTags(size_t idx) const{return (master ? tags.string(idx) : 0);}
  void Sessions::setTags(std::string _sid){tags.set(_sid, index);}
  void Sessions::setTags(std::string _sid, size_t idx){
    if (!master){return;}
    tags.set(_sid, idx);
  }

  Users::Users() : Comms(){}

  Users::Users(const Users &rhs) : Comms(){
    if (rhs){
      reload(rhs.streamName, (size_t)rhs.getTrack());
      if (*this){
        setKeyNum(rhs.getKeyNum());
        setTrack(rhs.getTrack());
      }
    }
  }

  void Users::reload(const std::string &_streamName, bool _master, bool reIssue){
    streamName = _streamName;

    char semName[NAME_BUFFER_SIZE];
    snprintf(semName, NAME_BUFFER_SIZE, SEM_USERS, streamName.c_str());
    sem.open(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);

    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_USERS, streamName.c_str());

    Comms::reload(userPageName, COMMS_USERS_INITSIZE, _master, reIssue);
  }
  
  void Users::addFields(){
    Comms::addFields();
    dataAccX.addField("track", RAX_64UINT);
    dataAccX.addField("keynum", RAX_64UINT);
  }

  void Users::nullFields(){
    Comms::nullFields();
    setTrack(0);
    setKeyNum(0);
  }

  void Users::fieldAccess(){
    Comms::fieldAccess();
    track = dataAccX.getFieldAccX("track");
    keyNum = dataAccX.getFieldAccX("keynum");
  }

  void Users::reload(const std::string &_streamName, size_t idx, uint8_t initialState){
    reload(_streamName);
    if (dataPage){
      setTrack(idx);
      setKeyNum(0);
      setStatus(initialState | defaultCommFlags);
    }
  }

  uint32_t Users::getTrack() const{return track.uint(index);}
  uint32_t Users::getTrack(size_t idx) const{return (master ? track.uint(idx) : 0);}
  void Users::setTrack(uint32_t _track){track.set(_track, index);}
  void Users::setTrack(uint32_t _track, size_t idx){
    if (!master){return;}
    track.set(_track, idx);
  }

  size_t Users::getKeyNum() const{return keyNum.uint(index);}
  size_t Users::getKeyNum(size_t idx) const{return (master ? keyNum.uint(idx) : 0);}
  void Users::setKeyNum(size_t _keyNum){keyNum.set(_keyNum, index);}
  void Users::setKeyNum(size_t _keyNum, size_t idx){
    if (!master){return;}
    keyNum.set(_keyNum, idx);
  }



  void Connections::reload(const std::string & sessId, bool _master, bool reIssue){
    // Open SEM_SESSION
    if(!sem){
      char semName[NAME_BUFFER_SIZE];
      snprintf(semName, NAME_BUFFER_SIZE, SEM_SESSION, sessId.c_str());
      sem.open(semName, O_RDWR, ACCESSPERMS, 1);
      if (!sem){return;}
    }
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_SESSIONS, sessId.c_str());
    Comms::reload(userPageName, COMMS_SESSIONS_INITSIZE, _master, reIssue);
  }

  /// \brief Claims a spot on the connections page for the input/output which calls this function
  ///        Starts the MistSession binary for each session, which handles the statistics
  ///         and the USER_NEW and USER_END triggers
  /// \param streamName: Name of the stream the input is providing or an output is making available to viewers
  /// \param ip: IP address of the viewer which wants to access streamName. For inputs this value can be set to any value
  /// \param tkn: Session token given by the player or randomly generated
  /// \param protocol: Protocol currently in use for this connection
  /// \param _master: If True, we are reading from this page. If False, we are writing (to our entry) on this page
  /// \param reIssue: If True, claim a new entry on this page
  void Connections::reload(const std::string & streamName, const std::string & ip, const std::string & tkn, const std::string & protocol, const std::string & reqUrl, bool _master, bool reIssue){
    initialTkn = tkn;
    uint8_t sessMode = sessionViewerMode;
    // Generate a unique session ID for each viewer, input or output
    if (protocol.size() >= 6 && protocol.substr(0, 6) == "INPUT:"){
      sessMode = sessionInputMode;
      sessionId = "I" + generateSession(streamName, ip, tkn, protocol, sessMode);
    }else if (protocol.size() >= 7 && protocol.substr(0, 7) == "OUTPUT:"){
      sessMode = sessionOutputMode;
      sessionId = "O" + generateSession(streamName, ip, tkn, protocol, sessMode);
    }else{
      // If the session only contains the HTTP connector, check sessionStreamInfoMode
      if (protocol.size() == 4 && protocol == "HTTP"){
        if (sessionStreamInfoMode == SESS_HTTP_AS_VIEWER){
          sessionId = generateSession(streamName, ip, tkn, protocol, sessMode);
        }else if (sessionStreamInfoMode == SESS_HTTP_AS_OUTPUT){
          sessMode = sessionOutputMode;
          sessionId = "O" + generateSession(streamName, ip, tkn, protocol, sessMode);
        }else if (sessionStreamInfoMode == SESS_HTTP_DISABLED){
          return;
        }else if (sessionStreamInfoMode == SESS_HTTP_AS_UNSPECIFIED){
          // Set sessMode to include all variables when determining the session ID
          sessMode = sessionUnspecifiedMode;
          sessionId = "U" + generateSession(streamName, ip, tkn, protocol, sessMode);
        }else{
          sessionId = generateSession(streamName, ip, tkn, protocol, sessMode);
        }
      }else{
        sessionId = generateSession(streamName, ip, tkn, protocol, sessMode);
      }
    }
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_SESSIONS, sessionId.c_str());
    // Check if the page exists, if not, spawn new session process
    if (!_master){
      dataPage.init(userPageName, 0, false, false);
      if (!dataPage){
        std::string host;
        Socket::hostBytesToStr(ip.data(), ip.size(), host);
        pid_t thisPid;
        std::deque<std::string> args;
        args.push_back(Util::getMyPath() + "MistServer");
        args.push_back("MistSession");
        args.push_back(sessionId);

        // First bit defines whether to include stream name
        if (sessMode & 0x08){
          args.push_back("--streamname");
          args.push_back(streamName);
        }else{
          setenv("SESSION_STREAM", streamName.c_str(), 1);
        }
        // Second bit defines whether to include viewer ip
        if (sessMode & 0x04){
          args.push_back("--ip");
          args.push_back(host);
        }else{
          setenv("SESSION_IP", host.c_str(), 1);
        }
        // Third bit defines whether to include tkn
        if (sessMode & 0x02){
          args.push_back("--tkn");
          args.push_back(tkn);
        }else{
          setenv("SESSION_TKN", tkn.c_str(), 1);
        }
        // Fourth bit defines whether to include protocol
        if (sessMode & 0x01){
          args.push_back("--protocol");
          args.push_back(protocol);
        }else{
          setenv("SESSION_PROTOCOL", protocol.c_str(), 1);
        }
        setenv("SESSION_REQURL", reqUrl.c_str(), 1);
        int err = fileno(stderr);
        thisPid = Util::Procs::StartPiped(args, 0, 0, &err);
        Util::Procs::forget(thisPid);
        unsetenv("SESSION_STREAM");
        unsetenv("SESSION_IP");
        unsetenv("SESSION_TKN");
        unsetenv("SESSION_PROTOCOL");
        unsetenv("SESSION_REQURL");
      }
    }
    reload(sessionId, _master, reIssue);
    if (index != INVALID_RECORD_INDEX){
      setConnector(protocol);
      setHost(ip);
      setStream(streamName);
      VERYHIGH_MSG("Reloading connection. Claimed record %" PRIu64, index);
    }
  }

  /// \brief Marks the data page as closed, so that we longer write any new data to is
  void Connections::setExit(){
    if (!master){return;}
    dataAccX.setExit();
  }

  bool Connections::getExit(){
    return dataAccX.isExit();
  }

  void Connections::unload(){
    if (index != INVALID_RECORD_INDEX){
      setStatus(COMM_STATUS_DISCONNECT | getStatus());
    }
    index = INVALID_RECORD_INDEX;
  }
  void Connections::addFields(){
    Comms::addFields();
    dataAccX.addField("now", RAX_64UINT);
    dataAccX.addField("time", RAX_64UINT);
    dataAccX.addField("lastsecond", RAX_64UINT);
    dataAccX.addField("down", RAX_64UINT);
    dataAccX.addField("up", RAX_64UINT);
    dataAccX.addField("host", RAX_RAW, 16);
    dataAccX.addField("stream", RAX_STRING, 100);
    dataAccX.addField("connector", RAX_STRING, 20);
    dataAccX.addField("pktcount", RAX_64UINT);
    dataAccX.addField("pktloss", RAX_64UINT);
    dataAccX.addField("pktretrans", RAX_64UINT);
  }

  void Connections::nullFields(){
    Comms::nullFields();
    setConnector("");
    setStream("");
    setHost("");
    setUp(0);
    setDown(0);
    setLastSecond(0);
    setTime(0);
    setNow(0);
    setPacketCount(0);
    setPacketLostCount(0);
    setPacketRetransmitCount(0);
  }

  void Connections::fieldAccess(){
    Comms::fieldAccess();
    now = dataAccX.getFieldAccX("now");
    time = dataAccX.getFieldAccX("time");
    lastSecond = dataAccX.getFieldAccX("lastsecond");
    down = dataAccX.getFieldAccX("down");
    up = dataAccX.getFieldAccX("up");
    host = dataAccX.getFieldAccX("host");
    stream = dataAccX.getFieldAccX("stream");
    connector = dataAccX.getFieldAccX("connector");
    pktcount = dataAccX.getFieldAccX("pktcount");
    pktloss = dataAccX.getFieldAccX("pktloss");
    pktretrans = dataAccX.getFieldAccX("pktretrans");
  }

  uint64_t Connections::getNow() const{return now.uint(index);}
  uint64_t Connections::getNow(size_t idx) const{return (master ? now.uint(idx) : 0);}
  void Connections::setNow(uint64_t _now){now.set(_now, index);}
  void Connections::setNow(uint64_t _now, size_t idx){
    if (!master){return;}
    now.set(_now, idx);
  }

  uint64_t Connections::getTime() const{return time.uint(index);}
  uint64_t Connections::getTime(size_t idx) const{return (master ? time.uint(idx) : 0);}
  void Connections::setTime(uint64_t _time){time.set(_time, index);}
  void Connections::setTime(uint64_t _time, size_t idx){
    if (!master){return;}
    time.set(_time, idx);
  }

  uint64_t Connections::getLastSecond() const{return lastSecond.uint(index);}
  uint64_t Connections::getLastSecond(size_t idx) const{
    return (master ? lastSecond.uint(idx) : 0);
  }
  void Connections::setLastSecond(uint64_t _lastSecond){lastSecond.set(_lastSecond, index);}
  void Connections::setLastSecond(uint64_t _lastSecond, size_t idx){
    if (!master){return;}
    lastSecond.set(_lastSecond, idx);
  }

  uint64_t Connections::getDown() const{return down.uint(index);}
  uint64_t Connections::getDown(size_t idx) const{return (master ? down.uint(idx) : 0);}
  void Connections::setDown(uint64_t _down){down.set(_down, index);}
  void Connections::setDown(uint64_t _down, size_t idx){
    if (!master){return;}
    down.set(_down, idx);
  }

  uint64_t Connections::getUp() const{return up.uint(index);}
  uint64_t Connections::getUp(size_t idx) const{return (master ? up.uint(idx) : 0);}
  void Connections::setUp(uint64_t _up){up.set(_up, index);}
  void Connections::setUp(uint64_t _up, size_t idx){
    if (!master){return;}
    up.set(_up, idx);
  }

  std::string Connections::getHost() const{return std::string(host.ptr(index), 16);}
  std::string Connections::getHost(size_t idx) const{
    if (!master){return std::string((size_t)16, (char)'\000');}
    return std::string(host.ptr(idx), 16);
  }
  void Connections::setHost(std::string _host){host.set(_host, index);}
  void Connections::setHost(std::string _host, size_t idx){
    if (!master){return;}
    host.set(_host, idx);
  }

  std::string Connections::getStream() const{return stream.string(index);}
  std::string Connections::getStream(size_t idx) const{return (master ? stream.string(idx) : "");}
  void Connections::setStream(std::string _stream){stream.set(_stream, index);}
  void Connections::setStream(std::string _stream, size_t idx){
    if (!master){return;}
    stream.set(_stream, idx);
  }

  std::string Connections::getConnector() const{return connector.string(index);}
  std::string Connections::getConnector(size_t idx) const{
    return (master ? connector.string(idx) : "");
  }
  void Connections::setConnector(std::string _connector){connector.set(_connector, index);}
  void Connections::setConnector(std::string _connector, size_t idx){
    if (!master){return;}
    connector.set(_connector, idx);
  }

  bool Connections::hasConnector(size_t idx, std::string protocol){
    std::stringstream sstream(connector.string(idx));
    std::string _conn;
    while (std::getline(sstream, _conn, ',')){
      if (_conn == protocol){
        return true;
      }
    }
    return false;
  }

  uint64_t Connections::getPacketCount() const{return pktcount.uint(index);}
  uint64_t Connections::getPacketCount(size_t idx) const{
    return (master ? pktcount.uint(idx) : 0);
  }
  void Connections::setPacketCount(uint64_t _count){pktcount.set(_count, index);}
  void Connections::setPacketCount(uint64_t _count, size_t idx){
    if (!master){return;}
    pktcount.set(_count, idx);
  }

  uint64_t Connections::getPacketLostCount() const{return pktloss.uint(index);}
  uint64_t Connections::getPacketLostCount(size_t idx) const{
    return (master ? pktloss.uint(idx) : 0);
  }
  void Connections::setPacketLostCount(uint64_t _lost){pktloss.set(_lost, index);}
  void Connections::setPacketLostCount(uint64_t _lost, size_t idx){
    if (!master){return;}
    pktloss.set(_lost, idx);
  }

  uint64_t Connections::getPacketRetransmitCount() const{return pktretrans.uint(index);}
  uint64_t Connections::getPacketRetransmitCount(size_t idx) const{
    return (master ? pktretrans.uint(idx) : 0);
  }
  void Connections::setPacketRetransmitCount(uint64_t _retrans){pktretrans.set(_retrans, index);}
  void Connections::setPacketRetransmitCount(uint64_t _retrans, size_t idx){
    if (!master){return;}
    pktretrans.set(_retrans, idx);
  }

  /// \brief Generates a session ID which is unique per viewer
  /// \return generated session ID as string
  std::string Connections::generateSession(const std::string & streamName, const std::string & ip, const std::string & tkn, const std::string & connector, uint64_t sessionMode){
    std::string concat;
    std::string debugMsg = "Generating session id based on";
    // First bit defines whether to include stream name
    if (sessionMode & 0x08){
      concat += streamName;
      debugMsg += " stream name '" + streamName + "'";
    }
    // Second bit defines whether to include viewer ip
    if (sessionMode & 0x04){
      concat += ip;
      std::string ipHex;
      Socket::hostBytesToStr(ip.c_str(), ip.size(), ipHex);
      debugMsg += " IP '" + ipHex + "'";
    }
    // Third bit defines whether to include client-side session token
    if (sessionMode & 0x02){
      concat += tkn;
      debugMsg += " session token '" + tkn + "'";
    }
    // Fourth bit defines whether to include protocol
    if (sessionMode & 0x01){
      concat += connector;
      debugMsg += " protocol '" + connector + "'";
    }
    VERYHIGH_MSG("%s", debugMsg.c_str());
    return Secure::sha256(concat.c_str(), concat.length());
  }
}// namespace Comms
