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

/// Holds all DDVTECH Stream Container classes and parsers.
///length (int, length in seconds, if available)
///video:
/// - codec (string: H264, H263, VP6)
/// - width (int, pixels)
/// - height (int, pixels)
/// - fpks (int, frames per kilosecond (FPS * 1000))
/// - bps (int, bytes per second)
/// - init (string, init data)
/// - keycount (int, count of keyframes)
/// - keyms (int, average ms per keyframe)
/// - keyvar (int, max ms per keyframe variance)
/// - keys (array of byte position ints - first is first keyframe, last is last keyframe, in between have ~equal spacing)
///
///audio:
/// - codec (string: AAC, MP3)
/// - rate (int, Hz)
/// - size (int, bitsize)
/// - bps (int, bytes per second)
/// - channels (int, channelcount)
/// - init (string, init data)
///
///All packets:
/// - datatype (string: audio, video, meta (unused))
/// - data (string: data)
/// - time (int: ms into video)
///
///Video packets:
/// - keyframe (int, if set, is a seekable keyframe)
/// - interframe (int, if set, is a non-seekable interframe)
/// - disposableframe (int, if set, is a disposable interframe)
///
///H264 video packets:
/// - nalu (int, if set, is a nalu)
/// - nalu_end (int, if set, is a end-of-sequence)
/// - offset (int, unsigned version of signed int! Holds the ms offset between timestamp and proper display time for B-frames)
namespace DTSC {

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
          if (seekPos < rhs.seekPos){
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
    long long unsigned int seekPos;
    unsigned int trackID;
  };

  /// A simple wrapper class that will open a file and allow easy reading/writing of DTSC data from/to it.
  class File{
    public:
      File();
      File(const File & rhs);
      File(std::string filename, bool create = false);
      File & operator = (const File & rhs);
      ~File();
      JSON::Value & getMeta();
      JSON::Value & getFirstMeta();
      long long int getLastReadPos();
      bool writeHeader(std::string & header, bool force = false);
      long long int addHeader(std::string & header);
      long int getBytePosEOF();
      long int getBytePos();
      bool reachedEOF();
      void seekNext();
      std::string & getPacket();
      JSON::Value & getJSON();
      JSON::Value & getTrackById(int trackNo);
      bool seek_time(int seconds);
      bool seek_time(int seconds, int trackNo);
      bool seek_bpos(int bpos);
      void writePacket(std::string & newPacket);
      void writePacket(JSON::Value & newPacket);
      bool atKeyframe();
      void selectTracks(std::set<int> & tracks);
    private:
      void readHeader(int pos);
      std::string strbuffer;
      JSON::Value jsonbuffer;
      JSON::Value metadata;
      JSON::Value firstmetadata;
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

  /// A part from the DTSC::Stream ringbuffer.
  /// Holds information about a buffer that will stay consistent
  class Ring{
    public:
      Ring(unsigned int v);
      volatile unsigned int b; ///< Holds current number of buffer. May and is intended to change unexpectedly!
      volatile bool waiting; ///< If true, this Ring is currently waiting for a buffer fill.
      volatile bool starved; ///< If true, this Ring can no longer receive valid data.
      volatile bool updated; ///< If true, this Ring should write a new header.
      volatile int playCount;
  };

  /// Holds temporary data for a DTSC stream and provides functions to utilize it.
  /// Optionally also acts as a ring buffer of a certain requested size.
  /// If ring buffering mode is enabled, it will automatically grow in size to always contain at least one keyframe.
  class Stream{
    public:
      Stream();
      ~Stream();
      Stream(unsigned int buffers, unsigned int bufferTime = 0);
      JSON::Value metadata;
      JSON::Value & getPacket(unsigned int num = 0);
      JSON::Value & getTrackById(int trackNo);
      datatype lastType();
      std::string & lastData();
      bool hasVideo();
      bool hasAudio();
      bool parsePacket(std::string & buffer);
      bool parsePacket(Socket::Buffer & buffer);
      std::string & outPacket(unsigned int num);
      std::string & outHeader();
      Ring * getRing();
      unsigned int getTime();
      void dropRing(Ring * ptr);
      void updateHeaders();
      int canSeekms(unsigned int ms);
      int canSeekFrame(unsigned int frameno);
      unsigned int msSeek(unsigned int ms);
      unsigned int frameSeek(unsigned int frameno);
      void setBufferTime(unsigned int ms);
    private:
      std::deque<JSON::Value> buffers;
      std::set<DTSC::Ring *> rings;
      std::deque<DTSC::Ring> keyframes;
      void advanceRings();
      void updateRingHeaders();
      std::string * datapointer;
      datatype datapointertype;
      unsigned int buffercount;
      unsigned int buffertime;
      std::map<int,std::string> trackMapping;
  };
}
