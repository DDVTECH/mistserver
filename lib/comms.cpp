#include "auth.h"
#include "bitfields.h"
#include "comms.h"
#include "defines.h"
#include "encode.h"
#include "procs.h"
#include "timing.h"
#include <fcntl.h>
#include <string.h>

namespace Comms{
  Comms::Comms(){
    index = INVALID_RECORD_INDEX;
    currentSize = 0;
    master = false;
  }

  Comms::~Comms(){
    if (index != INVALID_RECORD_INDEX){
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
        if (cPid > 1){
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
    fieldAccess();
    if (index == INVALID_RECORD_INDEX || reIssue){
      size_t reqCount = dataAccX.getRCount();
      for (index = 0; index < reqCount; ++index){
        if (getStatus() == COMM_STATUS_INVALID){
          IPC::semGuard G(&sem);
          if (getStatus() != COMM_STATUS_INVALID){continue;}
          nullFields();
          setStatus(COMM_STATUS_ACTIVE);
          break;
        }
      }
      if (index >= reqCount){
        FAIL_MSG("Could not register entry on comm page!");
        dataPage.close();
      }
    }
  }

  Statistics::Statistics() : Comms(){sem.open(SEM_STATISTICS, O_CREAT | O_RDWR, ACCESSPERMS, 1);}

  void Statistics::unload(){
    if (index != INVALID_RECORD_INDEX){
      setStatus(COMM_STATUS_DISCONNECT | getStatus());
    }
    index = INVALID_RECORD_INDEX;
  }

  void Statistics::reload(bool _master, bool reIssue){
    Comms::reload(COMMS_STATISTICS, COMMS_STATISTICS_INITSIZE, _master, reIssue);
  }

  void Statistics::addFields(){
    Comms::addFields();
    dataAccX.addField("sync", RAX_UINT);
    dataAccX.addField("now", RAX_64UINT);
    dataAccX.addField("time", RAX_64UINT);
    dataAccX.addField("lastsecond", RAX_64UINT);
    dataAccX.addField("down", RAX_64UINT);
    dataAccX.addField("up", RAX_64UINT);
    dataAccX.addField("host", RAX_RAW, 16);
    dataAccX.addField("stream", RAX_STRING, 100);
    dataAccX.addField("connector", RAX_STRING, 20);
    dataAccX.addField("crc", RAX_32UINT);
    dataAccX.addField("pktcount", RAX_64UINT);
    dataAccX.addField("pktloss", RAX_64UINT);
    dataAccX.addField("pktretrans", RAX_64UINT);
  }

  void Statistics::nullFields(){
    Comms::nullFields();
    setCRC(0);
    setConnector("");
    setStream("");
    setHost("");
    setUp(0);
    setDown(0);
    setLastSecond(0);
    setTime(0);
    setNow(0);
    setSync(0);
    setPacketCount(0);
    setPacketLostCount(0);
    setPacketRetransmitCount(0);
  }

  void Statistics::fieldAccess(){
    Comms::fieldAccess();
    sync = dataAccX.getFieldAccX("sync");
    now = dataAccX.getFieldAccX("now");
    time = dataAccX.getFieldAccX("time");
    lastSecond = dataAccX.getFieldAccX("lastsecond");
    down = dataAccX.getFieldAccX("down");
    up = dataAccX.getFieldAccX("up");
    host = dataAccX.getFieldAccX("host");
    stream = dataAccX.getFieldAccX("stream");
    connector = dataAccX.getFieldAccX("connector");
    crc = dataAccX.getFieldAccX("crc");
    pktcount = dataAccX.getFieldAccX("pktcount");
    pktloss = dataAccX.getFieldAccX("pktloss");
    pktretrans = dataAccX.getFieldAccX("pktretrans");
  }

  uint8_t Statistics::getSync() const{return sync.uint(index);}
  uint8_t Statistics::getSync(size_t idx) const{return (master ? sync.uint(idx) : 0);}
  void Statistics::setSync(uint8_t _sync){sync.set(_sync, index);}
  void Statistics::setSync(uint8_t _sync, size_t idx){
    if (!master){return;}
    sync.set(_sync, idx);
  }

  uint64_t Statistics::getNow() const{return now.uint(index);}
  uint64_t Statistics::getNow(size_t idx) const{return (master ? now.uint(idx) : 0);}
  void Statistics::setNow(uint64_t _now){now.set(_now, index);}
  void Statistics::setNow(uint64_t _now, size_t idx){
    if (!master){return;}
    now.set(_now, idx);
  }

  uint64_t Statistics::getTime() const{return time.uint(index);}
  uint64_t Statistics::getTime(size_t idx) const{return (master ? time.uint(idx) : 0);}
  void Statistics::setTime(uint64_t _time){time.set(_time, index);}
  void Statistics::setTime(uint64_t _time, size_t idx){
    if (!master){return;}
    time.set(_time, idx);
  }

  uint64_t Statistics::getLastSecond() const{return lastSecond.uint(index);}
  uint64_t Statistics::getLastSecond(size_t idx) const{
    return (master ? lastSecond.uint(idx) : 0);
  }
  void Statistics::setLastSecond(uint64_t _lastSecond){lastSecond.set(_lastSecond, index);}
  void Statistics::setLastSecond(uint64_t _lastSecond, size_t idx){
    if (!master){return;}
    lastSecond.set(_lastSecond, idx);
  }

  uint64_t Statistics::getDown() const{return down.uint(index);}
  uint64_t Statistics::getDown(size_t idx) const{return (master ? down.uint(idx) : 0);}
  void Statistics::setDown(uint64_t _down){down.set(_down, index);}
  void Statistics::setDown(uint64_t _down, size_t idx){
    if (!master){return;}
    down.set(_down, idx);
  }

  uint64_t Statistics::getUp() const{return up.uint(index);}
  uint64_t Statistics::getUp(size_t idx) const{return (master ? up.uint(idx) : 0);}
  void Statistics::setUp(uint64_t _up){up.set(_up, index);}
  void Statistics::setUp(uint64_t _up, size_t idx){
    if (!master){return;}
    up.set(_up, idx);
  }

  std::string Statistics::getHost() const{return std::string(host.ptr(index), 16);}
  std::string Statistics::getHost(size_t idx) const{
    if (!master){return std::string((size_t)16, (char)'\000');}
    return std::string(host.ptr(idx), 16);
  }
  void Statistics::setHost(std::string _host){host.set(_host, index);}
  void Statistics::setHost(std::string _host, size_t idx){
    if (!master){return;}
    host.set(_host, idx);
  }

  std::string Statistics::getStream() const{return stream.string(index);}
  std::string Statistics::getStream(size_t idx) const{return (master ? stream.string(idx) : "");}
  void Statistics::setStream(std::string _stream){stream.set(_stream, index);}
  void Statistics::setStream(std::string _stream, size_t idx){
    if (!master){return;}
    stream.set(_stream, idx);
  }

  std::string Statistics::getConnector() const{return connector.string(index);}
  std::string Statistics::getConnector(size_t idx) const{
    return (master ? connector.string(idx) : "");
  }
  void Statistics::setConnector(std::string _connector){connector.set(_connector, index);}
  void Statistics::setConnector(std::string _connector, size_t idx){
    if (!master){return;}
    connector.set(_connector, idx);
  }

  uint32_t Statistics::getCRC() const{return crc.uint(index);}
  uint32_t Statistics::getCRC(size_t idx) const{return (master ? crc.uint(idx) : 0);}
  void Statistics::setCRC(uint32_t _crc){crc.set(_crc, index);}
  void Statistics::setCRC(uint32_t _crc, size_t idx){
    if (!master){return;}
    crc.set(_crc, idx);
  }

  uint64_t Statistics::getPacketCount() const{return pktcount.uint(index);}
  uint64_t Statistics::getPacketCount(size_t idx) const{
    return (master ? pktcount.uint(idx) : 0);
  }
  void Statistics::setPacketCount(uint64_t _count){pktcount.set(_count, index);}
  void Statistics::setPacketCount(uint64_t _count, size_t idx){
    if (!master){return;}
    pktcount.set(_count, idx);
  }

  uint64_t Statistics::getPacketLostCount() const{return pktloss.uint(index);}
  uint64_t Statistics::getPacketLostCount(size_t idx) const{
    return (master ? pktloss.uint(idx) : 0);
  }
  void Statistics::setPacketLostCount(uint64_t _lost){pktloss.set(_lost, index);}
  void Statistics::setPacketLostCount(uint64_t _lost, size_t idx){
    if (!master){return;}
    pktloss.set(_lost, idx);
  }

  uint64_t Statistics::getPacketRetransmitCount() const{return pktretrans.uint(index);}
  uint64_t Statistics::getPacketRetransmitCount(size_t idx) const{
    return (master ? pktretrans.uint(idx) : 0);
  }
  void Statistics::setPacketRetransmitCount(uint64_t _retrans){pktretrans.set(_retrans, index);}
  void Statistics::setPacketRetransmitCount(uint64_t _retrans, size_t idx){
    if (!master){return;}
    pktretrans.set(_retrans, idx);
  }

  std::string Statistics::getSessId() const{return getSessId(index);}

  std::string Statistics::getSessId(size_t idx) const{
    char res[140];
    memset(res, 0, 140);
    std::string tmp = host.string(idx);
    memcpy(res, tmp.c_str(), (tmp.size() > 16 ? 16 : tmp.size()));
    tmp = stream.string(idx);
    memcpy(res + 16, tmp.c_str(), (tmp.size() > 100 ? 100 : tmp.size()));
    tmp = connector.string(idx);
    memcpy(res + 116, tmp.c_str(), (tmp.size() > 20 ? 20 : tmp.size()));
    Bit::htobl(res + 136, crc.uint(idx));
    return Secure::md5(res, 140);
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
      setStatus(initialState);
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
}// namespace Comms
