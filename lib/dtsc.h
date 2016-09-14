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

#define DTSC_INT 0x01
#define DTSC_STR 0x02
#define DTSC_OBJ 0xE0
#define DTSC_ARR 0x0A
#define DTSC_CON 0xFF

//Increase this value every time the DTSH file format changes in an incompatible way
//Changelog:
//  Version 0-2: Undocumented changes
//  Version 3: switched to bigMeta-style by default, Parts layout switched from 3/2/4 to 3/3/3 bytes
#define DTSH_VERSION 3

namespace DTSC {

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
  extern char Magic_Command[]; ///< The magic bytes for a DTCM packet

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

  enum packType {
    DTSC_INVALID,
    DTSC_HEAD,
    DTSC_V1,
    DTSC_V2,
    DTCM
  };

  /// This class allows scanning through raw binary format DTSC data.
  /// It can be used as an iterator or as a direct accessor.
  class Scan {
    public:
      Scan();
      Scan(char * pointer, size_t len);
      operator bool() const;
      std::string toPrettyString(unsigned int indent = 0);
      bool hasMember(std::string indice);
      bool hasMember(const char * indice, const unsigned int ind_len);
      Scan getMember(std::string indice);
      Scan getMember(const char * indice);
      Scan getMember(const char * indice, const unsigned int ind_len);
      Scan getIndice(unsigned int num);
      std::string getIndiceName(unsigned int num);
      unsigned int getSize();

      char getType();
      bool asBool();
      long long asInt();
      std::string asString();
      void getString(char *& result, unsigned int & len);
      JSON::Value asJSON();
    private:
      char * p;
      size_t len;
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
      packType getVersion() const;
      void reInit(Socket::Connection & src);
      void reInit(const char * data_, unsigned int len, bool noCopy = false);
      void genericFill(long long packTime, long long packOffset, long long packTrack, const char * packData, long long packDataSize, long long packBytePos, bool isKeyframe);
      void getString(const char * identifier, char *& result, unsigned int & len) const;
      void getString(const char * identifier, std::string & result) const;
      void getInt(const char * identifier, int & result) const;
      int getInt(const char * identifier) const;
      void getFlag(const char * identifier, bool & result) const;
      bool getFlag(const char * identifier) const;
      bool hasMember(const char * identifier) const;
      long long unsigned int getTime() const;
      long int getTrackId() const;
      char * getData() const;
      int getDataLen() const;
      int getPayloadLen() const;
      JSON::Value toJSON() const;
      std::string toSummary() const;
      Scan getScan() const;
    protected:
      bool master;
      packType version;
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
    long long unsigned int seekTime;
    unsigned int trackID;
  };


  ///\brief Basic class for storage of data associated with single DTSC packets, a.k.a. parts.
  class Part {
    public:
      uint32_t getSize();
      void setSize(uint32_t newSize);
      uint32_t getDuration();
      void setDuration(uint32_t newDuration);
      uint32_t getOffset();
      void setOffset(uint32_t newOffset);
      char * getData();
      void toPrettyString(std::ostream & str, int indent = 0);
    private:
#define PACKED_PART_SIZE 9
      ///\brief Data storage for this Part.
      ///
      /// - 3 bytes: MSB storage of the payload size of this packet in bytes.
      /// - 3 bytes: MSB storage of the duration of this packet in milliseconds.
      /// - 3 bytes: MSB storage of the presentation time offset of this packet in milliseconds.
      char data[PACKED_PART_SIZE];
  };

  ///\brief Basic class for storage of data associated with keyframes.
  ///
  /// When deleting this object, make sure to remove all DTSC::Part associated with it, if any. If you fail doing this, it *will* cause data corruption.
  class Key {
    public:
      unsigned long long getBpos();
      void setBpos(unsigned long long newBpos);
      unsigned long getLength();
      void setLength(unsigned long newLength);
      unsigned long getNumber();
      void setNumber(unsigned long newNumber);
      unsigned short getParts();
      void setParts(unsigned short newParts);
      unsigned long long getTime();
      void setTime(unsigned long long newTime);
      char * getData();
      void toPrettyString(std::ostream & str, int indent = 0);
    private:
#define PACKED_KEY_SIZE 25
      ///\brief Data storage for this Key.
      ///
      /// - 8 bytes: MSB storage of the position of the first packet of this keyframe within the file.
      /// - 3 bytes: MSB storage of the duration of this keyframe.
      /// - 4 bytes: MSB storage of the number of this keyframe.
      /// - 2 bytes: MSB storage of the amount of parts in this keyframe.
      /// - 8 bytes: MSB storage of the timestamp associated with this keyframe's first packet.
      char data[PACKED_KEY_SIZE];
  };

