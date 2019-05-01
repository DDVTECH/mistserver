#pragma once
#include "input.h"
#include <mist/util.h>

namespace Mist{


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
      uint64_t frameOffset;///The static average offset between transmit time and display time
      bool frameOffsetKnown;///Whether the average frame offset is known
      uint16_t smallestFrame;///low-ball estimate of time per frame
      uint64_t lastTime;///last send transmit timestamp
      uint64_t ctr;///ingested frame count
      uint64_t rem;///removed frame count
      uint64_t maxOffset;///maximum offset for this track
      uint64_t lowestTime;///First timestamp to enter the buffer
      trackPredictor(){
        smallestFrame = 0xFFFF;
        frameOffsetKnown = false;
        frameOffset = 0;
        maxOffset = 0;
        flush();
      }
      bool hasPackets(bool finished = false){
        if (finished || frameOffsetKnown){
          return (ctr - rem > 0);
        }else{
          return (ctr - rem > 12);
        }
      }
      /// Clears all internal values, for reuse as-new.
      void flush(){
        lastTime = 0;
        ctr = 0;
        rem = 0;
        lowestTime = 0;
      }
      packetData & getPacketData(bool mustCalcOffsets){
        //grab the next packet to output
        packetData & p = pkts[rem % PKT_COUNT];
        if (!mustCalcOffsets){
          frameOffsetKnown = true;
          return p;
        }
        if (rem && !p.key){
          p.offset = p.time - (lastTime + smallestFrame) + frameOffset;
          if (p.offset > maxOffset){
            uint64_t diff = p.offset - maxOffset;
            p.offset -= diff;
            lastTime += diff;
          }
          //If we calculate an offset less than a frame away,
          //we assume it's just time stamp drift due to lack of precision.
          p.time = (lastTime + smallestFrame);
        }else{
          if (!frameOffsetKnown){
            for (uint64_t i = 1; i < ctr; ++i){
              uint64_t timeDiff = pkts[i%PKT_COUNT].time - lowestTime;
              uint64_t timeExpt = smallestFrame*i;
              if (timeDiff > timeExpt && maxOffset < timeDiff-timeExpt){
                maxOffset = timeDiff-timeExpt;
              }
              if (timeDiff < timeExpt && frameOffset < timeExpt-timeDiff){
                frameOffset = timeExpt - timeDiff;
              }
            }
            maxOffset += frameOffset;
            HIGH_MSG("smallestFrame=%" PRIu16 ", frameOffset=%" PRIu64 ", maxOffset=%" PRIu64, smallestFrame, frameOffset, maxOffset);
            frameOffsetKnown = true;
          }
          p.offset = frameOffset;
        }
        lastTime = p.time;
        INSANE_MSG("Outputting%s %llu+%llu (#%llu, Max=%llu), display at %llu", (p.key?"KEY":""), p.time, p.offset, rem, maxOffset, p.time+p.offset);
        return p;
      }
      void add(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize, uint64_t packBytePos, bool isKeyframe, bool isVideo, void * dataPtr = 0){
        if (!ctr){lowestTime = packTime;}
        if (packTime > lowestTime && packTime - lowestTime < smallestFrame){smallestFrame = packTime - lowestTime;}
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

