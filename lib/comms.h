#pragma once
#include "procs.h"
#include "shared_memory.h"
#include "util.h"


#define COMM_LOOP(comm, onActive, onDisconnect) \
  {\
    for (size_t id = 0; id < comm.recordCount(); id++){\
      if (comm.getStatus(id) == COMM_STATUS_INVALID){continue;}\
      if (!(comm.getStatus(id) & COMM_STATUS_DISCONNECT) && comm.getPid(id) && !Util::Procs::isRunning(comm.getPid(id))){\
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
  extern uint8_t sessionViewerMode;
  extern uint8_t sessionInputMode;
  extern uint8_t sessionOutputMode;
  extern uint8_t sessionUnspecifiedMode;
  extern uint8_t sessionStreamInfoMode;
  extern uint8_t tknMode;
  void sessionConfigCache();

  class Comms{
  public:
    Comms();
    virtual ~Comms();
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
    uint64_t index;
    size_t currentSize;
    IPC::semaphore sem;
    IPC::sharedPage dataPage;
    Util::RelAccX dataAccX;
    Util::FieldAccX status;
    Util::FieldAccX pid;
  };

  class Connections : public Comms{
  public:
    void reload(const std::string & streamName, const std::string & ip, const std::string & tkn, const std::string & protocol, const std::string & reqUrl, bool _master = false, bool reIssue = false);
    void reload(const std::string & sessId, bool _master = false, bool reIssue = false);
    void unload();
    operator bool() const{return dataPage.mapped && (master || index != INVALID_RECORD_INDEX);}
    std::string generateSession(const std::string & streamName, const std::string & ip, const std::string & tkn, const std::string & connector, uint64_t sessionMode);
    std::string sessionId;
    std::string initialTkn;

    void setExit();
    bool getExit();
    
    virtual void addFields();
    virtual void nullFields();
    virtual void fieldAccess();

    const std::string & getTkn() const{return initialTkn;}

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
    bool hasConnector(size_t idx, std::string protocol);

    uint64_t getPacketCount() const;
    uint64_t getPacketCount(size_t idx) const;
    void setPacketCount(uint64_t _count);
    void setPacketCount(uint64_t _count, size_t idx);

    uint64_t getPacketLostCount() const;
    uint64_t getPacketLostCount(size_t idx) const;
    void setPacketLostCount(uint64_t _lost);
    void setPacketLostCount(uint64_t _lost, size_t idx);

    uint64_t getPacketRetransmitCount() const;
    uint64_t getPacketRetransmitCount(size_t idx) const;
    void setPacketRetransmitCount(uint64_t _retransmit);
    void setPacketRetransmitCount(uint64_t _retransmit, size_t idx);

  protected:
    Util::FieldAccX now;
    Util::FieldAccX time;
    Util::FieldAccX lastSecond;
    Util::FieldAccX down;
    Util::FieldAccX up;
    Util::FieldAccX host;
    Util::FieldAccX stream;
    Util::FieldAccX connector;
    Util::FieldAccX sessId;
    Util::FieldAccX tags;
    Util::FieldAccX pktcount;
    Util::FieldAccX pktloss;
    Util::FieldAccX pktretrans;
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

  class Sessions : public Connections{
    public:
      Sessions();
      void reload(bool _master = false, bool reIssue = false);
      std::string getSessId() const;
      std::string getSessId(size_t idx) const;
      void setSessId(std::string _sid);
      void setSessId(std::string _sid, size_t idx);
      bool sessIdExists(std::string _sid);
      virtual void addFields();
      virtual void nullFields();
      virtual void fieldAccess();

      std::string getTags() const;
      std::string getTags(size_t idx) const;
      void setTags(std::string _sid);
      void setTags(std::string _sid, size_t idx);
  };
}// namespace Comms
