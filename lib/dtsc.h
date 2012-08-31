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
namespace DTSC{

  /// This enum holds all possible datatypes for DTSC packets.
  enum datatype {
    AUDIO, ///< Stream Audio data
    VIDEO, ///< Stream Video data
    META, ///< Stream Metadata
    INVALID ///< Anything else or no data available.
  };

  extern char Magic_Header[]; ///< The magic bytes for a DTSC header
  extern char Magic_Packet[]; ///< The magic bytes for a DTSC packet

  /// A simple wrapper class that will open a file and allow easy reading/writing of DTSC data from/to it.
  class File{
    public:
      File(std::string filename, bool create = false);
      ~File();
      std::string & getHeader();
      bool writeHeader(std::string & header, bool force = false);
      std::string & getPacket();
      bool seek_frame(int frameno);
    private:
      std::string strbuffer;
      std::map<int, long> frames;
      int currframe;
      FILE * F;
      unsigned long headerSize;
      char buffer[4];
  };//FileWriter


  /// A part from the DTSC::Stream ringbuffer.
  /// Holds information about a buffer that will stay consistent
  class Ring {
    public:
      Ring(unsigned int v);
      volatile unsigned int b; ///< Holds current number of buffer. May and is intended to change unexpectedly!
      volatile bool waiting; ///< If true, this Ring is currently waiting for a buffer fill.
      volatile bool starved; ///< If true, this Ring can no longer receive valid data.
  };

  /// Holds temporary data for a DTSC stream and provides functions to utilize it.
  /// Optionally also acts as a ring buffer of a certain requested size.
  /// If ring buffering mode is enabled, it will automatically grow in size to always contain at least one keyframe.
  class Stream {
    public:
      Stream();
      ~Stream();
      Stream(unsigned int buffers);
      JSON::Value metadata;
      JSON::Value & getPacket(unsigned int num = 0);
      datatype lastType();
      std::string & lastData();
      bool hasVideo();
      bool hasAudio();
      bool parsePacket(std::string & buffer);
      std::string & outPacket(unsigned int num);
      std::string & outHeader();
      Ring * getRing();
      unsigned int getTime();
      void dropRing(Ring * ptr);
  private:
      std::deque<JSON::Value> buffers;
      std::set<DTSC::Ring *> rings;
      std::deque<DTSC::Ring> keyframes;
      void advanceRings();
      std::string * datapointer;
      datatype datapointertype;
      unsigned int buffercount;
  };
};
