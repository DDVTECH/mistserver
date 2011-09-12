/// \file dtsc.h
/// Holds all headers for DDVTECH Stream Container parsing/generation.

#pragma once
#include "dtmi.h"

// Video:
//  Codec (string)

// Audio:
//  Codec (string)
//  Samping rate (int, Hz)
//  Sample Size (int, bytesize)
//  Channels (int, channelcount)

namespace DTSC{

  /// This enum holds all possible datatypes for DTSC packets.
  enum datatype {
    AUDIO, ///< Stream Audio data
    VIDEO, ///< Stream Video data
    META, ///< Stream Metadata
    INVALID ///< Anything else or no data available.
  }

  char * Magic_Header; ///< The magic bytes for a DTSC header
  char * Magic_Packet; ///< The magic bytes for a DTSC packet

  /// Holds temporary data for a DTSC stream and provides functions to access/store it.
  class Stream {
    public:
      Stream();
      DTSC::DTMI metadata;
      DRSC::DTMI lastPacket;
      datatype lastType();
      char * lastData();
      bool hasVideo();
      bool hasAudio();
      bool parsePacket(std::string & buffer);
    private:
      char * datapointer;
      datatype datapointertype;
  }

  

};
