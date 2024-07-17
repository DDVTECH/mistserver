#pragma once
#include <cstdlib>
#include <fstream>
#include <map>
#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/shared_memory.h>
#include <mist/timing.h>
#include <mist/url.h>
#include <set>

#ifdef SSL
#include <mist/encryption.h>
#endif

#include "../io.h"

namespace Mist{
  struct booking{
    uint32_t first;
    uint32_t curKey;
    uint32_t curPart;
  };

  struct trackKey{
    size_t track;
    size_t key;
    trackKey(){track = 0; key = 0;}
    trackKey(size_t t, size_t k){
      track = t;
      key = k;
    }
  };

  inline bool operator< (const trackKey a, const trackKey b){
    return a.track < b.track || (a.track == b.track && a.key < b.key);
  }

  class Input : public InOutBase{
  public:
    Input(Util::Config *cfg);
    virtual int run();
    virtual void onCrash(){}
    virtual int boot(int argc, char *argv[]);
    virtual ~Input(){};

    bool keepAlive();
    void reloadClientMeta();
    bool hasMeta() const;
    bool trackLoaded(size_t idx) const;
    static Util::Config *config;
    virtual bool needsLock(){return !config->getBool("realtime");}
    virtual bool publishesTracks(){return true;}

    static JSON::Value capa;

  protected:
    bool internalOnly;
    bool isBuffer;
    Comms::Connections statComm;
    uint64_t startTime;
    uint64_t lastStats;
    uint64_t inputTimeout;

    virtual bool checkArguments() = 0;
    virtual bool readHeader();
    virtual bool needHeader(){return !readExistingHeader();}
    virtual void postHeader(){};
    virtual bool preRun(){return true;}
    virtual bool isThread(){return false;}
    virtual bool isSingular(){return !config->getBool("realtime");}
    virtual bool readExistingHeader();
    virtual bool atKeyFrame();
    virtual void getNext(size_t idx = INVALID_TRACK_ID){}
    virtual void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID){}
    virtual void finish();
    virtual bool keepRunning();
    virtual bool openStreamSource(){return readHeader();}
    virtual void closeStreamSource(){}
    virtual void parseStreamHeader(){}
    virtual void checkHeaderTimes(const HTTP::URL & streamFile);
    virtual void removeUnused();
    virtual void convert();
    virtual void serve();
    virtual void inputServeStats();
    virtual void stream();
    virtual std::string getConnectedBinHost(){return std::string("\000\000\000\000\000\000\000\000\000\000\000\000\000\000\000\001", 16);}
    virtual size_t streamByteCount(){
      return 0;
    }; // For live streams: to update the stats with correct values.
    virtual void streamMainLoop();
    virtual void realtimeMainLoop();
    bool isAlwaysOn();

    virtual void userLeadIn();
    virtual void userOnActive(size_t id);
    virtual void userOnDisconnect(size_t id);
    virtual void userLeadOut();
    virtual void connStats(Comms::Connections & statComm);
    virtual void parseHeader();
    virtual JSON::Value enumerateSources(const std::string &){ return JSON::Value(); };
    virtual JSON::Value getSourceCapa(const std::string &){ return JSON::Value(); };
    bool bufferFrame(size_t track, uint32_t keyNum);
    void doInputAbortTrigger(pid_t pid, char *mRExitReason, char *exitReason);
    bool exitAndLogReason();

    uint64_t activityCounter;

    std::map<size_t, std::set<uint64_t> > keyTimes;
    std::map<trackKey, uint64_t> keyLoadPriority;

    // Create server for user pages
    Comms::Users users;
    size_t connectedUsers;

#ifdef SSL
    Encryption::AES aesCipher;
#endif

    IPC::sharedPage streamStatus;

    std::map<size_t, std::map<uint32_t, uint64_t> > pageCounter;

    static Input *singleton;

    bool hasSrt;
    std::ifstream srtSource;
    unsigned int srtTrack;

    void readSrtHeader();
    void getNextSrt(bool smart = true);
    DTSC::Packet srtPack;

    uint64_t simStartTime;

    IPC::sharedPage pidPage; ///Stores responsible input process PID
    bool bufferActive(); ///< Returns true if the buffer process for this stream input is alive.
    pid_t bufferPid;
    uint64_t lastBufferCheck;///< Time of last buffer liveness check.

    void handleBuyDRM();
  };
}// namespace Mist
