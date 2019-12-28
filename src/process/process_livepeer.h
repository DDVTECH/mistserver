#include "../output/output_ts_base.h"
#include <mist/ts_stream.h>
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/stream.h>

namespace Mist{
  bool getFirst = false;
  bool sendFirst = false;
  bool doingSetup = true;
  bool queueClear = false;

  uint64_t packetTimeDiff;
  uint64_t sendPacketTime;
  uint64_t keyCount = 0;
  JSON::Value opt; /// Options

  size_t nextFreeID = 0;

  class readySegment{
    public:
      uint64_t time;
      uint64_t lastPacket;
      int64_t timeOffset;
      uint64_t byteOffset;
      bool offsetCalcd;
      size_t ID;
      bool fullyRead;
      bool fullyWritten;
      TS::Stream S;
      Util::ResizeablePointer data;
      readySegment(){
        ID = nextFreeID++;
        time = 0;
        timeOffset = 0;
        byteOffset = 0;
        fullyRead = true;
        fullyWritten = false;
        offsetCalcd = false;
        lastPacket = 0;
      };
      void set(uint64_t t, void * ptr, size_t len){
        time = t;
        data.assign(ptr, len);
        fullyRead = false;
        fullyWritten = true;
        offsetCalcd = false;
        byteOffset = 0;
      }
  };


  std::map<std::string, readySegment> segs;

  JSON::Value lpEnc;
  JSON::Value lpBroad;
  std::string currBroadAddr;
  std::string lpID;

  class ProcLivepeer{
  public:
    std::string api_url;
    ProcLivepeer(){};
    bool CheckConfig();
    void Run();
  };

}// namespace Mist

