/// \file flv_tag.h
/// Holds all headers for the FLV namespace.

#pragma once
#include "amf.h"
#include "dtsc.h"
#include "socket.h"
#include <string>

// forward declaration of RTMPStream::Chunk to avoid circular dependencies.
namespace RTMPStream{
  class Chunk;
}

/// This namespace holds all FLV-parsing related functionality.
namespace FLV{
  // variables
  extern char Header[13];  ///< Holds the last FLV header parsed.
  extern bool Parse_Error; ///< This variable is set to true if a problem is encountered while
                           ///< parsing the FLV.
  extern std::string Error_Str; ///< This variable is set if a problem is encountered while parsing the FLV.

  // functions
  bool check_header(char *header); ///< Checks a FLV Header for validness.
  bool is_header(const char *header);    ///< Checks the first 3 bytes for the string "FLV".

  /// Helper function that can quickly skip through a file looking for a particular tag type
  bool seekToTagType(FILE *f, uint8_t type);

  /// This class is used to hold, work with and get information about a single FLV tag.
  class Tag{
  public:
    int len;                     ///< Actual length of tag.
    bool isKeyframe;             ///< True if current tag is a video keyframe.
    char *data;                  ///< Pointer to tag buffer.
    bool needsInitData();        ///< True if this media type requires init data.
    bool isInitData();           ///< True if current tag is init data for this media type.
    const char *getAudioCodec(); ///< Returns a c-string with the audio codec name.
    const char *getVideoCodec(); ///< Returns a c-string with the video codec name.
    std::string tagType();       ///< Returns a std::string describing the tag in detail.
    uint64_t tagTime();
    void tagTime(uint64_t T);
    int64_t offset();
    void offset(int64_t o);
    Tag();                        ///< Constructor for a new, empty, tag.
    Tag(const Tag &O);            ///< Copy constructor, copies the contents of an existing tag.
    Tag &operator=(const Tag &O); ///< Assignment operator - works exactly like the copy constructor.
    Tag(const RTMPStream::Chunk &O); ///< Copy constructor from a RTMP chunk.
    ~Tag();                          ///< Generic destructor.
    // loader functions
    bool ChunkLoader(const RTMPStream::Chunk &O);
    bool DTSCLoader(DTSC::Packet &packData, const DTSC::Meta &M, size_t idx);
    bool DTSCVideoInit(DTSC::Meta &meta, uint32_t vTrack);
    bool DTSCAudioInit(const std::string & codec, unsigned int sampleRate, unsigned int sampleSize, unsigned int channels, const std::string & initData);
    bool DTSCMetaInit(const DTSC::Meta &M, std::set<size_t> &selTracks);
    void toMeta(DTSC::Meta &meta, AMF::Object &amf_storage);
    void toMeta(DTSC::Meta &meta, AMF::Object &amf_storage, size_t &reTrack, const std::map<std::string, std::string> &targetParams);
    bool MemLoader(const char *D, unsigned int S, unsigned int &P);
    bool FileLoader(FILE *f);
    unsigned int getTrackID();
    char *getData();
    unsigned int getDataLen();

  protected:
    int buf;            ///< Maximum length of buffer space.
    bool done;          ///< Body reading done?
    unsigned int sofar; ///< How many bytes are read sofar?
    void setLen();
    bool checkBufferSize();
    // loader helper functions
    bool MemReadUntil(char *buffer, unsigned int count, unsigned int &sofar, const char *D,
                      unsigned int S, unsigned int &P);
    bool FileReadUntil(char *buffer, unsigned int count, unsigned int &sofar, FILE *f);
  };
  // Tag

}// namespace FLV
