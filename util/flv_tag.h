/// \file flv_tag.h
/// Holds all headers for the FLV namespace.

#pragma once
#include "ddv_socket.h"
#include <string>

/// This namespace holds all FLV-parsing related functionality.
namespace FLV {
  //variables
  extern char Header[13]; ///< Holds the last FLV header parsed.
  extern bool Parse_Error; ///< This variable is set to true if a problem is encountered while parsing the FLV.
  extern std::string Error_Str; ///< This variable is set if a problem is encountered while parsing the FLV.
  
  //functions
  bool check_header(char * header); ///< Checks a FLV Header for validness.
  bool is_header(char * header); ///< Checks the first 3 bytes for the string "FLV".

  /// This class is used to hold, work with and get information about a single FLV tag.
  class Tag {
    public:
      int len; ///< Actual length of tag.
      bool isKeyframe; ///< True if current tag is a video keyframe.
      char * data; ///< Pointer to tag buffer.
      std::string tagType(); ///< Returns a std::string describing the tag in detail.
      unsigned int tagTime(); ///< Returns the 32-bit timestamp of this tag.
      void tagTime(unsigned int T); ///< Sets the 32-bit timestamp of this tag.
      Tag(); ///< Constructor for a new, empty, tag.
      Tag(const Tag& O); ///< Copy constructor, copies the contents of an existing tag.
      Tag & operator= (const Tag& O); ///< Assignment operator - works exactly like the copy constructor.
      //loader functions
      bool MemLoader(char * D, unsigned int S, unsigned int & P);
      bool SockLoader(int sock);
      bool SockLoader(DDV::Socket sock);
      bool FileLoader(FILE * f);
    protected:
      int buf; ///< Maximum length of buffer space.
      //loader helper functions
      bool MemReadUntil(char * buffer, unsigned int count, unsigned int & sofar, char * D, unsigned int S, unsigned int & P);
      bool SockReadUntil(char * buffer, unsigned int count, unsigned int & sofar, DDV::Socket & sock);
      bool FileReadUntil(char * buffer, unsigned int count, unsigned int & sofar, FILE * f);
  };//Tag

};//FLV namespace
