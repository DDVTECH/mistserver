/// \file dtsc.h
/// Holds all headers for DDVTECH Stream Container parsing/generation.

#pragma once
#include <vector>
#include <iostream>
#include <stdint.h> //for uint64_t
#include <string>
#include <deque>
#include <set>

// video
//  codec (string)

// audio
//  codec (string)
//  sampingrate (int, Hz)
//  samplesize (int, bytesize)
//  channels (int, channelcount)

/// Holds all DDVTECH Stream Container classes and parsers.
namespace DTSC{

  /// Enumerates all possible DTMI types.
  enum DTMItype {
    DTMI_INT = 0x01, ///< Unsigned 64-bit integer.
    DTMI_STRING = 0x02, ///< String, equivalent to the AMF longstring type.
    DTMI_OBJECT = 0xE0, ///< Object, equivalent to the AMF object type.
    DTMI_OBJ_END = 0xEE, ///< End of object marker.
    DTMI_ROOT = 0xFF ///< Root node for all DTMI data.
  };
  
  /// Recursive class that holds DDVTECH MediaInfo.
  class DTMI {
  public:
    std::string Indice();
    DTMItype GetType();
    uint64_t NumValue();
    std::string StrValue();
    const char * Str();
    int hasContent();
    void addContent(DTMI c);
    DTMI* getContentP(int i);
    DTMI getContent(int i);
    DTMI* getContentP(std::string s);
    DTMI getContent(std::string s);
    DTMI();
    DTMI(std::string indice, double val, DTMItype setType = DTMI_INT);
    DTMI(std::string indice, std::string val, DTMItype setType = DTMI_STRING);
    DTMI(std::string indice, DTMItype setType = DTMI_OBJECT);
    void Print(std::string indent = "");
    std::string Pack();
    std::string packed;
  protected:
    std::string myIndice; ///< Holds this objects indice, if any.
    DTMItype myType; ///< Holds this objects AMF0 type.
    std::string strval; ///< Holds this objects string value, if any.
    uint64_t numval; ///< Holds this objects numeric value, if any.
    std::vector<DTMI> contents; ///< Holds this objects contents, if any (for container types).
  };//AMFType
  
  /// Parses a C-string to a valid DTSC::DTMI.
  DTMI parseDTMI(const unsigned char * data, unsigned int len);
  /// Parses a std::string to a valid DTSC::DTMI.
  DTMI parseDTMI(std::string data);
  /// Parses a single DTMI type - used recursively by the DTSC::parseDTMI() functions.
  DTMI parseOneDTMI(const unsigned char *& data, unsigned int &len, unsigned int &i, std::string name);
  
  /// This enum holds all possible datatypes for DTSC packets.
  enum datatype {
    AUDIO, ///< Stream Audio data
    VIDEO, ///< Stream Video data
    META, ///< Stream Metadata
    INVALID ///< Anything else or no data available.
  };

  extern char Magic_Header[]; ///< The magic bytes for a DTSC header
  extern char Magic_Packet[]; ///< The magic bytes for a DTSC packet

  /// A part from the DTSC::Stream ringbuffer.
  /// Holds information about a buffer that will stay consistent
  class Ring {
    public:
      Ring(unsigned int v);
      unsigned int b; ///< Holds current number of buffer. May and is intended to change unexpectedly!
      bool waiting; ///< If true, this Ring is currently waiting for a buffer fill.
      bool starved; ///< If true, this Ring can no longer receive valid data.
  };

  /// Holds temporary data for a DTSC stream and provides functions to utilize it.
  /// Optionally also acts as a ring buffer of a certain requested size.
  /// If ring buffering mode is enabled, it will automatically grow in size to always contain at least one keyframe.
  class Stream {
    public:
      Stream();
      ~Stream();
      Stream(unsigned int buffers);
      DTSC::DTMI metadata;
      DTSC::DTMI & getPacket(unsigned int num = 0);
      datatype lastType();
      const char * lastData();
      bool hasVideo();
      bool hasAudio();
      bool parsePacket(std::string & buffer);
      std::string outPacket(unsigned int num);
      std::string outHeader();
      Ring * getRing();
      void dropRing(Ring * ptr);
  private:
      std::deque<DTSC::DTMI> buffers;
      std::set<DTSC::Ring *> rings;
      std::deque<DTSC::Ring> keyframes;
      void advanceRings();
      const char * datapointer;
      datatype datapointertype;
      unsigned int buffercount;
  };
};
