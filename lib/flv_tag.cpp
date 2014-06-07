/// \file flv_tag.cpp
/// Holds all code for the FLV namespace.

#include "amf.h"
#include "rtmpchunks.h"
#include "flv_tag.h"
#include "timing.h"
#include <stdio.h> //for Tag::FileLoader
#include <unistd.h> //for Tag::FileLoader
#include <fcntl.h> //for Tag::FileLoader
#include <stdlib.h> //malloc
#include <string.h> //memcpy
#include <sstream>

/// Holds the last FLV header parsed.
/// Defaults to a audio+video header on FLV version 0x01 if no header received yet.
char FLV::Header[13] = {'F', 'L', 'V', 0x01, 0x05, 0, 0, 0, 0x09, 0, 0, 0, 0};

bool FLV::Parse_Error = false; ///< This variable is set to true if a problem is encountered while parsing the FLV.
std::string FLV::Error_Str = "";

/// Checks a FLV Header for validness. Returns true if the header is valid, false
/// if the header is not. Not valid can mean:
/// - Not starting with the string "FLV".
/// - The DataOffset is not 9 bytes.
/// - The PreviousTagSize is not 0 bytes.
///
/// Note that we see PreviousTagSize as part of the FLV header, not part of the tag header!
bool FLV::check_header(char * header){
  if (header[0] != 'F') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'V') return false;
  if (header[5] != 0) return false;
  if (header[6] != 0) return false;
  if (header[7] != 0) return false;
  if (header[8] != 0x09) return false;
  if (header[9] != 0) return false;
  if (header[10] != 0) return false;
  if (header[11] != 0) return false;
  if (header[12] != 0) return false;
  return true;
} //FLV::check_header

/// Checks the first 3 bytes for the string "FLV". Implementing a basic FLV header check,
/// returning true if it is, false if not.
bool FLV::is_header(char * header){
  if (header[0] != 'F') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'V') return false;
  return true;
} //FLV::is_header

/// True if this media type requires init data.
/// Will always return false if the tag type is not 0x08 or 0x09.
/// Returns true for H263, AVC (H264), AAC.
/// \todo Check if MP3 does or does not require init data...
bool FLV::Tag::needsInitData(){
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0x0F){
        case 2:
          return true;
          break; //H263 requires init data
        case 7:
          return true;
          break; //AVC requires init data
        default:
          return false;
          break; //other formats do not
      }
      break;
    case 0x08:
      switch (data[11] & 0xF0){
        case 0x20:
          return false;
          break; //MP3 does not...? Unsure.
        case 0xA0:
          return true;
          break; //AAC requires init data
        case 0xE0:
          return false;
          break; //MP38kHz does not...?
        default:
          return false;
          break; //other formats do not
      }
      break;
  }
  return false; //only audio/video can require init data
}

/// True if current tag is init data for this media type.
bool FLV::Tag::isInitData(){
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0xF0){
        case 0x50:
          return true;
          break;
      }
      if ((data[11] & 0x0F) == 7){
        switch (data[12]){
          case 0:
            return true;
            break;
        }
      }
      break;
    case 0x08:
      if ((data[12] == 0) && ((data[11] & 0xF0) == 0xA0)){
        return true;
      }
      break;
  }
  return false;
}

const char * FLV::Tag::getVideoCodec(){
  switch (data[11] & 0x0F){
    case 1:
      return "JPEG";
    case 2:
      return "H263";
    case 3:
      return "ScreenVideo1";
    case 4:
      return "VP6";
    case 5:
      return "VP6Alpha";
    case 6:
      return "ScreenVideo2";
    case 7:
      return "H264";
    default:
      return "unknown";
  }
}

const char * FLV::Tag::getAudioCodec(){
  switch (data[11] & 0xF0){
    case 0x00:
      return "linear PCM PE";
    case 0x10:
      return "ADPCM";
    case 0x20:
      return "MP3";
    case 0x30:
      return "linear PCM LE";
    case 0x40:
      return "Nelly16kHz";
    case 0x50:
      return "Nelly8kHz";
    case 0x60:
      return "Nelly";
    case 0x70:
      return "G711A-law";
    case 0x80:
      return "G711mu-law";
    case 0x90:
      return "reserved";
    case 0xA0:
      return "AAC";
    case 0xB0:
      return "Speex";
    case 0xE0:
      return "MP38kHz";
    case 0xF0:
      return "DeviceSpecific";
    default:
      return "unknown";
  }
}

