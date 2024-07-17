#pragma once
#include "input.h"
#include <mist/util.h>
#include <mist/urireader.h>

namespace Mist{

#define PKT_COUNT 24

  class packetData{
  public:
    uint64_t time, offset, track, dsize, bpos;
    bool key;
    Util::ResizeablePointer ptr;
    packetData() : time(0), offset(0), track(0), dsize(0), bpos(0), key(false){}
    void set(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize,
             uint64_t packBytePos, bool isKeyframe, void *dataPtr = 0){
      time = packTime;
      offset = packOffset;
      track = packTrack;
      dsize = packDataSize;
      bpos = packBytePos;
      key = isKeyframe;
      if (dataPtr){ptr.assign(dataPtr, packDataSize);}
    }
    packetData(uint64_t packTime, uint64_t packOffset, uint64_t packTrack, uint64_t packDataSize,
               uint64_t packBytePos, bool isKeyframe, void *dataPtr = 0){
      set(packTime, packOffset, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
    }
  };
  class trackPredictor{
  public:
    packetData pkts[PKT_COUNT]; /// Buffer for packet data
    uint64_t times[PKT_COUNT];  /// Sorted timestamps of buffered packets
    size_t maxDelay;            /// Maximum amount of bframes we expect
    uint32_t timeOffset;        /// Milliseconds we need to subtract from times so that offsets are always > 0
    uint64_t ctr;               /// ingested frame count
    uint64_t rem;               /// removed frame count
    bool initialized;
    trackPredictor(){
      initialized = false;
      maxDelay = 0;
      timeOffset = 0;
      flush();
    }
    bool hasPackets(bool finished = false){
      if (finished){
        return (ctr - rem > 0);
      }else{
        return ((initialized || ctr > 16) && ctr - rem > maxDelay);
      }
    }
    /// Clears all internal values, for reuse as-new.
    void flush(){
      ctr = 0;
      rem = 0;
    }
    packetData &getPacketData(bool mustCalcOffsets){
      // grab the next packet to output
      packetData &p = pkts[rem % PKT_COUNT];
      if (!mustCalcOffsets || !maxDelay){
        initialized = true;
        return p;
      }
      //Calculate the timeOffset when extracting the first frame
      if (!initialized){
        size_t buffLen = (ctr-rem-1) % PKT_COUNT;
        for (size_t i = 0; i <= buffLen; ++i){
          if (pkts[i].time < times[i]){
            if (times[i] - pkts[i].time > timeOffset){
              timeOffset = times[i] - pkts[i].time;
            }
          }
          DONTEVEN_MSG("Checking time offset against entry %zu/%zu: %" PRIu64 "-%" PRIu64 " = %" PRIu32, i, buffLen, times[i], pkts[i].time, timeOffset);
        }
        MEDIUM_MSG("timeOffset calculated to be %" PRIu32 ", max frame delay %zu", timeOffset, maxDelay);
        initialized = true;
      }

      uint64_t origTime = p.time;
      //Set new timestamp to first time in sorted array
      p.time = times[0];
      //Subtract timeOffset if possible
      if (p.time >= timeOffset){p.time -= timeOffset;}
      //If possible, calculate offset based on original timestamp difference with new timestamp
      if (origTime > p.time){p.offset = origTime-p.time;}
      //Less than 3 milliseconds off? Assume we needed 0 and it's a rounding error in timestamps.
      if (p.offset < 3){p.offset = 0;}
      DONTEVEN_MSG("Outputting%s %" PRIu64 "+%" PRIu64 " (#%" PRIu64 "), display at %" PRIu64,
                 (p.key ? " KEY" : ""), p.time, p.offset, rem, p.time + p.offset);
      return p;
    }

    void add(uint64_t packTime, uint64_t packTrack, uint64_t packDataSize,
             uint64_t packBytePos, bool isKeyframe, bool isVideo, void *dataPtr = 0){
      pkts[ctr % PKT_COUNT].set(packTime, 0, packTrack, packDataSize, packBytePos, isKeyframe, dataPtr);
      ++ctr;
      if (!isVideo){return;}
      size_t buffLen = ctr-rem-1;
      //Just in case somebody messed up, ensure we don't go out of our PKT_COUNT sized array
      if (buffLen >= PKT_COUNT){buffLen = PKT_COUNT - 1;}
      times[buffLen] = packTime;
      if (buffLen){
        //Swap the times while the previous is higher than the current
        size_t i = buffLen;
        while (i && times[i] < times[i-1]){
          uint64_t tmp = times[i-1];
          times[i-1] = times[i];
          times[i] = tmp;
          --i;
          //Keep track of maximum delay
          if (!initialized && buffLen - i + 1 > maxDelay){
            maxDelay = buffLen - i + 1;
          }
        }
      }
    }
    void remove(){
      ++rem;
      size_t buffLen = ctr-rem;
      if (buffLen >= PKT_COUNT){buffLen = PKT_COUNT-1;}
      for (size_t i = 0; i < buffLen; ++i){times[i] = times[i+1];}
    }
  };

  class InputEBML : public Input, public Util::DataCallback{
  public:
    InputEBML(Util::Config *cfg);
    bool needsLock();
    virtual bool isSingular(){return standAlone && !config->getBool("realtime");}
    virtual void dataCallback(const char *ptr, size_t size);
    virtual size_t getDataCallbackPos() const;

  protected:

    HTTP::URIReader inFile;
    Util::ResizeablePointer readBuffer;
    uint64_t readBufferOffset;
    uint64_t readPos;
    bool firstRead;

    virtual size_t streamByteCount(){
      return totalBytes;
    }; // For live streams: to update the stats with correct values.
    void fillPacket(packetData &C);
    bool checkArguments();
    bool preRun();
    bool readHeader();
    void postHeader();
    bool readElement();
    void getNext(size_t idx = INVALID_TRACK_ID);
    void seek(uint64_t seekTime, size_t idx = INVALID_TRACK_ID);
    void clearPredictors();
    bool readingMinimal;
    uint64_t lastClusterBPos;
    uint64_t lastClusterTime;
    uint64_t bufferedPacks;
    std::map<uint64_t, trackPredictor> packBuf;
    std::set<uint64_t> swapEndianness;
    bool readExistingHeader();
    void parseStreamHeader(){readHeader();}
    bool openStreamSource(){return true;}
    bool needHeader(){return (config->getBool("realtime") || needsLock()) && !readExistingHeader();}
    double timeScale;
    bool wantBlocks;
    size_t totalBytes;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::InputEBML mistIn;
#endif
