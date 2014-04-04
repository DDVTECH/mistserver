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

  ///\brief This enum holds all possible datatypes for DTSC packets.
  enum datatype {
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

  ///\brief A simple structure used for ordering byte seek positions.
  struct seekPos {
    ///\brief Less-than comparison for seekPos structures.
    ///\param rhs The seekPos to compare with.
    ///\return Whether this object is smaller than rhs.
    bool operator < (const seekPos & rhs) const {
      if (seekTime < rhs.seekTime) {
        return true;
      } else {
        if (seekTime == rhs.seekTime) {
          if (trackID < rhs.trackID) {
            return true;
          }
        }
      }
      return false;
    }
    long long unsigned int seekTime;///< Stores the timestamp of the DTSC packet referenced by this structure.
    long long unsigned int bytePos;///< Stores the byteposition of the DTSC packet referenced by this structure.
    unsigned int trackID;///< Stores the track the DTSC packet referenced by this structure is associated with.
  };

  enum packType{
    DTSC_INVALID,
    DTSC_HEAD,
    DTSC_V1,
    DTSC_V2
  };

  /// DTSC::Packets can currently be three types:
  /// DTSC_HEAD packets are the "DTSC" header string, followed by 4 bytes len and packed content.
  /// DTSC_V1 packets are "DTPD", followed by 4 bytes len and packed content.
  /// DTSC_V2 packets are "DTP2", followed by 4 bytes len, 4 bytes trackID, 8 bytes time, and packed content.
  /// The len is always without the first 8 bytes counted.
  class Packet {
    public:
      Packet();
      Packet(const Packet & rhs);
      Packet(const char * data_, unsigned int len, bool noCopy = false);
      ~Packet();
      void null();
      void operator = (const Packet & rhs);
      operator bool() const;
      packType getVersion();
      void reInit(const char * data_, unsigned int len, bool noCopy = false);
      void getString(const char * identifier, char *& result, int & len);
      void getString(const char * identifier, std::string & result);
      void getInt(const char * identifier, int & result);
      int getInt(const char * identifier);
      void getFlag(const char * identifier, bool & result);
      bool getFlag(const char * identifier);
      bool hasMember(const char * identifier);
      long long unsigned int getTime();
      long int getTrackId();
      char * getData();
      int getDataLen();
      JSON::Value toJSON();
    protected:
      bool master;
      packType version;
      char * findIdentifier(const char * identifier);
      void resize(unsigned int size);
      char * data;
      unsigned int bufferLen;
      unsigned int dataLen;
  };

  /// A simple structure used for ordering byte seek positions.
  struct livePos {
    livePos() {
      seekTime = 0;
      trackID = 0;
    }
    livePos(const livePos & rhs) {
      seekTime = rhs.seekTime;
      trackID = rhs.trackID;
    }
    void operator = (const livePos & rhs) {
      seekTime = rhs.seekTime;
      trackID = rhs.trackID;
    }
    bool operator == (const livePos & rhs) {
      return seekTime == rhs.seekTime && trackID == rhs.trackID;
    }
    bool operator != (const livePos & rhs) {
      return seekTime != rhs.seekTime || trackID != rhs.trackID;
    }
    bool operator < (const livePos & rhs) const {
      if (seekTime < rhs.seekTime) {
        return true;
      } else {
        if (seekTime > rhs.seekTime) {
          return false;
        }
      }
      return (trackID < rhs.trackID);
    }
    volatile long long unsigned int seekTime;
    volatile unsigned int trackID;
  };

  /// A part from the DTSC::Stream ringbuffer.
  /// Holds information about a buffer that will stay consistent
  class Ring {
    public:
      Ring(livePos v);
      livePos b;
      //volatile unsigned int b; ///< Holds current number of buffer. May and is intended to change unexpectedly!
      volatile bool waiting; ///< If true, this Ring is currently waiting for a buffer fill.
      volatile bool starved; ///< If true, this Ring can no longer receive valid data.
      volatile bool updated; ///< If true, this Ring should write a new header.
      volatile int playCount;
  };

  /*LTS-START*/
  ///\brief Basic class supporting initialization Vectors.
  ///
  ///These are used for encryption of data.
  class Ivec {
    public:
      Ivec();
      Ivec(long long int iVec);
      void setIvec(long long int iVec);
      void setIvec(std::string iVec);
      void setIvec(char * iVec, int len);
      long long int asInt();
      char * getData();
    private:
      ///\brief Data storage for this initialization vector.
      ///
      /// - 8 bytes: MSB storage of the initialization vector.
      char data[8];
  };
  /*LTS-END*/

  ///\brief Basic class for storage of data associated with single DTSC packets, a.k.a. parts.
  class Part {
    public:
      long getSize();
      void setSize(long newSize);
      short getDuration();
      void setDuration(short newDuration);
      long getOffset();
      void setOffset(long newOffset);
      char * getData();
      void toPrettyString(std::stringstream & str, int indent = 0);
    private:
      ///\brief Data storage for this packet.
      ///
      /// - 3 bytes: MSB storage of the payload size of this packet in bytes.
      /// - 2 bytes: MSB storage of the duration of this packet in milliseconds.
      /// - 4 bytes: MSB storage of the presentation time offset of this packet in milliseconds.
      char data[9];
  };

  ///\brief Basic class for storage of data associated with keyframes.
  ///
  /// When deleting this object, make sure to remove all DTSC::Part associated with it, if any. If you fail doing this, it *will* cause data corruption.
  class Key {
    public:
      long long unsigned int getBpos();
      void setBpos(long long unsigned int newBpos);
      long getLength();
      void setLength(long newLength);
      unsigned short getNumber();
      void setNumber(unsigned short newNumber);
      short getParts();
      void setParts(short newParts);
      long getTime();
      void setTime(long newTime);
      char * getData();
      void toPrettyString(std::stringstream & str, int indent = 0);
    private:
      ///\brief Data storage for this packet.
      ///
      /// - 5 bytes: MSB storage of the position of the first packet of this keyframe within the file.
      /// - 3 bytes: MSB storage of the duration of this keyframe.
      /// - 2 bytes: MSB storage of the number of this keyframe.
      /// - 2 bytes: MSB storage of the amount of parts in this keyframe.
      /// - 4 bytes: MSB storage of the timestamp associated with this keyframe's first packet.
      char data[16];
  };

  ///\brief Basic class for storage of data associated with fragments.
  class Fragment {
    public:
      long getDuration();
      void setDuration(long newDuration);
      char getLength();
      void setLength(char newLength);
      short getNumber();
      void setNumber(short newNumber);
      long getSize();
      void setSize(long newSize);
      char * getData();
      void toPrettyString(std::stringstream & str, int indent = 0);
    private:
      char data[11];
  };

  class readOnlyTrack {
    public:
      readOnlyTrack();
      readOnlyTrack(JSON::Value & trackRef);
      int getSendLen();
      void send(Socket::Connection & conn);
      void writeTo(char *& p);
      std::string getIdentifier();
      std::string getWritableIdentifier();
      JSON::Value toJSON();
      long long unsigned int fragLen;
      Fragment * fragments;
      long long unsigned int keyLen;
      Key * keys;
      long long unsigned int partLen;
      Part * parts;
      long long unsigned int iVecLen; /*LTS*/
      Ivec * ivecs; /*LTS*/
      int trackID;
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
      void toPrettyString(std::stringstream & str, int indent = 0, int verbosity = 0);
  };

  class Track : public readOnlyTrack {
    public:
      Track();
      Track(const readOnlyTrack & rhs);
      Track(JSON::Value & trackRef);
      inline operator bool() const {
        return parts.size();
      }
      void update(DTSC::Packet & pack);
      void update(JSON::Value & pack);
      int getSendLen();
      void send(Socket::Connection & conn);
      void writeTo(char *& p);
      JSON::Value toJSON();
      std::deque<Fragment> fragments;
      std::deque<Key> keys;
      std::deque<Part> parts;
      std::deque<Ivec> ivecs; /*LTS*/
      Key & getKey(unsigned int keyNum);
      void reset();
      void toPrettyString(std::stringstream & str, int indent = 0, int verbosity = 0);
  };

  class readOnlyMeta {
    public:
      readOnlyMeta();
      readOnlyMeta(JSON::Value & meta);
      inline operator bool() const {
        return vod || live;
      }
      std::map<int, readOnlyTrack> tracks;
      bool vod;
      bool live;
      bool merged;
      long long int moreheader;
      long long int bufferWindow;
      unsigned int getSendLen();
      void send(Socket::Connection & conn);
      void writeTo(char * p);
      JSON::Value toJSON();
      bool isFixed();
      void toPrettyString(std::stringstream & str, int indent = 0, int verbosity = 0);
  };

  class Meta : public readOnlyMeta {
    /// \todo Make toJSON().toNetpacked() shorter
    public:
      Meta();
      Meta(const readOnlyMeta & meta);
      Meta(JSON::Value & meta);
      std::map<int, Track> tracks;
      void update(DTSC::Packet & pack);
      void update(JSON::Value & pack);
      unsigned int getSendLen();
      void send(Socket::Connection & conn);
      void writeTo(char * p);
      JSON::Value toJSON();
      void reset();
      bool isFixed();
      void toPrettyString(std::stringstream & str, int indent = 0, int verbosity = 0);
  };

  /// A simple wrapper class that will open a file and allow easy reading/writing of DTSC data from/to it.
  class File {
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
      DTSC::Packet & getPacket();
      bool seek_time(unsigned int ms);
      bool seek_time(unsigned int ms, int trackNo, bool forceSeek = false);
      bool seek_bpos(int bpos);
      void rewritePacket(std::string & newPacket, int bytePos);
      void writePacket(std::string & newPacket);
      void writePacket(JSON::Value & newPacket);
      bool atKeyframe();
      void selectTracks(std::set<int> & tracks);
    private:
      long int endPos;
      void readHeader(int pos);
      DTSC::Packet myPack; 
      JSON::Value metaStorage;
      readOnlyMeta metadata;
      std::map<int, std::string> trackMapping;
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
  class Stream {
    public:
      Stream();
      virtual ~Stream();
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
      void setRecord(std::string path); /*LTS*/
      unsigned int getTime();
      void dropRing(Ring * ptr);
      int canSeekms(unsigned int ms);
      livePos msSeek(unsigned int ms, std::set<int> & allowedTracks);
      void setBufferTime(unsigned int ms);
      bool isNewest(DTSC::livePos & pos, std::set<int> & allowedTracks);
      DTSC::livePos getNext(DTSC::livePos & pos, std::set<int> & allowedTracks);
      void endStream();
      void waitForMeta(Socket::Connection & sourceSocket);
      void waitForPause(Socket::Connection & sourceSocket);
    protected:
      void cutOneBuffer();
      void resetStream();
      std::map<livePos, JSON::Value> buffers;
      std::map<int, std::set<livePos> > keyframes;
      virtual void addPacket(JSON::Value & newPack);
      virtual void addMeta(JSON::Value & newMeta);
      void recordPacket(JSON::Value & packet); /*LTS*/
      datatype datapointertype;
      unsigned int buffercount;
      unsigned int buffertime;
      std::string recordPath; /*LTS*/
      File * recordFile; /*LTS*/
      bool headerRecorded; /*LTS*/
      std::map<int, std::string> trackMapping;
      virtual void deletionCallback(livePos deleting);
  };
}

