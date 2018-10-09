#pragma once
#include "input.h"
#include <mist/util.h>

namespace Mist{


  extern uint16_t maxEBMLFrameOffset;
  extern bool frameOffsetKnown;
#define PKT_COUNT 64

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
      packetData pkts[PKT_COUNT];
      uint64_t frameOffset;
      uint16_t smallestFrame;
      uint64_t lastTime;
      uint64_t ctr;
      uint64_t rem;
      trackPredictor(){
        flush();
      }
      bool hasPackets(bool finished = false){
        if (finished){
          return (ctr - rem > 0);
        }else{
          return (ctr - rem > 12);
        }
      }
      /// Clears all internal values, for reuse as-new.
      void flush(){
        frameOffset = 0;
        smallestFrame = 0;
        lastTime = 0;
        ctr = 0;
        rem = 0;
      }
      packetData & getPacketData(bool mustCalcOffsets){
        frameOffsetKnown = true;
        //grab the next packet to output
        packetData & p = pkts[rem % PKT_COUNT];
        //Substract the max frame offset, so we know all offsets are positive, no matter what.
        //if it's not the first and we're calculating offsets, see if we need an offset
        if (!mustCalcOffsets){
          p.time += maxEBMLFrameOffset;
          DONTEVEN_MSG("Outputting %llu + %llu (%llu -> %llu)", p.time, maxEBMLFrameOffset, rem, rem % PKT_COUNT);
          return p;
        }else{
          if (rem && !p.key){
            p.offset = p.time + maxEBMLFrameOffset - (lastTime + smallestFrame);
            //If we calculate an offset less than a frame away,
            //we assume it's just time stamp drift due to lack of precision.
            p.time = (lastTime + smallestFrame);
          }else{
            p.time -= frameOffset?frameOffset + smallestFrame:0;
            p.offset = maxEBMLFrameOffset + frameOffset + smallestFrame;
          }
        }
        lastTime = p.time;
        DONTEVEN_MSG("Outputting%s %llu + %llu, offset %llu (%llu -> %llu), display at %llu", (p.key?" (KEY)":""), p.time, frameOffset, p.offset, rem, rem % PKT_COUNT, p.time+p.offset);
        return p;
      }
      void add(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe, bool isVideo, void * dataPtr = 0){
        //If no packets have been removed yet and there is more than one packet, calculate frameOffset
        if (!rem && ctr && packTime < pkts[0].time){
          frameOffset = pkts[0].time - packTime;
          INSANE_MSG("Setting frameOffset to %llu", frameOffset);
        }
        if (isVideo && ctr && ctr >= rem){
          int32_t currOffset = packTime - pkts[(ctr-1)%PKT_COUNT].time;
          if (currOffset < 0){currOffset *= -1;}
          if (!smallestFrame || currOffset < smallestFrame){
            smallestFrame = currOffset;
            HIGH_MSG("Smallest frame is now %u", smallestFrame);
          }
          if (!frameOffsetKnown && currOffset < 8*smallestFrame && currOffset*2 > maxEBMLFrameOffset && ctr < PKT_COUNT/2){
            maxEBMLFrameOffset = currOffset*2;
            INFO_MSG("Max frame offset is now %u", maxEBMLFrameOffset);
          }
        }
        DONTEVEN_MSG("Ingesting %llu (%llu -> %llu)", packTime, ctr, ctr % PKT_COUNT);
        pkts[ctr % PKT_COUNT].set(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
        ++ctr;
        if (ctr == PKT_COUNT-1){frameOffsetKnown = true;}
      }
      void remove(){
        ++rem;
      }

  };

  class InputEBML : public Input{
  public:
    InputEBML(Util::Config *cfg);
    bool needsLock();
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
    void parseStreamHeader(){
      readHeader();
    }
    bool openStreamSource(){return true;}
    bool needHeader(){return needsLock() && !readExistingHeader();}
    double timeScale;
    bool wantBlocks;
  };
}

typedef Mist::InputEBML mistIn;