/// Returns a std::string describing the tag in detail.
/// The string includes information about whether the tag is
/// audio, video or metadata, what encoding is used, and the details
/// of the encoding itself.
std::string FLV::Tag::tagType(){
  std::stringstream R;
  R << len << " bytes of ";
  switch (data[0]){
    case 0x09:
      R << getVideoCodec() << " video ";
      switch (data[11] & 0xF0){
        case 0x10:
          R << "keyframe";
          break;
        case 0x20:
          R << "iframe";
          break;
        case 0x30:
          R << "disposableiframe";
          break;
        case 0x40:
          R << "generatedkeyframe";
          break;
        case 0x50:
          R << "videoinfo";
          break;
      }
      if ((data[11] & 0x0F) == 7){
        switch (data[12]){
          case 0:
            R << " header";
            break;
          case 1:
            R << " NALU";
            break;
          case 2:
            R << " endofsequence";
            break;
        }
      }
      break;
    case 0x08:
      R << getAudioCodec();
      switch (data[11] & 0x0C){
        case 0x0:
          R << " 5.5kHz";
          break;
        case 0x4:
          R << " 11kHz";
          break;
        case 0x8:
          R << " 22kHz";
          break;
        case 0xC:
          R << " 44kHz";
          break;
      }
      switch (data[11] & 0x02){
        case 0:
          R << " 8bit";
          break;
        case 2:
          R << " 16bit";
          break;
      }
      switch (data[11] & 0x01){
        case 0:
          R << " mono";
          break;
        case 1:
          R << " stereo";
          break;
      }
      R << " audio";
      if ((data[12] == 0) && ((data[11] & 0xF0) == 0xA0)){
        R << " initdata";
      }
      break;
    case 0x12: {
      R << "(meta)data: ";
      AMF::Object metadata = AMF::parse((unsigned char*)data + 11, len - 15);
      R << metadata.Print();
      break;
    }
    default:
      R << "unknown";
      break;
  }
  return R.str();
} //FLV::Tag::tagtype

/// Returns the 24-bit offset of this tag.
/// Returns 0 if the tag isn't H264
int FLV::Tag::offset(){
  if ((data[11] & 0x0F) == 7){
    return (((data[13] << 16) + (data[14] << 8) + data[15]) << 8) >> 8;
  }else{
    return 0;
  }
} //offset getter

/// Sets the 24-bit offset of this tag.
/// Ignored if the tag isn't H264
void FLV::Tag::offset(int o){
  data[13] = (o >> 16) & 0xFF;
  data[14] = (o >> 8) & 0XFF;
  data[15] = o & 0xFF;
} //offset setter

/// Returns the 32-bit timestamp of this tag.
unsigned int FLV::Tag::tagTime(){
  return (data[4] << 16) + (data[5] << 8) + data[6] + (data[7] << 24);
} //tagTime getter

/// Sets the 32-bit timestamp of this tag.
void FLV::Tag::tagTime(unsigned int T){
  data[4] = ((T >> 16) & 0xFF);
  data[5] = ((T >> 8) & 0xFF);
  data[6] = (T & 0xFF);
  data[7] = ((T >> 24) & 0xFF);
} //tagTime setter

/// Constructor for a new, empty, tag.
/// The buffer length is initialized to 0, and later automatically
/// increased if neccesary.
FLV::Tag::Tag(){
  len = 0;
  buf = 0;
  data = 0;
  isKeyframe = false;
  done = true;
  sofar = 0;
} //empty constructor

/// Copy constructor, copies the contents of an existing tag.
/// The buffer length is initialized to the actual size of the tag
/// that is being copied, and later automaticallt increased if
/// neccesary.
FLV::Tag::Tag(const Tag& O){
  done = true;
  sofar = 0;
  len = O.len;
  data = 0;
  if (len > 0){
    if (checkBufferSize()){
      memcpy(data, O.data, len);
    }
  }
  isKeyframe = O.isKeyframe;
} //copy constructor

/// Copy constructor from a RTMP chunk.
/// Copies the contents of a RTMP chunk into a valid FLV tag.
/// Exactly the same as making a chunk by through the default (empty) constructor
/// and then calling FLV::Tag::ChunkLoader with the chunk as argument.
FLV::Tag::Tag(const RTMPStream::Chunk& O){
  len = 0;
  buf = 0;
  data = 0;
  isKeyframe = false;
  done = true;
  sofar = 0;
  ChunkLoader(O);
}

/// Generic destructor that frees the allocated memory in the internal data variable, if any.
FLV::Tag::~Tag(){
  if (data){
    free(data);
    data = 0;
    buf = 0;
    len = 0;
  }
}

/// Assignment operator - works exactly like the copy constructor.
/// This operator checks for self-assignment.
FLV::Tag & FLV::Tag::operator=(const FLV::Tag& O){
  if (this != &O){ //no self-assignment
    len = O.len;
    if (len > 0){
      if (checkBufferSize()){
        memcpy(data, O.data, len);
      }else{
        len = buf;
      }
    }
    isKeyframe = O.isKeyframe;
  }
  return *this;
} //assignment operator

