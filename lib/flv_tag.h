/// \file flv_tag.h
/// Holds all headers for the FLV namespace.

#pragma once
#include "socket.h"
#include "dtsc.h"
#include "json.h"
#include <string>


//forward declaration of RTMPStream::Chunk to avoid circular dependencies.
namespace RTMPStream {
  class Chunk;
}

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
  class Tag{
    public:
      int len; ///< Actual length of tag.
      bool isKeyframe; ///< True if current tag is a video keyframe.
      char * data; ///< Pointer to tag buffer.
      bool needsInitData(); ///< True if this media type requires init data.
      bool isInitData(); ///< True if current tag is init data for this media type.
      const char * getAudioCodec(); ///< Returns a c-string with the audio codec name.
      const char * getVideoCodec(); ///< Returns a c-string with the video codec name.
      std::string tagType(); ///< Returns a std::string describing the tag in detail.
      unsigned int tagTime(); ///< Returns the 32-bit timestamp of this tag.
      void tagTime(unsigned int T); ///< Sets the 32-bit timestamp of this tag.
      Tag(); ///< Constructor for a new, empty, tag.
      Tag(const Tag& O); ///< Copy constructor, copies the contents of an existing tag.
      Tag & operator=(const Tag& O); ///< Assignment operator - works exactly like the copy constructor.
      Tag(const RTMPStream::Chunk& O); ///<Copy constructor from a RTMP chunk.
      ~Tag(); ///< Generic destructor.
      //loader functions
      bool ChunkLoader(const RTMPStream::Chunk& O);
      bool DTSCLoader(DTSC::Stream & S);
      bool DTSCVideoInit(DTSC::Stream & S);
      bool DTSCVideoInit(JSON::Value & video);
      bool DTSCAudioInit(DTSC::Stream & S);
      bool DTSCAudioInit(JSON::Value & audio);
      bool DTSCMetaInit(DTSC::Stream & S, std::string vidName = "", std::string audName = "");
      JSON::Value toJSON(JSON::Value & metadata);
      bool MemLoader(char * D, unsigned int S, unsigned int & P);
      bool FileLoader(FILE * f);
    protected:
      int buf; ///< Maximum length of buffer space.
      bool done; ///< Body reading done?
      unsigned int sofar; ///< How many bytes are read sofar?
      void setLen();
      bool checkBufferSize();
      //loader helper functions
      bool MemReadUntil(char * buffer, unsigned int count, unsigned int & sofar, char * D, unsigned int S, unsigned int & P);
      bool FileReadUntil(char * buffer, unsigned int count, unsigned int & sofar, FILE * f);
      //JSON writer helpers
      void Meta_Put(JSON::Value & meta, std::string cat, std::string elem, std::string val);
      void Meta_Put(JSON::Value & meta, std::string cat, std::string elem, uint64_t val);
      bool Meta_Has(JSON::Value & meta, std::string cat, std::string elem);
  };
//Tag

}//FLV namespace
