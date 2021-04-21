#include "auth.h"
#include "bitfields.h"
#include "comms.h"
#include "defines.h"
#include "encode.h"
#include "procs.h"
#include "timing.h"

namespace Comms{
  Comms::Comms(){
    index = INVALID_RECORD_INDEX;
    currentSize = 0;
    master = false;
  }

  Comms::~Comms(){
    if (index != INVALID_RECORD_INDEX){setStatus(COMM_STATUS_DISCONNECT);}
    if (master){
      if (dataPage.mapped){
        finishAll();
        dataPage.master = true;
      }
      sem.unlink();
    }
    sem.close();
  }

  void Comms::addCommonFields(){
    dataAccX.addField("status", RAX_UINT);
    dataAccX.addField("command", RAX_64UINT);
    dataAccX.addField("timer", RAX_UINT);
    dataAccX.addField("pid", RAX_32UINT);
    dataAccX.addField("killtime", RAX_64UINT);
  }

  void Comms::commonFieldAccess(){
    status = dataAccX.getFieldAccX("status");
    command = dataAccX.getFieldAccX("command");
    timer = dataAccX.getFieldAccX("timer");
    pid = dataAccX.getFieldAccX("pid");
    killTime = dataAccX.getFieldAccX("killtime");
  }

  size_t Comms::firstValid() const{
    if (!master){return index;}
    return dataAccX.getStartPos();
  }

  size_t Comms::endValid() const{
    if (!master){return index + 1;}
    return dataAccX.getEndPos();
  }

  void Comms::deleteFirst(){
    if (!master){return;}
    dataAccX.deleteRecords(1);
  }

  uint8_t Comms::getStatus() const{return status.uint(index);}
  uint8_t Comms::getStatus(size_t idx) const{return (master ? status.uint(idx) : 0);}
  void Comms::setStatus(uint8_t _status){status.set(_status, index);}
  void Comms::setStatus(uint8_t _status, size_t idx){
    if (!master){return;}
    status.set(_status, idx);
  }

  uint64_t Comms::getCommand() const{return command.uint(index);}
  uint64_t Comms::getCommand(size_t idx) const{return (master ? command.uint(idx) : 0);}
  void Comms::setCommand(uint64_t _cmd){command.set(_cmd, index);}
  void Comms::setCommand(uint64_t _cmd, size_t idx){
    if (!master){return;}
    command.set(_cmd, idx);
  }

  uint8_t Comms::getTimer() const{return timer.uint(index);}
  uint8_t Comms::getTimer(size_t idx) const{return (master ? timer.uint(idx) : 0);}
  void Comms::setTimer(uint8_t _timer){timer.set(_timer, index);}
  void Comms::setTimer(uint8_t _timer, size_t idx){
    if (!master){return;}
    timer.set(_timer, idx);
  }

  uint32_t Comms::getPid() const{return pid.uint(index);}
  uint32_t Comms::getPid(size_t idx) const{return (master ? pid.uint(idx) : 0);}
  void Comms::setPid(uint32_t _pid){pid.set(_pid, index);}
  void Comms::setPid(uint32_t _pid, size_t idx){
    if (!master){return;}
    pid.set(_pid, idx);
  }

  void Comms::kill(size_t idx, bool force){
    if (!master){return;}
    if (force){
      Util::Procs::Murder(pid.uint(idx)); // hard kill
      status.set(COMM_STATUS_INVALID, idx);
      return;
    }
    uint64_t kTime = killTime.uint(idx);
    uint64_t now = Util::bootSecs();
    if (!kTime){
      kTime = now;
      killTime.set(kTime, idx);
    }
    if (now - kTime > 30){
      Util::Procs::Murder(pid.uint(idx)); // hard kill
      status.set(COMM_STATUS_INVALID, idx);
    }else{
      Util::Procs::Stop(pid.uint(idx)); // soft kill
    }
  }

  void Comms::finishAll(){
    if (!master){return;}
    size_t c = 0;
    do{
      for (size_t i = firstValid(); i < endValid(); i++){
        if (getStatus(i) == COMM_STATUS_INVALID){continue;}
        setStatus(COMM_STATUS_DISCONNECT, i);
      }
      while (getStatus(firstValid()) == COMM_STATUS_INVALID){deleteFirst();}
    }while (firstValid() < endValid() && ++c < 10);
  }

  void Comms::keepAlive(){
    if (isAlive()){setTimer(0);}
  }

  bool Comms::isAlive() const{
    if (!*this){return false;}
    if (getStatus() == COMM_STATUS_INVALID){return false;}
    if (getStatus() == COMM_STATUS_DISCONNECT){return false;}
    return getTimer() < 126;
  }

  void Comms::setMaster(bool _master){
    master = _master;
    dataPage.master = _master;
  }

  Statistics::Statistics() : Comms(){sem.open(SEM_STATISTICS, O_CREAT | O_RDWR, ACCESSPERMS, 1);}

  void Statistics::unload(){
    if (index != INVALID_RECORD_INDEX){setStatus(COMM_STATUS_DISCONNECT);}
    index = INVALID_RECORD_INDEX;
  }

