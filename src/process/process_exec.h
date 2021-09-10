#include "../input/input_ebml.h"
#include "../output/output_ebml.h"
#include <mist/defines.h>
#include <mist/json.h>

namespace Mist{
  bool getFirst = false;
  bool sendFirst = false;

  uint64_t packetTimeDiff;
  uint64_t sendPacketTime;
  JSON::Value opt; /// Options

  class ProcMKVExec{
  public:
    ProcMKVExec(){};
    bool CheckConfig();
    void Run();
  };

  class ProcessSink : public InputEBML{
  public:
    ProcessSink(Util::Config *cfg) : InputEBML(cfg){};
    void getNext(bool smart = true){
      static bool recurse = false;
      if (recurse){return InputEBML::getNext(smart);}
      recurse = true;
      InputEBML::getNext(smart);
      recurse = false;
      if (!getFirst){
        packetTimeDiff = sendPacketTime - thisPacket.getTime();
        getFirst = true;
      }
      uint64_t tmpLong;
      uint64_t packTime = thisPacket.getTime() + packetTimeDiff;
      // change packettime
      char *data = thisPacket.getData();
      tmpLong = htonl((int)(packTime >> 32));
      memcpy(data + 12, (char *)&tmpLong, 4);
      tmpLong = htonl((int)(packTime & 0xFFFFFFFF));
      memcpy(data + 16, (char *)&tmpLong, 4);
    }
    void setInFile(int stdin_val){
      inFile = fdopen(stdin_val, "r");
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      nProxy.streamName = streamName;
    }
    bool needsLock(){return false;}
    bool isSingular(){return false;}
  };

  class ProcessSource : public OutEBML{
  public:
    ProcessSource(Socket::Connection &c) : OutEBML(c){};
    void sendNext(){
      if (!sendFirst){
        sendPacketTime = thisPacket.getTime();
        sendFirst = true;
      }
      OutEBML::sendNext();
    }
  };
}// namespace Mist
