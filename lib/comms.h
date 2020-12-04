#pragma once
#include "procs.h"
#include "shared_memory.h"
#include "util.h"

#define COMM_STATUS_SOURCE 0x80
#define COMM_STATUS_DONOTTRACK 0x40
#define COMM_STATUS_DISCONNECT 0x20
#define COMM_STATUS_REQDISCONNECT 0x10
#define COMM_STATUS_ACTIVE 0x1
#define COMM_STATUS_INVALID 0x0


#define COMM_LOOP(comm, onActive, onDisconnect) \
  {\
    for (size_t id = 0; id < comm.recordCount(); id++){\
      if (comm.getStatus(id) == COMM_STATUS_INVALID){continue;}\
      if (!Util::Procs::isRunning(comm.getPid(id))){\
        comm.setStatus(COMM_STATUS_DISCONNECT | comm.getStatus(id), id);\
      }\
      onActive;\
      if (comm.getStatus(id) & COMM_STATUS_DISCONNECT){\
        onDisconnect;\
        comm.setStatus(COMM_STATUS_INVALID, id);\
      }\
    }\
  }

namespace Comms{
  class Comms{
  public:
    Comms();
    ~Comms();
    operator bool() const;
    void reload(const std::string & prefix, size_t baseSize, bool _master = false, bool reIssue = false);
    virtual void addFields();
    virtual void nullFields();
    virtual void fieldAccess();
    size_t recordCount() const;
    uint8_t getStatus() const;
    uint8_t getStatus(size_t idx) const;
    void setStatus(uint8_t _status);
    void setStatus(uint8_t _status, size_t idx);
    uint64_t getCommand() const;
    uint64_t getCommand(size_t idx) const;
    void setCommand(uint64_t _cmd);
    void setCommand(uint64_t _cmd, size_t idx);
    uint32_t getPid() const;
    uint32_t getPid(size_t idx) const;
    void setPid(uint32_t _pid);
    void setPid(uint32_t _pid, size_t idx);
    void finishAll();
    void setMaster(bool _master);
    const std::string &pageName() const{return dataPage.name;}

  protected:
    bool master;
    size_t index;
    size_t currentSize;
    IPC::semaphore sem;
    IPC::sharedPage dataPage;
    Util::RelAccX dataAccX;
    Util::FieldAccX status;
    Util::FieldAccX pid;
  };

  class Statistics : public Comms{
  public:
    Statistics();
    operator bool() const{return dataPage.mapped && (master || index != INVALID_RECORD_INDEX);}
    void unload();
    void reload(bool _master = false, bool reIssue = false);
    virtual void addFields();
    virtual void nullFields();
    virtual void fieldAccess();

    uint8_t getSync() const;
    uint8_t getSync(size_t idx) const;
    void setSync(uint8_t _sync);
    void setSync(uint8_t _sync, size_t idx);

    uint64_t getNow() const;
    uint64_t getNow(size_t idx) const;
    void setNow(uint64_t _now);
    void setNow(uint64_t _now, size_t idx);

    uint64_t getTime() const;
    uint64_t getTime(size_t idx) const;
    void setTime(uint64_t _time);
    void setTime(uint64_t _time, size_t idx);

    uint64_t getLastSecond() const;
    uint64_t getLastSecond(size_t idx) const;
    void setLastSecond(uint64_t _lastSecond);
    void setLastSecond(uint64_t _lastSecond, size_t idx);

    uint64_t getDown() const;
    uint64_t getDown(size_t idx) const;
    void setDown(uint64_t _down);
    void setDown(uint64_t _down, size_t idx);

    uint64_t getUp() const;
    uint64_t getUp(size_t idx) const;
    void setUp(uint64_t _up);
    void setUp(uint64_t _up, size_t idx);

    std::string getHost() const;
    std::string getHost(size_t idx) const;
    void setHost(std::string _host);
    void setHost(std::string _host, size_t idx);

    std::string getStream() const;
    std::string getStream(size_t idx) const;
    void setStream(std::string _stream);
    void setStream(std::string _stream, size_t idx);

    std::string getConnector() const;
    std::string getConnector(size_t idx) const;
    void setConnector(std::string _connector);
    void setConnector(std::string _connector, size_t idx);

    uint32_t getCRC() const;
    uint32_t getCRC(size_t idx) const;
    void setCRC(uint32_t _crc);
    void setCRC(uint32_t _crc, size_t idx);

    std::string getSessId() const;
    std::string getSessId(size_t index) const;

  private:
    Util::FieldAccX sync;
    Util::FieldAccX now;
    Util::FieldAccX time;
    Util::FieldAccX lastSecond;
    Util::FieldAccX down;
    Util::FieldAccX up;
    Util::FieldAccX host;
    Util::FieldAccX stream;
    Util::FieldAccX connector;
    Util::FieldAccX crc;
  };

  class Users : public Comms{
  public:
    Users();
    Users(const Users &rhs);
    void reload(const std::string &_streamName = "", bool _master = false, bool reIssue = false);
    void reload(const std::string &_streamName, size_t track, uint8_t initialState = COMM_STATUS_ACTIVE);
    virtual void addFields();
    virtual void nullFields();
    virtual void fieldAccess();

    operator bool() const{return dataPage.mapped;}

    uint32_t getTrack() const;
    uint32_t getTrack(size_t idx) const;
    void setTrack(uint32_t _track);
    void setTrack(uint32_t _track, size_t idx);

    size_t getKeyNum() const;
    size_t getKeyNum(size_t idx) const;
    void setKeyNum(size_t _keyNum);
    void setKeyNum(size_t _keyNum, size_t idx);

  private:
    std::string streamName;

    Util::FieldAccX track;
    Util::FieldAccX keyNum;
  };
}// namespace Comms