/// FLV loader function from DTSC.
/// Takes the DTSC data and makes it into FLV.
bool FLV::Tag::DTSCLoader(DTSC::Stream & S){
  std::string tmp = S.getPacket().toNetPacked();
  DTSC::Packet tmpPack(tmp.data(), tmp.size());
  return DTSCLoader(tmpPack, S.metadata.tracks[S.getPacket()["trackid"].asInt()]);
}

bool FLV::Tag::DTSCLoader(DTSC::Packet & packData, DTSC::Track & track){
  std::string meta_str;
  len = 0;
  if (track.type == "video"){
    char * tmpData = 0;
    unsigned int tmpLen = 0;
    packData.getString("data", tmpData, tmpLen);
    len = tmpLen + 16;
    if (track.codec == "H264"){
      len += 4;
    }
    if ( !checkBufferSize()){
      return false;
    }
    if (track.codec == "H264"){
      memcpy(data + 16, tmpData, len - 20);
      data[12] = 1;
      if (packData.getInt("offset") < 0){
        offset(0);
      }else{
        offset(packData.getInt("offset"));
      }
    }else{
      memcpy(data + 12, tmpData, len - 16);
    }
    data[11] = 0;
    if (track.codec == "H264"){
      data[11] |= 7;
    }
    if (track.codec == "H263"){
      data[11] |= 2;
    }
    if (packData.getFlag("keyframe")){
      data[11] |= 0x10;
    }else{
      data[11] |= 0x20;
    }
    if (packData.getFlag("disposableframe")){
      data[11] |= 0x30;
    }
  }
  if (track.type == "audio"){
    char * tmpData = 0;
    unsigned int tmpLen = 0;
    packData.getString("data", tmpData, tmpLen);
    len = tmpLen + 16;
    if (track.codec == "AAC"){
      len ++;
    }
    if ( !checkBufferSize()){
      return false;
    }
    if (track.codec == "AAC"){
      memcpy(data + 13, tmpData, len - 17);
      data[12] = 1; //raw AAC data, not sequence header
    }else{
      memcpy(data + 12, tmpData, len - 16);
    }
    data[11] = 0;
    if (track.codec == "AAC"){
      data[11] |= 0xA0;
    }
    if (track.codec == "MP3"){
      data[11] |= 0x20;
    }
    unsigned int datarate = track.rate;
    if (datarate >= 44100){
      data[11] |= 0x0C;
    }else if (datarate >= 22050){
      data[11] |= 0x08;
    }else if (datarate >= 11025){
      data[11] |= 0x04;
    }
    if (track.size == 16){
      data[11] |= 0x02;
    }
    if (track.channels > 1){
      data[11] |= 0x01;
    }
  }
  if (!len){
    return false;
  }
  setLen();
  if (track.type == "video"){data[0] = 0x09;}
  if (track.type == "audio"){data[0] = 0x08;}
  if (track.type == "meta"){data[0] = 0x12;}
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(packData.getTime());
  if (packData.getInt("offset")){
    tagTime(packData.getTime() + packData.getInt("offset"));
  }
  return true;
}

/// Helper function that properly sets the tag length from the internal len variable.
void FLV::Tag::setLen(){
  int len4 = len - 4;
  int i = len;
  data[ --i] = (len4) & 0xFF;
  len4 >>= 8;
  data[ --i] = (len4) & 0xFF;
  len4 >>= 8;
  data[ --i] = (len4) & 0xFF;
  len4 >>= 8;
  data[ --i] = (len4) & 0xFF;
}

/// FLV Video init data loader function from JSON.
bool FLV::Tag::DTSCVideoInit(DTSC::Track & video){
  //Unknown? Assume H264.
  if (video.codec == "?"){
    video.codec = "H264";
  }
  if (video.codec == "H264"){
    len = video.init.size() + 20;
  }
  if (len <= 0 || !checkBufferSize()){
    return false;
  }
  memcpy(data + 16, video.init.c_str(), len - 20);
  data[12] = 0; //H264 sequence header
  data[13] = 0;
  data[14] = 0;
  data[15] = 0;
  data[11] = 0x17; //H264 keyframe (0x07 & 0x10)
  setLen();
  data[0] = 0x09;
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(0);
  return true;
}