  void Statistics::reload(bool _master, bool reIssue){
    master = _master;
    bool setFields = true;

    if (!currentSize){currentSize = COMMS_STATISTICS_INITSIZE;}
    dataPage.init(COMMS_STATISTICS, currentSize, false, false);
    if (master){
      if (dataPage.mapped){
        setFields = false;
        dataPage.master = true;
      }else{
        dataPage.init(COMMS_STATISTICS, currentSize, true);
      }
    }
    if (!dataPage.mapped){
      FAIL_MSG("Unable to open page " COMMS_STATISTICS);
      return;
    }

    if (master){
      dataAccX = Util::RelAccX(dataPage.mapped, false);
      if (setFields){
        addCommonFields();

        dataAccX.addField("sync", RAX_UINT);
        dataAccX.addField("now", RAX_64UINT);
        dataAccX.addField("time", RAX_64UINT);
        dataAccX.addField("lastsecond", RAX_64UINT);
        dataAccX.addField("down", RAX_64UINT);
        dataAccX.addField("up", RAX_64UINT);
        dataAccX.addField("host", RAX_STRING, 16);
        dataAccX.addField("stream", RAX_STRING, 100);
        dataAccX.addField("connector", RAX_STRING, 20);
        dataAccX.addField("crc", RAX_32UINT);

        dataAccX.setRCount((currentSize - dataAccX.getOffset()) / dataAccX.getRSize());
        dataAccX.setReady();
      }

    }else{
      dataAccX = Util::RelAccX(dataPage.mapped);
      if (index == INVALID_RECORD_INDEX || reIssue){
        sem.wait();
        for (index = 0; index < dataAccX.getEndPos(); ++index){
          if (dataAccX.getInt("status", index) == COMM_STATUS_INVALID){
            // Reverse! clear entry and claim it.
            dataAccX.setInt("crc", 0, index);
            dataAccX.setString("connector", "", index);
            dataAccX.setString("stream", "", index);
            dataAccX.setString("host", "", index);
            dataAccX.setInt("up", 0, index);
            dataAccX.setInt("down", 0, index);
            dataAccX.setInt("lastsecond", 0, index);
            dataAccX.setInt("time", 0, index);
            dataAccX.setInt("now", 0, index);
            dataAccX.setInt("sync", 0, index);
            dataAccX.setInt("killtime", 0, index);
            dataAccX.setInt("pid", 0, index);
            dataAccX.setInt("timer", 0, index);
            dataAccX.setInt("command", 0, index);
            dataAccX.setInt("status", 0, index);
            break;
          }
        }
        if (index == dataAccX.getEndPos()){dataAccX.addRecords(1);}
        sem.post();
      }
    }

    commonFieldAccess();

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

  std::string Statistics::getHost() const{return host.string(index);}
  std::string Statistics::getHost(size_t idx) const{return (master ? host.string(idx) : "");}
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
    if (rhs && rhs.isAlive()){
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

    master = _master;

    if (!currentSize){currentSize = COMMS_USERS_INITSIZE;}

    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_USERS, streamName.c_str());

    bool newPage = false;
    if (master){
      dataPage.init(userPageName, currentSize, false, false);
      if (dataPage){
        dataPage.master = true;
      }else{
        dataPage.init(userPageName, currentSize, true);
        newPage = true;
      }
    }else{
      dataPage.init(userPageName, currentSize, false);
    }
    if (!dataPage.mapped){
      HIGH_MSG("Unable to open page %s", userPageName);
      return;
    }

    if (master){
      if (newPage){
        dataAccX = Util::RelAccX(dataPage.mapped, false);
        addCommonFields();

        dataAccX.addField("track", RAX_32UINT);
        dataAccX.addField("keynum", RAX_32UINT);

        dataAccX.setRCount((currentSize - dataAccX.getOffset()) / dataAccX.getRSize());
        dataAccX.setReady();
      }else{
        dataAccX = Util::RelAccX(dataPage.mapped);
      }

    }else{
      dataAccX = Util::RelAccX(dataPage.mapped);
      if (index == INVALID_RECORD_INDEX || reIssue){
        sem.wait();

        for (index = 0; index < dataAccX.getEndPos(); ++index){
          if (dataAccX.getInt("status", index) == COMM_STATUS_INVALID){
            // Reverse! clear entry and claim it.
            dataAccX.setInt("keynum", 0, index);
            dataAccX.setInt("track", 0, index);
            dataAccX.setInt("killtime", 0, index);
            dataAccX.setInt("pid", 0, index);
            dataAccX.setInt("timer", 0, index);
            dataAccX.setInt("command", 0, index);
            dataAccX.setInt("status", 0, index);
            break;
          }
        }
        if (index == dataAccX.getEndPos()){dataAccX.addRecords(1);}
        sem.post();
      }
    }

    commonFieldAccess();

    track = dataAccX.getFieldAccX("track");
    keyNum = dataAccX.getFieldAccX("keynum");

    setPid(getpid());
  }

  void Users::reload(const std::string &_streamName, size_t idx, uint8_t initialState){
    reload(_streamName);
    if (dataPage.mapped){
      setTrack(idx);
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
