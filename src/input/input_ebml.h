#include "input.h"
#include <mist/util.h>

namespace Mist{

  class packetData{
    public:
    uint64_t time, offset, track, dsize, bpos;
    bool key;
    Util::ResizeablePointer ptr;
    packetData(){
      time = 0;
      offset = 0;
      track = 0;
      dsize = 0;
      bpos = 0;
      key = false;
    }
    void set(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe, void * dataPtr = 0){
      time = packTime;
      offset = packOffset;
      track = packTrack;
      dsize = packDataSize;
      bpos = packBytePos;
      key = isKeyframe;
      if (dataPtr){
        ptr.assign(dataPtr, packDataSize);
      }
    }
    packetData(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe, void * dataPtr = 0){
      set(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
    }
  };
  class trackPredictor{
    public:
      packetData pkts[16];
      uint16_t smallestFrame;
      uint64_t lastTime;
      uint64_t ctr;
      uint64_t rem;
      trackPredictor(){
        smallestFrame = 0;
        lastTime = 0;
        ctr = 0;
        rem = 0;
      }
      bool hasPackets(bool finished = false){
        if (finished){
          return (ctr - rem > 0);
        }else{
          return (ctr - rem > 8);
        }
      }
      packetData & getPacketData(bool mustCalcOffsets){
        packetData & p = pkts[rem % 16];
        if (rem && mustCalcOffsets){
          if (p.time > lastTime + smallestFrame){
            while (p.time - (lastTime + smallestFrame) > smallestFrame * 8){
              lastTime += smallestFrame;
            }
            p.offset = p.time - (lastTime + smallestFrame);
            p.time = lastTime + smallestFrame;
          }
        }
        lastTime = p.time;
        return p;
      }
      void add(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe, void * dataPtr = 0){
        if (ctr && ctr > rem){
          if ((pkts[(ctr-1)%16].time < packTime - 2) && (!smallestFrame || packTime - pkts[(ctr-1)%16].time < smallestFrame)){
            smallestFrame = packTime - pkts[(ctr-1)%16].time;
          }
        }
        pkts[ctr % 16].set(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
        ++ctr;
      }
      void remove(){
        ++rem;
      }

  };

  class InputEBML : public Input{
  public:
    InputEBML(Util::Config *cfg);

  protected:
    void fillPacket(packetData & C);
    bool checkArguments();
    bool preRun();
    bool readHeader();
    bool readElement();
    void getNext(bool smart = true);
    void seek(int seekTime);
    FILE *inFile;
    Util::ResizeablePointer ptr;
    bool readingMinimal;
    uint64_t lastClusterBPos;
    uint64_t lastClusterTime;
    uint64_t bufferedPacks;
    std::map<uint64_t, trackPredictor> packBuf;
    std::set<uint64_t> swapEndianness;
    bool readExistingHeader();
  };
}

typedef Mist::InputEBML mistIn;

