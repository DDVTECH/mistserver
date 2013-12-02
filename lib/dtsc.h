/// \file dtsc.h
/// Holds all headers for DDVTECH Stream Container parsing/generation.

#pragma once
#include <vector>
#include <iostream>
#include <stdint.h> //for uint64_t
#include <string>
#include <deque>
#include <set>
#include <stdio.h> //for FILE
#include "json.h"
#include "socket.h"
#include "timing.h"

namespace DTSC {
  bool isFixed(JSON::Value & metadata);

  /// This enum holds all possible datatypes for DTSC packets.
  enum datatype{
    AUDIO, ///< Stream Audio data
    VIDEO, ///< Stream Video data
    META, ///< Stream Metadata
    PAUSEMARK, ///< Pause marker
    MODIFIEDHEADER, ///< Modified header data.
    INVALID ///< Anything else or no data available.
  };

  extern char Magic_Header[]; ///< The magic bytes for a DTSC header
  extern char Magic_Packet[]; ///< The magic bytes for a DTSC packet
  extern char Magic_Packet2[]; ///< The magic bytes for a DTSC packet version 2

  /// A simple structure used for ordering byte seek positions.
  struct seekPos {
    bool operator < (const seekPos& rhs) const {
      if (seekTime < rhs.seekTime){
        return true;
      }else{
        if (seekTime == rhs.seekTime){
          if (bytePos < rhs.bytePos){
            return true;
          }else{
            if (trackID < rhs.trackID){
              return true;
            }
          }
        }
      }
      return false;
    }
    long long unsigned int seekTime;
    long long unsigned int bytePos;
    unsigned int trackID;
  };

  /// A simple structure used for ordering byte seek positions.
  struct livePos {
    livePos(){
      seekTime = 0;
      trackID = 0;
    }
    livePos(const livePos & rhs){
      seekTime = rhs.seekTime;
      trackID = rhs.trackID;
    }
    void operator = (const livePos& rhs) {
      seekTime = rhs.seekTime;
      trackID = rhs.trackID;
    }
    bool operator == (const livePos& rhs) {
      return seekTime == rhs.seekTime && trackID == rhs.trackID;
    }
    bool operator != (const livePos& rhs) {
      return seekTime != rhs.seekTime || trackID != rhs.trackID;
    }
    bool operator < (const livePos& rhs) const {
      if (seekTime < rhs.seekTime){
        return true;
      }else{
        if (seekTime == rhs.seekTime){
          if (trackID < rhs.trackID){
            return true;
          }
        }
      }
      return false;
    }
    volatile long long unsigned int seekTime;
    volatile unsigned int trackID;
  };

  /// A part from the DTSC::Stream ringbuffer.
  /// Holds information about a buffer that will stay consistent
  class Ring{
    public:
      Ring(livePos v);
      livePos b;
      //volatile unsigned int b; ///< Holds current number of buffer. May and is intended to change unexpectedly!
      volatile bool waiting; ///< If true, this Ring is currently waiting for a buffer fill.
      volatile bool starved; ///< If true, this Ring can no longer receive valid data.
      volatile bool updated; ///< If true, this Ring should write a new header.
      volatile int playCount;
  };

  class Part{
    public:
      long getSize();
      void setSize(long newSize);
      short getDuration();
      void setDuration(short newDuration);
      long getOffset();
      void setOffset(long newOffset);
      char* getData();
    private:
      char data[9];
  };

  class Key{
    public:
      long long unsigned int getBpos();
      void setBpos(long long unsigned int newBpos);
      long getLength();
      void setLength(long newLength);
      short getNumber();
      void setNumber(short newNumber);
      short getParts();
      void setParts(short newParts);
      long getTime();
      void setTime(long newTime);
      char* getData();
    private:
      char data[16];
  };

  class Fragment{
    public:
      long getDuration();
      void setDuration(long newDuration);
      char getLength();
      void setLength(char newLength);
      short getNumber();
      void setNumber(short newNumber);
      long getSize();
      void setSize(long newSize);
      char* getData();
    private:
      char data[11];
  };

  class readOnlyTrack{
    public:
      readOnlyTrack();
      readOnlyTrack(JSON::Value & trackRef);
      int getSendLen();
      void send(Socket::Connection & conn);
      std::string getIdentifier();
      JSON::Value toJSON();
      long long unsigned int fragLen;
      Fragment* fragments;
      long long unsigned int keyLen;
      Key* keys;
      long long unsigned int partLen;
      Part* parts;
      int trackID;
      int length;
      int firstms;
      int lastms;
      int bps;
      int missedFrags;
      std::string init;
      std::string codec;
      std::string type;
      //audio only
      int rate;
      int size;
      int channels;
      //video only
      int width;
      int height;
      int fpks;
      //vorbis and theora only
      std::string idHeader;
      std::string commentHeader;
  };