/// FLV Audio init data loader function from JSON.
bool FLV::Tag::DTSCAudioInit(DTSC::Track & audio){
  len = 0;
  //Unknown? Assume AAC.
  if (audio.codec == "?"){
    audio.codec = "AAC";
  }
  if (audio.codec == "AAC"){
    len = audio.init.size() + 17;
  }
  if (len <= 0 || !checkBufferSize()){
    return false;
  }
  memcpy(data + 13, audio.init.c_str(), len - 17);
  data[12] = 0; //AAC sequence header
  data[11] = 0;
  if (audio.codec == "AAC"){
    data[11] += 0xA0;
  }
  if (audio.codec == "MP3"){
    data[11] += 0x20;
  }
  unsigned int datarate = audio.rate;
  if (datarate >= 44100){
    data[11] += 0x0C;
  }else if (datarate >= 22050){
    data[11] += 0x08;
  }else if (datarate >= 11025){
    data[11] += 0x04;
  }
  if (audio.size == 16){
    data[11] += 0x02;
  }
  if (audio.channels > 1){
    data[11] += 0x01;
  }
  setLen();
  data[0] = 0x08;
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(0);
  return true;
}

bool FLV::Tag::DTSCMetaInit(DTSC::Meta & M, std::set<long unsigned int> & selTracks){
  AMF::Object amfdata("root", AMF::AMF0_DDV_CONTAINER);
  amfdata.addContent(AMF::Object("", "onMetaData"));
  amfdata.addContent(AMF::Object("", AMF::AMF0_ECMA_ARRAY));
  AMF::Object trinfo = AMF::Object("trackinfo", AMF::AMF0_STRICT_ARRAY);
  int i = 0;
  unsigned long long mediaLen = 0;
  for (std::set<long unsigned int>::iterator it = selTracks.begin(); it != selTracks.end(); it++){
    if (M.tracks[*it].lastms - M.tracks[*it].firstms > mediaLen){
      mediaLen = M.tracks[*it].lastms - M.tracks[*it].firstms;
    }
    if (M.tracks[*it].type == "video"){
      trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
      trinfo.getContentP(i)->addContent(AMF::Object("length", ((double)M.tracks[*it].lastms / 1000) * ((double)M.tracks[*it].fpks / 1000.0), AMF::AMF0_NUMBER));
      trinfo.getContentP(i)->addContent(AMF::Object("timescale", ((double)M.tracks[*it].fpks / 1000.0), AMF::AMF0_NUMBER));
      trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
      amfdata.getContentP(1)->addContent(AMF::Object("hasVideo", 1, AMF::AMF0_BOOL));
      if (M.tracks[*it].codec == "H264"){
        amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", (std::string)"avc1"));
        trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"avc1"));
      }
      if (M.tracks[*it].codec == "VP6"){
        amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 4, AMF::AMF0_NUMBER));
        trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"vp6"));
      }
      if (M.tracks[*it].codec == "H263"){
        amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 2, AMF::AMF0_NUMBER));
        trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"h263"));
      }
      amfdata.getContentP(1)->addContent(AMF::Object("width", M.tracks[*it].width, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("height", M.tracks[*it].height, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("videoframerate", (double)M.tracks[*it].fpks / 1000.0, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("videodatarate", (double)M.tracks[*it].bps * 128.0, AMF::AMF0_NUMBER));
      ++i;
    }
    if (M.tracks[*it].type == "audio"){
      trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
      trinfo.getContentP(i)->addContent(AMF::Object("length", ((double)M.tracks[*it].lastms) * ((double)M.tracks[*it].rate), AMF::AMF0_NUMBER));
      trinfo.getContentP(i)->addContent(AMF::Object("timescale", M.tracks[*it].rate, AMF::AMF0_NUMBER));
      trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
      amfdata.getContentP(1)->addContent(AMF::Object("hasAudio", 1, AMF::AMF0_BOOL));
      amfdata.getContentP(1)->addContent(AMF::Object("audiodelay", 0, AMF::AMF0_NUMBER));
      if (M.tracks[*it].codec == "AAC"){
        amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp4a"));
        trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp4a"));
      }
      if (M.tracks[*it].codec == "MP3"){
        amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp3"));
        trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp3"));
      }
      amfdata.getContentP(1)->addContent(AMF::Object("audiochannels", M.tracks[*it].channels, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplerate", M.tracks[*it].rate, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplesize", M.tracks[*it].size, AMF::AMF0_NUMBER));
      amfdata.getContentP(1)->addContent(AMF::Object("audiodatarate", (double)M.tracks[*it].bps * 128.0, AMF::AMF0_NUMBER));
      ++i;
    }
  }
  if (M.vod){
    amfdata.getContentP(1)->addContent(AMF::Object("duration", mediaLen/1000, AMF::AMF0_NUMBER));
  }
  amfdata.getContentP(1)->addContent(trinfo);
  
  std::string tmp = amfdata.Pack();
  len = tmp.length() + 15;
  if (len <= 0 || !checkBufferSize()){
    return false;
  }
  memcpy(data + 11, tmp.data(), len - 15);
  setLen();
  data[0] = 0x12;
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(0);
  return true;
}

