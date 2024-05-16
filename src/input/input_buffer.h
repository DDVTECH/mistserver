#include "input.h"
#include <fstream>
#include <mist/dtsc.h>
#include <mist/shared_memory.h>

namespace Mist{
  class InputBuffer : public Input{
  public:
    InputBuffer(Util::Config *cfg);
    ~InputBuffer();
    void onCrash();

  private:
    void fillBufferDetails(JSON::Value &details) const;
    uint64_t bufferTime;
    uint64_t cutTime;
    size_t segmentSize;  /*LTS*/
    uint64_t lastReTime; /*LTS*/
    uint64_t lastProcTime; /*LTS*/
    uint64_t firstProcTime; /*LTS*/
    uint64_t finalMillis;
    bool hasPush;//Is a push currently being received?
    bool everHadPush;//Was there ever a push received?
    bool allProcsRunning;
    bool resumeMode;
    uint64_t maxKeepAway;
    IPC::semaphore *liveMeta;

  protected:
    // Private Functions
    bool preRun();
    bool checkArguments(){return true;}
    void updateMeta();
    bool needHeader(){return false;}
    void getNext(size_t idx = INVALID_TRACK_ID){};
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID){};

    void removeTrack(size_t tid);

    bool removeKey(size_t tid);
    void removeUnused();
    void finish();

    uint64_t retrieveSetting(DTSC::Scan &streamCfg, const std::string &setting, const std::string &option = "");

    void userLeadIn();
    void userOnActive(size_t id);
    void userOnDisconnect(size_t id);
    void userLeadOut();
    // This is used for an ugly fix to prevent metadata from disappearing in some cases.
    std::map<size_t, std::string> initData;

    uint64_t findTrack(const std::string &trackVal);
    void checkProcesses(const JSON::Value &procs); // LTS
    std::map<std::string, pid_t> runningProcs;     // LTS

    std::set<size_t> generatePids;
    std::map<size_t, size_t> sourcePids;
  };
}// namespace Mist

#ifndef ONE_BINARY
#ifndef ONE_BINARY
typedef Mist::InputBuffer mistIn;
#endif
#endif