  class Track : public readOnlyTrack {
    public:
      Track();
      Track(const readOnlyTrack & rhs);
      Track(JSON::Value & trackRef);
      inline operator bool() const {return parts.size();}
      void update(JSON::Value & pack);
      int getSendLen();
      void send(Socket::Connection & conn);
      JSON::Value toJSON();
      std::deque<Fragment> fragments;
      std::deque<Key> keys;
      std::deque<Part> parts;
      Key & getKey(int keyNum);
      void reset();
  };

  class readOnlyMeta {
    public:
      readOnlyMeta();
      readOnlyMeta(JSON::Value & meta);
      std::map<int,readOnlyTrack> tracks;
      bool vod;
      bool live;
      bool merged;
      long long int moreheader;
      long long int length;
      long long int bufferWindow;
      void send(Socket::Connection & conn);
      JSON::Value toJSON();
      bool isFixed();
  };

  class Meta : public readOnlyMeta {
    public:
      Meta();
      Meta(const readOnlyMeta & meta);
      Meta(JSON::Value & meta);
      inline operator bool() const {return vod || live;}
      std::map<int,Track> tracks;
      void update(JSON::Value & pack);
      void send(Socket::Connection & conn);
      JSON::Value toJSON();
      void reset();
      bool isFixed();
  };

  /// A simple wrapper class that will open a file and allow easy reading/writing of DTSC data from/to it.
  class File{
    public:
      File();
      File(const File & rhs);
      File(std::string filename, bool create = false);
      File & operator = (const File & rhs);
      operator bool() const;
      ~File();
      readOnlyMeta & getMeta();
      long long int getLastReadPos();
      bool writeHeader(std::string & header, bool force = false);
      long long int addHeader(std::string & header);
      long int getBytePosEOF();
      long int getBytePos();
      bool reachedEOF();
      void seekNext();
      void parseNext();
      std::string & getPacket();
      JSON::Value & getJSON();
      bool seek_time(int seconds);
      bool seek_time(int seconds, int trackNo, bool forceSeek = false);
      bool seek_bpos(int bpos);
      void writePacket(std::string & newPacket);
      void writePacket(JSON::Value & newPacket);
      bool atKeyframe();
      void selectTracks(std::set<int> & tracks);
    private:
      long int endPos;
      void readHeader(int pos);
      std::string strbuffer;
      JSON::Value jsonbuffer;
      JSON::Value metaStorage;
      readOnlyMeta metadata;
      std::map<int,std::string> trackMapping;
      long long int currtime;
      long long int lastreadpos;
      int currframe;
      FILE * F;
      unsigned long headerSize;
      char buffer[4];
      bool created;
      std::set<seekPos> currentPositions;
      std::set<int> selectedTracks;
  };
  //FileWriter

  /// Holds temporary data for a DTSC stream and provides functions to utilize it.
  /// Optionally also acts as a ring buffer of a certain requested size.
  /// If ring buffering mode is enabled, it will automatically grow in size to always contain at least one keyframe.
  class Stream{
    public:
      Stream();
      ~Stream();
      Stream(unsigned int buffers, unsigned int bufferTime = 0);
      Meta metadata;
      JSON::Value & getPacket();
      JSON::Value & getPacket(livePos num);
      datatype lastType();
      std::string & lastData();
      bool hasVideo();
      bool hasAudio();
      bool parsePacket(std::string & buffer);
      bool parsePacket(Socket::Buffer & buffer);
      std::string & outPacket();
      std::string & outPacket(livePos num);
      std::string & outHeader();
      Ring * getRing();
      unsigned int getTime();
      void dropRing(Ring * ptr);
      int canSeekms(unsigned int ms);
      livePos msSeek(unsigned int ms, std::set<int> & allowedTracks);
      void setBufferTime(unsigned int ms);
      bool isNewest(DTSC::livePos & pos, std::set<int> & allowedTracks);
      DTSC::livePos getNext(DTSC::livePos & pos, std::set<int> & allowedTracks);
      void endStream();
      void waitForMeta(Socket::Connection & sourceSocket);
    protected:
      void cutOneBuffer();
      void resetStream();
      std::map<livePos,JSON::Value> buffers;
      std::map<int,std::set<livePos> > keyframes;
      void addPacket(JSON::Value & newPack);
      void addMeta(JSON::Value & newMeta);
      datatype datapointertype;
      unsigned int buffercount;
      unsigned int buffertime;
      std::map<int,std::string> trackMapping;
      void deletionCallback(livePos deleting);
  };
}