/// FLV metadata loader function from DTSC.
/// Takes the DTSC metadata and makes it into FLV.
/// Assumes metadata is available - so check before calling!
bool FLV::Tag::DTSCMetaInit(DTSC::Stream & S, DTSC::Track & videoRef, DTSC::Track & audioRef){
  //Unknown? Assume AAC.
  if (audioRef.codec == "?"){
    audioRef.codec = "AAC";
  }
  //Unknown? Assume H264.
  if (videoRef.codec == "?"){
    videoRef.codec = "H264";
  }

  AMF::Object amfdata("root", AMF::AMF0_DDV_CONTAINER);

  amfdata.addContent(AMF::Object("", "onMetaData"));
  amfdata.addContent(AMF::Object("", AMF::AMF0_ECMA_ARRAY));
  if (S.metadata.vod){
    amfdata.getContentP(1)->addContent(AMF::Object("duration", videoRef.lastms / 1000, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("moovPosition", 40, AMF::AMF0_NUMBER));
    AMF::Object keys("keyframes", AMF::AMF0_OBJECT);
    keys.addContent(AMF::Object("filepositions", AMF::AMF0_STRICT_ARRAY));
    keys.addContent(AMF::Object("times", AMF::AMF0_STRICT_ARRAY));
    int total_byterate = 0;
    if (videoRef.trackID > 0){
      total_byterate += videoRef.bps;
    }
    if (audioRef.trackID > 0){
      total_byterate += audioRef.bps;
    }
    for (unsigned long long i = 0; i < videoRef.lastms / 1000; ++i){ //for each second in the file
      keys.getContentP(0)->addContent(AMF::Object("", i * total_byterate, AMF::AMF0_NUMBER)); //multiply by byterate for fake byte positions
      keys.getContentP(1)->addContent(AMF::Object("", i, AMF::AMF0_NUMBER)); //seconds
    }
    amfdata.getContentP(1)->addContent(keys);
  }
  if (videoRef.trackID > 0){
    amfdata.getContentP(1)->addContent(AMF::Object("hasVideo", 1, AMF::AMF0_BOOL));
    if (videoRef.codec == "H264"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", (std::string)"avc1"));
    }
    if (videoRef.codec == "VP6"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 4, AMF::AMF0_NUMBER));
    }
    if (videoRef.codec == "H263"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 2, AMF::AMF0_NUMBER));
    }
    amfdata.getContentP(1)->addContent(AMF::Object("width", videoRef.width, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("height", videoRef.height, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("videoframerate", (double)videoRef.fpks / 1000.0, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("videodatarate", (double)videoRef.bps * 128.0, AMF::AMF0_NUMBER));
  }
  if (audioRef.trackID > 0){
    amfdata.getContentP(1)->addContent(AMF::Object("hasAudio", 1, AMF::AMF0_BOOL));
    amfdata.getContentP(1)->addContent(AMF::Object("audiodelay", 0, AMF::AMF0_NUMBER));
    if (audioRef.codec == "AAC"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp4a"));
    }
    if (audioRef.codec == "MP3"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp3"));
    }
    amfdata.getContentP(1)->addContent(AMF::Object("audiochannels", audioRef.channels, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("audiosamplerate", audioRef.rate, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("audiosamplesize", audioRef.size, AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("audiodatarate", (double)audioRef.bps * 128.0, AMF::AMF0_NUMBER));
  }
  AMF::Object trinfo = AMF::Object("trackinfo", AMF::AMF0_STRICT_ARRAY);
  int i = 0;
  if (audioRef){
    trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
    trinfo.getContentP(i)->addContent(AMF::Object("length", ((double)audioRef.lastms) * ((double)audioRef.rate), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("timescale", audioRef.rate, AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    if (audioRef.codec == "AAC"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp4a"));
    }
    if (audioRef.codec == "MP3"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp3"));
    }
    ++i;
  }
  if (videoRef){
    trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
    trinfo.getContentP(i)->addContent(
        AMF::Object("length", ((double)videoRef.lastms / 1000) * ((double)videoRef.fpks / 1000.0), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("timescale", ((double)videoRef.fpks / 1000.0), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    if (videoRef.codec == "H264"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"avc1"));
    }
    if (videoRef.codec == "VP6"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"vp6"));
    }
    if (videoRef.codec == "H263"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"h263"));
    }
    ++i;
  }
  amfdata.getContentP(1)->addContent(trinfo);

  std::string tmp = amfdata.Pack();
  len = tmp.length() + 15;
  if (len <= 0 || !checkBufferSize()){
    return false;
  }
  memcpy(data + 11, tmp.data(), len - 15);
  setLen();
  data[0] = 0x12;
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(0);
  return true;
}

/// FLV loader function from chunk.
/// Copies the contents and wraps it in a FLV header.
bool FLV::Tag::ChunkLoader(const RTMPStream::Chunk& O){
  len = O.len + 15;
  if (len > 0){
    if ( !checkBufferSize()){
      return false;
    }
    memcpy(data + 11, &(O.data[0]), O.len);
  }
  setLen();
  data[0] = O.msg_type_id;
  data[3] = O.len & 0xFF;
  data[2] = (O.len >> 8) & 0xFF;
  data[1] = (O.len >> 16) & 0xFF;
  tagTime(O.timestamp);
  return true;
}

/// Helper function for FLV::MemLoader.
/// This function will try to read count bytes from data buffer D into buffer.
/// This function should be called repeatedly until true.
/// P and sofar are not the same value, because D may not start with the current tag.
/// \param buffer The target buffer.
/// \param count Amount of bytes to read.
/// \param sofar Current amount read.
/// \param D The location of the data buffer.
/// \param S The size of the data buffer.
/// \param P The current position in the data buffer. Will be updated to reflect new position.
/// \return True if count bytes are read succesfully, false otherwise.
bool FLV::Tag::MemReadUntil(char * buffer, unsigned int count, unsigned int & sofar, char * D, unsigned int S, unsigned int & P){
  if (sofar >= count){
    return true;
  }
  int r = 0;
  if (P + (count - sofar) > S){
    r = S - P;
  }else{
    r = count - sofar;
  }
  memcpy(buffer + sofar, D + P, r);
  P += r;
  sofar += r;
  if (sofar >= count){
    return true;
  }
  return false;
} //Tag::MemReadUntil

/// Try to load a tag from a data buffer in memory.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param D The location of the data buffer.
/// \param S The size of the data buffer.
/// \param P The current position in the data buffer. Will be updated to reflect new position.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::MemLoader(char * D, unsigned int S, unsigned int & P){
  if (len < 15){
    len = 15;
  }
  if ( !checkBufferSize()){
    return false;
  }
  if (done){
    //read a header
    if (MemReadUntil(data, 11, sofar, D, S, P)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (MemReadUntil(data, 13, sofar, D, S, P)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{
            FLV::Parse_Error = true;
            Error_Str = "Invalid header received.";
            return false;
          }
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if ( !checkBufferSize()){
          return false;
        }
        if (data[0] > 0x12){
          data[0] += 32;
          FLV::Parse_Error = true;
          Error_Str = "Invalid Tag received (";
          Error_Str += data[0];
          Error_Str += ").";
          return false;
        }
        done = false;
      }
    }
  }else{
    //read tag body
    if (MemReadUntil(data, len, sofar, D, S, P)){
      //calculate keyframeness, next time read header again, return true
      if ((data[0] == 0x09) && (((data[11] & 0xf0) >> 4) == 1)){
        isKeyframe = true;
      }else{
        isKeyframe = false;
      }
      done = true;
      sofar = 0;
      return true;
    }
  }
  return false;
} //Tag::MemLoader

/// Helper function for FLV::FileLoader.
/// This function will try to read count bytes from file f into buffer.
/// This function should be called repeatedly until true.
/// \param buffer The target buffer.
/// \param count Amount of bytes to read.
/// \param sofar Current amount read.
/// \param f File to read from.
/// \return True if count bytes are read succesfully, false otherwise.
bool FLV::Tag::FileReadUntil(char * buffer, unsigned int count, unsigned int & sofar, FILE * f){
  if (sofar >= count){
    return true;
  }
  int r = 0;
  r = fread(buffer + sofar, 1, count - sofar, f);
  if (r < 0){
    FLV::Parse_Error = true;
    Error_Str = "File reading error.";
    return false;
  }
  sofar += r;
  if (sofar >= count){
    return true;
  }
  return false;
}

/// Try to load a tag from a file.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param f The file to read from.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::FileLoader(FILE * f){
  int preflags = fcntl(fileno(f), F_GETFL, 0);
  int postflags = preflags | O_NONBLOCK;
  fcntl(fileno(f), F_SETFL, postflags);
  
  if (len < 15){len = 15;}
  if ( !checkBufferSize()){
    return false;
  }

  if (done){
    //read a header
    if (FileReadUntil(data, 11, sofar, f)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (FileReadUntil(data, 13, sofar, f)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{
            FLV::Parse_Error = true;
            Error_Str = "Invalid header received.";
            return false;
          }
        }else{
          Util::sleep(100);//sleep 100ms
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if ( !checkBufferSize()){
          return false;
        }
        if (data[0] > 0x12){
          data[0] += 32;
          FLV::Parse_Error = true;
          Error_Str = "Invalid Tag received (";
          Error_Str += data[0];
          Error_Str += ").";
          return false;
        }
        done = false;
      }
    }else{
      Util::sleep(100);//sleep 100ms
    }
  }else{
    //read tag body
    if (FileReadUntil(data, len, sofar, f)){
      //calculate keyframeness, next time read header again, return true
      if ((data[0] == 0x09) && (((data[11] & 0xf0) >> 4) == 1)){
        isKeyframe = true;
      }else{
        isKeyframe = false;
      }
      done = true;
      sofar = 0;
      fcntl(fileno(f), F_SETFL, preflags);
      return true;
    }else{
      Util::sleep(100);//sleep 100ms
    }
  }
  fcntl(fileno(f), F_SETFL, preflags);
  return false;
} //FLV_GetPacket

JSON::Value FLV::Tag::toJSON(DTSC::Meta & metadata){
  JSON::Value pack_out; // Storage for outgoing metadata.

  if (data[0] == 0x12){
    AMF::Object meta_in = AMF::parse((unsigned char*)data + 11, len - 15);
    AMF::Object * tmp = 0;
    if (meta_in.getContentP(1) && meta_in.getContentP(0) && (meta_in.getContentP(0)->StrValue() == "onMetaData")){
      tmp = meta_in.getContentP(1);
    }else{
      if (meta_in.getContentP(2) && meta_in.getContentP(1) && (meta_in.getContentP(1)->StrValue() == "onMetaData")){
        tmp = meta_in.getContentP(2);
      }
    }
    if (tmp){
      if (tmp->getContentP("width")){
        metadata.tracks[1].width = (long long int)tmp->getContentP("width")->NumValue();
      }else{
        metadata.tracks[1].width = 0;
      }
      if (tmp->getContentP("height")){
        metadata.tracks[1].height = (long long int)tmp->getContentP("height")->NumValue();
      }else{
        metadata.tracks[1].height = 0;
      }
      if (tmp->getContentP("framerate")){
        metadata.tracks[1].fpks = (long long int)(tmp->getContentP("framerate")->NumValue() * 1000.0);
      }else{
        metadata.tracks[1].fpks = 0;
      }
      if (tmp->getContentP("videodatarate")){
        metadata.tracks[1].bps = (long long int)(tmp->getContentP("videodatarate")->NumValue() * 1024) / 8;
      }else{
        metadata.tracks[1].bps = 0;
      }
      if (tmp->getContentP("audiodatarate")){
        metadata.tracks[2].bps = (long long int)(tmp->getContentP("audiodatarate")->NumValue() * 1024) / 8;
      }else{
        metadata.tracks[2].bps = 0;
      }
      if (tmp->getContentP("audiosamplerate")){
        metadata.tracks[2].rate = (long long int)tmp->getContentP("audiosamplerate")->NumValue();
      }else{
        metadata.tracks[2].rate = 0;
      }
      if (tmp->getContentP("audiosamplesize")){
        metadata.tracks[2].size = (long long int)tmp->getContentP("audiosamplesize")->NumValue();
      }else{
        metadata.tracks[2].size = 0;
      }
      if (tmp->getContentP("stereo")){
        if (tmp->getContentP("stereo")->NumValue() == 1){
          metadata.tracks[2].channels = 2;
        }else{
          metadata.tracks[2].channels = 1;
        }
      }else{
        metadata.tracks[2].channels = 1;
      }
      for (int i = 0; i < tmp->hasContent(); ++i){
        if (tmp->getContentP(i)->Indice() == "videocodecid" || tmp->getContentP(i)->Indice() == "audiocodecid" || tmp->getContentP(i)->Indice() == "width" || tmp->getContentP(i)->Indice() == "height" || tmp->getContentP(i)->Indice() == "framerate" || tmp->getContentP(i)->Indice() == "videodatarate" || tmp->getContentP(i)->Indice() == "audiodatarate" || tmp->getContentP(i)->Indice() == "audiosamplerate" || tmp->getContentP(i)->Indice() == "audiosamplesize" || tmp->getContentP(i)->Indice() == "audiochannels"){
          continue;
        }
        if (tmp->getContentP(i)->NumValue()){
          pack_out["data"][tmp->getContentP(i)->Indice()] = (long long)tmp->getContentP(i)->NumValue();
        }else{
          if (tmp->getContentP(i)->StrValue() != ""){
            pack_out["data"][tmp->getContentP(i)->Indice()] = tmp->getContentP(i)->StrValue();
          }
        }
      }
      if (pack_out){
        pack_out["datatype"] = "meta";
        pack_out["time"] = tagTime();
      }
    }
    return pack_out; //empty
  }
  if (data[0] == 0x08){
    char audiodata = data[11];
    metadata.tracks[2].trackID = 2;
    metadata.tracks[2].type = "audio";
    if (metadata.tracks[2].codec == ""){
      metadata.tracks[2].codec = getAudioCodec();
    }
    if ( !metadata.tracks[2].rate){
      switch (audiodata & 0x0C){
        case 0x0:
          metadata.tracks[2].rate = 5512;
          break;
        case 0x4:
          metadata.tracks[2].rate = 11025;
          break;
        case 0x8:
          metadata.tracks[2].rate = 22050;
          break;
        case 0xC:
          metadata.tracks[2].rate = 44100;
          break;
      }
    }
    if ( !metadata.tracks[2].size){
      switch (audiodata & 0x02){
        case 0x0:
          metadata.tracks[2].size = 8;
          break;
        case 0x2:
          metadata.tracks[2].size = 16;
          break;
      }
    }
    if ( !metadata.tracks[2].channels){
      switch (audiodata & 0x01){
        case 0x0:
          metadata.tracks[2].channels = 1;
          break;
        case 0x1:
          metadata.tracks[2].channels = 2;
          break;
      }
    }
    if (needsInitData() && isInitData()){
      if ((audiodata & 0xF0) == 0xA0){
        metadata.tracks[2].init = std::string((char*)data + 13, (size_t)len - 17);
      }else{
        metadata.tracks[2].init = std::string((char*)data + 12, (size_t)len - 16);
      }
      return pack_out; //skip rest of parsing, get next tag.
    }
    pack_out["time"] = tagTime();
    pack_out["trackid"] = 2;
    if ((audiodata & 0xF0) == 0xA0){
      if (len < 18){
        return JSON::Value();
      }
      pack_out["data"] = std::string((char*)data + 13, (size_t)len - 17);
    }else{
      if (len < 17){
        return JSON::Value();
      }
      pack_out["data"] = std::string((char*)data + 12, (size_t)len - 16);
    }
    return pack_out;
  }
  if (data[0] == 0x09){
    char videodata = data[11];
    if (metadata.tracks[1].codec == ""){
      metadata.tracks[1].codec = getVideoCodec();
    }
    metadata.tracks[1].type = "video";
    metadata.tracks[1].trackID = 1;
    if (needsInitData() && isInitData()){
      if ((videodata & 0x0F) == 7){
        if (len < 21){
          return JSON::Value();
        }
        metadata.tracks[1].init = std::string((char*)data + 16, (size_t)len - 20);
      }else{
        if (len < 17){
          return JSON::Value();
        }
        metadata.tracks[1].init = std::string((char*)data + 12, (size_t)len - 16);
      }
      return pack_out; //skip rest of parsing, get next tag.
    }
    pack_out["trackid"] = 1;
    switch (videodata & 0xF0){
      case 0x10:
        pack_out["keyframe"] = 1;
        break;
      case 0x20:
        pack_out["interframe"] = 1;
        break;
      case 0x30:
        pack_out["disposableframe"] = 1;
        break;
      case 0x40:
        pack_out["keyframe"] = 1;
        break;
      case 0x50:
        return JSON::Value();
        break; //the video info byte we just throw away - useless to us...
    }
    pack_out["time"] = tagTime();
    if ((videodata & 0x0F) == 7){
      switch (data[12]){
        case 1:
          pack_out["nalu"] = 1;
          break;
        case 2:
          pack_out["nalu_end"] = 1;
          break;
      }
      pack_out["offset"] = offset();
      if (len < 21){
        return JSON::Value();
      }
      pack_out["data"] = std::string((char*)data + 16, (size_t)len - 20);
    }else{
      if (len < 17){
        return JSON::Value();
      }
      pack_out["data"] = std::string((char*)data + 12, (size_t)len - 16);
    }
    return pack_out;
  }
  return pack_out; //should never get here
} //FLV::Tag::toJSON

/// Checks if buf is large enough to contain len.
/// Attempts to resize data buffer if not/
/// \returns True if buffer is large enough, false otherwise.
bool FLV::Tag::checkBufferSize(){
  if (buf < len || !data){
    char * newdata = (char*)realloc(data, len);
    // on realloc fail, retain the old data
    if (newdata != 0){
      data = newdata;
      buf = len;
    }else{
      len = buf;
      return false;
    }
  }
  return true;
}
