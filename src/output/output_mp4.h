#include "output_http.h"
#include <list>
#include <mist/http_parser.h>

namespace Mist{
  class keyPart{
  public:
    bool operator<(const keyPart &rhs) const{
      if (time < rhs.time){return true;}
      if (time > rhs.time){return false;}
      if (trackID < rhs.trackID){return true;}
      return (trackID == rhs.trackID && index < rhs.index);
    }
    size_t trackID;
    uint64_t time;
    uint64_t byteOffset; // Stores relative bpos for fragmented MP4
    uint64_t index;
    uint64_t firstIndex;
    size_t sampleSize;
    uint16_t sampleDuration;
    uint16_t sampleOffset;
  };

  class SortSet{
  private:
    Util::ResizeablePointer ptr;
    Util::ResizeablePointer avail;
    size_t entries;
    size_t currBegin;
    void findBegin();
    bool hasBegin;
  public:
    SortSet();
    const keyPart & begin();
    void erase();
    bool empty();
    void insert(const keyPart & part);
  };

  /// Class that implements a tiny subset of std::map, optimized for speed for our type of usage.
  template <class T> class QuickMap{
  private:
    Util::ResizeablePointer ptr;
    size_t entries;
  public:
    QuickMap(){
      entries = 0;
    }
    ~QuickMap(){
      size_t len = 8 + sizeof(T*);
      for (size_t i = 0; i < entries; ++i){
        delete *(T**)(void*)(ptr+len*i+8);
      }
    }
    T & get(uint64_t idx){
      static T blank;
      size_t len = 8 + sizeof(T*);
      for (size_t i = 0; i < entries; ++i){
        if (*((uint64_t*)(void*)(ptr+len*i)) == idx){
          return **(T**)(void*)(ptr+len*i+8);
        }
      }
      return blank;
    }
    void insert(uint64_t idx, T elem){
      size_t i = 0;
      size_t len = 8 + sizeof(T*);
      for (i = 0; i < entries; ++i){
        if (*((uint64_t*)(void*)(ptr+len*i)) == idx){
          *(T**)(void*)(ptr+len*i+8) = new T(elem);
          return;
        }
      }
      entries = i+1;
      ptr.allocate(len*entries);
      *(T**)(void*)(ptr+len*i+8) = new T(elem);
      *((uint64_t*)(void*)(ptr+len*i)) = idx;
    }
  };

  class OutMP4 : public HTTPOutput{
  public:
    OutMP4(Socket::Connection &conn);
    ~OutMP4();
    static void init(Util::Config *cfg);

    uint64_t mp4HeaderSize(uint64_t &fileSize, int fragmented = 0) const;
    bool mp4Header(Util::ResizeablePointer & headOut, uint64_t &size, int fragmented = 0);

    uint64_t mp4moofSize(uint64_t startFragmentTime, uint64_t endFragmentTime, uint64_t &mdatSize, std::map<size_t, DTSC::Keys *> & keysCache) const;
    virtual void sendFragmentHeaderTime(uint64_t startFragmentTime,
                                        uint64_t endFragmentTime); // this builds the moof box for fragmented MP4

    void findSeekPoint(uint64_t byteStart, uint64_t &seekPoint, uint64_t headerSize);
    void appendSinglePacketMoof(Util::ResizeablePointer& moofOut, size_t extraBytes = 0); 
    size_t fragmentHeaderSize(std::deque<size_t>& sortedTracks, std::set<keyPart>& trunOrder, uint64_t startFragmentTime, uint64_t endFragmentTime);
    void respondHTTP(const HTTP::Parser & req, bool headersOnly);
    void sendNext();
    void sendHeader();
    bool doesWebsockets() { return true; }
    void onWebsocketConnect();
    void onWebsocketFrame();
    virtual void dropTrack(size_t trackId, const std::string &reason, bool probablyBad = true);
  protected:
    bool isFileTarget(){return isRecording();}
    void sendWebsocketCodecData(const std::string& type);
    bool handleWebsocketSeek(JSON::Value& command);

    uint64_t fileSize;
    uint64_t byteStart;
    uint64_t byteEnd;
    int64_t leftOver;
    uint64_t currPos;
    uint64_t seekPoint;
    int64_t timeOffset;

    uint64_t nextHeaderTime;
    uint64_t headerSize;

    // variables for standard MP4
    std::set<keyPart> sortSet; // needed for unfragmented MP4, remembers the order of keyparts

    // variables for fragmented
    size_t fragSeqNum;       // the sequence number of the next keyframe/fragment when producing
                             // fragmented MP4's
    size_t vidTrack;         // the video track we use as fragmenting base
    uint64_t realBaseOffset; // base offset for every moof packet
    // from sendnext

    bool sending3GP;

    uint64_t startTime;
    uint64_t endTime;

    bool chromeWorkaround;
    int keysOnly;
    uint64_t estimateFileSize() const;

    std::string protectionHeader(size_t idx);
    Util::ResizeablePointer webBuf;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutMP4 mistOut;
#endif