  ///\brief Basic class for storage of data associated with fragments.
  class Fragment {
    public:
      unsigned long getDuration();
      void setDuration(unsigned long newDuration);
      char getLength();
      void setLength(char newLength);
      unsigned long getNumber();
      void setNumber(unsigned long newNumber);
      unsigned long getSize();
      void setSize(unsigned long newSize);
      char * getData();
      void toPrettyString(std::ostream & str, int indent = 0);
    private:
#define PACKED_FRAGMENT_SIZE 13
      ///\brief Data storage for this Fragment.
      ///
      /// - 4 bytes: duration (in milliseconds)
      /// - 1 byte: length (amount of keyframes)
      /// - 4 bytes: number of first keyframe in fragment
      /// - 4 bytes: size of fragment in bytes
      char data[PACKED_FRAGMENT_SIZE];
  };

  ///\brief Class for storage of track data
  class Track {
    public:
      Track();      
      Track(JSON::Value & trackRef);
      Track(Scan & trackRef);
            
      inline operator bool() const {
        return (parts.size() && keySizes.size() && (keySizes.size() == keys.size()));
      }
      void update(long long packTime, long long packOffset, long long packDataSize, long long packBytePos, bool isKeyframe, long long packSendSize, unsigned long segment_size = 5000);
      int getSendLen(bool skipDynamic = false);
      void send(Socket::Connection & conn, bool skipDynamic = false);
      void writeTo(char *& p);
      JSON::Value toJSON(bool skipDynamic = false);
      std::deque<Fragment> fragments;
      std::deque<Key> keys;
      std::deque<unsigned long> keySizes;
      std::deque<Part> parts;
      Key & getKey(unsigned int keyNum);
      unsigned int timeToKeynum(unsigned int timestamp);
      unsigned int timeToFragnum(unsigned int timestamp);
      void reset();
      void toPrettyString(std::ostream & str, int indent = 0, int verbosity = 0);
      void finalize();
      uint32_t biggestFragment();
      
      std::string getIdentifier();
      std::string getWritableIdentifier();
      unsigned int trackID;
      unsigned long long firstms;
      unsigned long long lastms;
      int bps;
      int missedFrags;
      std::string init;
      std::string codec;
      std::string type;
      std::string lang;///< ISO 639-2 Language of track, empty or und if unknown.
      //audio only
      int rate;
      int size;
      int channels;
      //video only
      int width;
      int height;
      int fpks;
    private:
      std::string cachedIdent;
  };

  ///\brief Class for storage of meta data
  class Meta{
      /// \todo Make toJSON().toNetpacked() shorter
    public:
      Meta();
      Meta(const DTSC::Packet & source);
      Meta(JSON::Value & meta);

      inline operator bool() const { //returns if the object contains valid meta data BY LOOKING AT vod/live FLAGS
        return vod || live;
      }
      void reinit(const DTSC::Packet & source);
      void update(DTSC::Packet & pack, unsigned long segment_size = 5000);
      void updatePosOverride(DTSC::Packet & pack, unsigned long bpos);
      void update(JSON::Value & pack, unsigned long segment_size = 5000);
      void update(long long packTime, long long packOffset, long long packTrack, long long packDataSize, long long packBytePos, bool isKeyframe, long long packSendSize = 0, unsigned long segment_size = 5000);
      unsigned int getSendLen(bool skipDynamic = false, std::set<unsigned long> selectedTracks = std::set<unsigned long>());
      void send(Socket::Connection & conn, bool skipDynamic = false, std::set<unsigned long> selectedTracks = std::set<unsigned long>());
      void writeTo(char * p);
      JSON::Value toJSON();
      void reset();
      bool toFile(const std::string & fileName);
      void toPrettyString(std::ostream & str, int indent = 0, int verbosity = 0);
      //members:
      std::map<unsigned int, Track> tracks;
      bool vod;
      bool live;
      bool merged;
      uint16_t version;
      long long int moreheader;
      long long int bufferWindow;
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
      Meta & getMeta();
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
      bool seek_time(unsigned int ms, unsigned int trackNo, bool forceSeek = false);
      bool seek_bpos(int bpos);
      void rewritePacket(std::string & newPacket, int bytePos);
      void writePacket(std::string & newPacket);
      void writePacket(JSON::Value & newPacket);
      bool atKeyframe();
      void selectTracks(std::set<unsigned long> & tracks);
    private:
      long int endPos;
      void readHeader(int pos);
      DTSC::Packet myPack;
      Meta metadata;
      std::map<unsigned int, std::string> trackMapping;
      long long int currtime;
      long long int lastreadpos;
      int currframe;
      FILE * F;
      unsigned long headerSize;
      void * buffer;
      bool created;
      std::set<seekPos> currentPositions;
      std::set<unsigned long> selectedTracks;
  };
  //FileWriter

}

