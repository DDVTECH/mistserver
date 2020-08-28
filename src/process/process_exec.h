#include "../input/input_ebml.h"
#include "../output/output_ebml.h"
#include <mist/defines.h>
#include <mist/json.h>
#include <mist/stream.h>

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
    ProcessSink(Util::Config *cfg) : InputEBML(cfg){
      capa["name"] = "MKVExec";
    };
    void getNext(size_t idx = INVALID_TRACK_ID){
      static bool recurse = false;
      if (recurse){return InputEBML::getNext(idx);}
      recurse = true;
      InputEBML::getNext(idx);
      recurse = false;
      if (thisPacket){
        if (!getFirst){
          packetTimeDiff = sendPacketTime - thisPacket.getTime();
          getFirst = true;
        }
        uint64_t packTime = thisPacket.getTime() + packetTimeDiff;
        // change packettime
        char *data = thisPacket.getData();
        Bit::htobll(data + 12, packTime);
      }
    }
    void setInFile(int stdin_val){
      inFile = fdopen(stdin_val, "r");
      streamName = opt["sink"].asString();
      if (!streamName.size()){streamName = opt["source"].asString();}
      Util::streamVariables(streamName, opt["source"].asString());
      Util::setStreamName(opt["source"].asString() + "→" + streamName);
    }
    bool needsLock(){return false;}
    bool isSingular(){return false;}
  };

  class ProcessSource : public OutEBML{
  public:
    bool isRecording(){return false;}
    ProcessSource(Socket::Connection &c) : OutEBML(c){
      capa["name"] = "MKVExec";
      realTime = 0;
    };
    void sendHeader(){
      if (opt["masksource"].asBool()){
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti != userSelect.end(); ++ti){
          if (ti->first == INVALID_TRACK_ID){continue;}
          INFO_MSG("Masking source track %zu", ti->first);
          meta.validateTrack(ti->first, meta.trackValid(ti->first) & ~(TRACK_VALID_EXT_HUMAN | TRACK_VALID_EXT_PUSH));
        }
      }
      realTime = 0;
      OutEBML::sendHeader();
    };
    void sendNext(){
      extraKeepAway = 0;
      needsLookAhead = 0;
      maxSkipAhead = 0;
      if (!sendFirst){
        sendPacketTime = thisPacket.getTime();
        sendFirst = true;
        /*
        uint64_t maxJitter = 1;
        for (std::map<size_t, Comms::Users>::iterator ti = userSelect.begin(); ti !=
        userSelect.end(); ++ti){if (!M.trackValid(ti->first)){continue;
          }// ignore missing tracks
          if (M.getMinKeepAway(ti->first) > maxJitter){
            maxJitter = M.getMinKeepAway(ti->first);
          }
        }
        DTSC::veryUglyJitterOverride = maxJitter;
        */
      }
      OutEBML::sendNext();
    }
  };
}// namespace Mist
