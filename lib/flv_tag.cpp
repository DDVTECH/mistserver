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

/// Returns a std::string describing the tag in detail.
/// The string includes information about whether the tag is
/// audio, video or metadata, what encoding is used, and the details
/// of the encoding itself.
std::string FLV::Tag::tagType(){
  std::stringstream R;
  R << len << " bytes of ";
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0x0F){
        case 1:
          R << "JPEG";
          break;
        case 2:
          R << "H263";
          break;
        case 3:
          R << "ScreenVideo1";
          break;
        case 4:
          R << "VP6";
          break;
        case 5:
          R << "VP6Alpha";
          break;
        case 6:
          R << "ScreenVideo2";
          break;
        case 7:
          R << "H264";
          break;
        default:
          R << "unknown";
          break;
      }
      R << " video ";
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
      switch (data[11] & 0xF0){
        case 0x00:
          R << "linear PCM PE";
          break;
        case 0x10:
          R << "ADPCM";
          break;
        case 0x20:
          R << "MP3";
          break;
        case 0x30:
          R << "linear PCM LE";
          break;
        case 0x40:
          R << "Nelly16kHz";
          break;
        case 0x50:
          R << "Nelly8kHz";
          break;
        case 0x60:
          R << "Nelly";
          break;
        case 0x70:
          R << "G711A-law";
          break;
        case 0x80:
          R << "G711mu-law";
          break;
        case 0x90:
          R << "reserved";
          break;
        case 0xA0:
          R << "AAC";
          break;
        case 0xB0:
          R << "Speex";
          break;
        case 0xE0:
          R << "MP38kHz";
          break;
        case 0xF0:
          R << "DeviceSpecific";
          break;
        default:
          R << "unknown";
          break;
      }
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
  buf = O.len;
  len = buf;
  if (len > 0){
    data = (char*)malloc(len);
    memcpy(data, O.data, len);
  }else{
    data = 0;
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

/// Assignment operator - works exactly like the copy constructor.
/// This operator checks for self-assignment.
FLV::Tag & FLV::Tag::operator=(const FLV::Tag& O){
  if (this != &O){ //no self-assignment
    len = O.len;
    if (len > 0){
      if ( !data){
        data = (char*)malloc(len);
        buf = len;
      }else{
        if (buf < len){
          data = (char*)realloc(data, len);
          buf = len;
        }
      }
      memcpy(data, O.data, len);
    }
    isKeyframe = O.isKeyframe;
  }
  return *this;
} //assignment operator

/// FLV loader function from DTSC.
/// Takes the DTSC data and makes it into FLV.
bool FLV::Tag::DTSCLoader(DTSC::Stream & S){
  switch (S.lastType()){
    case DTSC::VIDEO:
      len = S.lastData().length() + 16;
      if (S.metadata.isMember("video") && S.metadata["video"].isMember("codec")){
        if (S.metadata["video"]["codec"].asString() == "H264"){
          len += 4;
        }
      }
      break;
    case DTSC::AUDIO:
      len = S.lastData().length() + 16;
      if (S.metadata.isMember("audio") && S.metadata["audio"].isMember("codec")){
        if (S.metadata["audio"]["codec"].asString() == "AAC"){
          len += 1;
        }
      }
      break;
    case DTSC::META:
      len = S.lastData().length() + 15;
      break;
    default: //ignore all other types (there are currently no other types...)
      break;
  }
  if (len > 0){
    if ( !data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    switch (S.lastType()){
      case DTSC::VIDEO:
        if ((unsigned int)len == S.lastData().length() + 16){
          memcpy(data + 12, S.lastData().c_str(), S.lastData().length());
        }else{
          memcpy(data + 16, S.lastData().c_str(), S.lastData().length());
          if (S.getPacket().isMember("nalu")){
            data[12] = 1;
          }else{
            data[12] = 2;
          }
          int offset = S.getPacket()["offset"].asInt();
          data[13] = (offset >> 16) & 0xFF;
          data[14] = (offset >> 8) & 0XFF;
          data[15] = offset & 0xFF;
        }
        data[11] = 0;
        if (S.metadata["video"]["codec"].asString() == "H264"){
          data[11] += 7;
        }
        if (S.metadata["video"]["codec"].asString() == "H263"){
          data[11] += 2;
        }
        if (S.getPacket().isMember("keyframe")){
          data[11] += 0x10;
        }
        if (S.getPacket().isMember("interframe")){
          data[11] += 0x20;
        }
        if (S.getPacket().isMember("disposableframe")){
          data[11] += 0x30;
        }
        break;
      case DTSC::AUDIO: {
        if ((unsigned int)len == S.lastData().length() + 16){
          memcpy(data + 12, S.lastData().c_str(), S.lastData().length());
        }else{
          memcpy(data + 13, S.lastData().c_str(), S.lastData().length());
          data[12] = 1; //raw AAC data, not sequence header
        }
        data[11] = 0;
        if (S.metadata["audio"]["codec"].asString() == "AAC"){
          data[11] += 0xA0;
        }
        if (S.metadata["audio"]["codec"].asString() == "MP3"){
          data[11] += 0x20;
        }
        unsigned int datarate = S.metadata["audio"]["rate"].asInt();
        if (datarate >= 44100){
          data[11] += 0x0C;
        }else if (datarate >= 22050){
          data[11] += 0x08;
        }else if (datarate >= 11025){
          data[11] += 0x04;
        }
        if (S.metadata["audio"]["size"].asInt() == 16){
          data[11] += 0x02;
        }
        if (S.metadata["audio"]["channels"].asInt() > 1){
          data[11] += 0x01;
        }
        break;
      }
      case DTSC::META:
        memcpy(data + 11, S.lastData().c_str(), S.lastData().length());
        break;
      default:
        break;
    }
  }
  setLen();
  switch (S.lastType()){
    case DTSC::VIDEO:
      data[0] = 0x09;
      break;
    case DTSC::AUDIO:
      data[0] = 0x08;
      break;
    case DTSC::META:
      data[0] = 0x12;
      break;
    default:
      break;
  }
  data[1] = ((len - 15) >> 16) & 0xFF;
  data[2] = ((len - 15) >> 8) & 0xFF;
  data[3] = (len - 15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(S.getPacket()["time"].asInt());
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

/// FLV Video init data loader function from DTSC.
/// Takes the DTSC Video init data and makes it into FLV.
/// Assumes init data is available - so check before calling!
bool FLV::Tag::DTSCVideoInit(DTSC::Stream & S){
  //Unknown? Assume H264.
  if (S.metadata["video"]["codec"].asString() == "?"){
    S.metadata["video"]["codec"] = "H264";
  }
  if (S.metadata["video"]["codec"].asString() == "H264"){
    len = S.metadata["video"]["init"].asString().length() + 20;
  }
  if (len > 0){
    if ( !data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data + 16, S.metadata["video"]["init"].asString().c_str(), len - 20);
    data[12] = 0; //H264 sequence header
    data[13] = 0;
    data[14] = 0;
    data[15] = 0;
    data[11] = 0x17; //H264 keyframe (0x07 & 0x10)
  }
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

/// FLV Audio init data loader function from DTSC.
/// Takes the DTSC Audio init data and makes it into FLV.
/// Assumes init data is available - so check before calling!
bool FLV::Tag::DTSCAudioInit(DTSC::Stream & S){
  len = 0;
  //Unknown? Assume AAC.
  if (S.metadata["audio"]["codec"].asString() == "?"){
    S.metadata["audio"]["codec"] = "AAC";
  }
  if (S.metadata["audio"]["codec"].asString() == "AAC"){
    len = S.metadata["audio"]["init"].asString().length() + 17;
  }
  if (len > 0){
    if ( !data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data + 13, S.metadata["audio"]["init"].asString().c_str(), len - 17);
    data[12] = 0; //AAC sequence header
    data[11] = 0;
    if (S.metadata["audio"]["codec"].asString() == "AAC"){
      data[11] += 0xA0;
    }
    if (S.metadata["audio"]["codec"].asString() == "MP3"){
      data[11] += 0x20;
    }
    unsigned int datarate = S.metadata["audio"]["rate"].asInt();
    if (datarate >= 44100){
      data[11] += 0x0C;
    }else if (datarate >= 22050){
      data[11] += 0x08;
    }else if (datarate >= 11025){
      data[11] += 0x04;
    }
    if (S.metadata["audio"]["size"].asInt() == 16){
      data[11] += 0x02;
    }
    if (S.metadata["audio"]["channels"].asInt() > 1){
      data[11] += 0x01;
    }
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

/// FLV metadata loader function from DTSC.
/// Takes the DTSC metadata and makes it into FLV.
/// Assumes metadata is available - so check before calling!
bool FLV::Tag::DTSCMetaInit(DTSC::Stream & S){
  //Unknown? Assume AAC.
  if (S.metadata["audio"]["codec"].asString() == "?"){
    S.metadata["audio"]["codec"] = "AAC";
  }
  //Unknown? Assume H264.
  if (S.metadata["video"]["codec"].asString() == "?"){
    S.metadata["video"]["codec"] = "H264";
  }

  AMF::Object amfdata("root", AMF::AMF0_DDV_CONTAINER);

  amfdata.addContent(AMF::Object("", "onMetaData"));
  amfdata.addContent(AMF::Object("", AMF::AMF0_ECMA_ARRAY));
  if (S.metadata.isMember("length")){
    amfdata.getContentP(1)->addContent(AMF::Object("duration", S.metadata["length"].asInt(), AMF::AMF0_NUMBER));
    amfdata.getContentP(1)->addContent(AMF::Object("moovPosition", 40, AMF::AMF0_NUMBER));
    AMF::Object keys("keyframes", AMF::AMF0_OBJECT);
    keys.addContent(AMF::Object("filepositions", AMF::AMF0_STRICT_ARRAY));
    keys.addContent(AMF::Object("times", AMF::AMF0_STRICT_ARRAY));
    int total_byterate = 0;
    if (S.metadata.isMember("video")){
      total_byterate += S.metadata["video"]["bps"].asInt();
    }
    if (S.metadata.isMember("audio")){
      total_byterate += S.metadata["audio"]["bps"].asInt();
    }
    for (int i = 0; i < S.metadata["length"].asInt(); ++i){ //for each second in the file
      keys.getContentP(0)->addContent(AMF::Object("", i * total_byterate, AMF::AMF0_NUMBER)); //multiply by byterate for fake byte positions
      keys.getContentP(1)->addContent(AMF::Object("", i, AMF::AMF0_NUMBER)); //seconds
    }
    amfdata.getContentP(1)->addContent(keys);
  }
  if (S.metadata.isMember("video")){
    amfdata.getContentP(1)->addContent(AMF::Object("hasVideo", 1, AMF::AMF0_BOOL));
    if (S.metadata["video"]["codec"].asString() == "H264"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", (std::string)"avc1"));
    }
    if (S.metadata["video"]["codec"].asString() == "VP6"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 4, AMF::AMF0_NUMBER));
    }
    if (S.metadata["video"]["codec"].asString() == "H263"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 2, AMF::AMF0_NUMBER));
    }
    if (S.metadata["video"].isMember("width")){
      amfdata.getContentP(1)->addContent(AMF::Object("width", S.metadata["video"]["width"].asInt(), AMF::AMF0_NUMBER));
    }
    if (S.metadata["video"].isMember("height")){
      amfdata.getContentP(1)->addContent(AMF::Object("height", S.metadata["video"]["height"].asInt(), AMF::AMF0_NUMBER));
    }
    if (S.metadata["video"].isMember("fpks")){
      amfdata.getContentP(1)->addContent(AMF::Object("videoframerate", (double)S.metadata["video"]["fpks"].asInt() / 1000.0, AMF::AMF0_NUMBER));
    }
    if (S.metadata["video"].isMember("bps")){
      amfdata.getContentP(1)->addContent(AMF::Object("videodatarate", (double)S.metadata["video"]["bps"].asInt() * 128.0, AMF::AMF0_NUMBER));
    }
  }
  if (S.metadata.isMember("audio")){
    amfdata.getContentP(1)->addContent(AMF::Object("hasAudio", 1, AMF::AMF0_BOOL));
    amfdata.getContentP(1)->addContent(AMF::Object("audiodelay", 0, AMF::AMF0_NUMBER));
    if (S.metadata["audio"]["codec"].asString() == "AAC"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp4a"));
    }
    if (S.metadata["audio"]["codec"].asString() == "MP3"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", (std::string)"mp3"));
    }
    if (S.metadata["audio"].isMember("channels")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiochannels", S.metadata["audio"]["channels"].asInt(), AMF::AMF0_NUMBER));
    }
    if (S.metadata["audio"].isMember("rate")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplerate", S.metadata["audio"]["rate"].asInt(), AMF::AMF0_NUMBER));
    }
    if (S.metadata["audio"].isMember("size")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplesize", S.metadata["audio"]["size"].asInt(), AMF::AMF0_NUMBER));
    }
    if (S.metadata["audio"].isMember("bps")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiodatarate", (double)S.metadata["audio"]["bps"].asInt() * 128.0, AMF::AMF0_NUMBER));
    }
  }
  AMF::Object trinfo = AMF::Object("trackinfo", AMF::AMF0_STRICT_ARRAY);
  int i = 0;
  if (S.metadata.isMember("audio")){
    trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
    trinfo.getContentP(i)->addContent(
        AMF::Object("length", ((double)S.metadata["length"].asInt()) * ((double)S.metadata["audio"]["rate"].asInt()), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("timescale", S.metadata["audio"]["rate"].asInt(), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    if (S.metadata["audio"]["codec"].asString() == "AAC"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp4a"));
    }
    if (S.metadata["audio"]["codec"].asString() == "MP3"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"mp3"));
    }
    ++i;
  }
  if (S.metadata.isMember("video")){
    trinfo.addContent(AMF::Object("", AMF::AMF0_OBJECT));
    trinfo.getContentP(i)->addContent(
        AMF::Object("length", ((double)S.metadata["length"].asInt()) * ((double)S.metadata["video"]["fkps"].asInt() / 1000.0), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("timescale", ((double)S.metadata["video"]["fkps"].asInt() / 1000.0), AMF::AMF0_NUMBER));
    trinfo.getContentP(i)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    if (S.metadata["video"]["codec"].asString() == "H264"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"avc1"));
    }
    if (S.metadata["video"]["codec"].asString() == "VP6"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"vp6"));
    }
    if (S.metadata["video"]["codec"].asString() == "H263"){
      trinfo.getContentP(i)->getContentP(2)->addContent(AMF::Object("sampletype", (std::string)"h263"));
    }
    ++i;
  }
  amfdata.getContentP(1)->addContent(trinfo);

  std::string tmp = amfdata.Pack();
  len = tmp.length() + 15;
  if (len > 0){
    if ( !data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data + 11, tmp.c_str(), len - 15);
  }
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
    if ( !data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
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
  if (buf < 15){
    data = (char*)realloc(data, 15);
    buf = 15;
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
        if (buf < len){
          data = (char*)realloc(data, len);
          buf = len;
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
  if (buf < 15){
    data = (char*)realloc(data, 15);
    buf = 15;
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
        if (buf < len){
          data = (char*)realloc(data, len);
          buf = len;
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

JSON::Value FLV::Tag::toJSON(JSON::Value & metadata){
  JSON::Value pack_out; // Storage for outgoing metadata.

  if (data[0] == 0x12){
    AMF::Object meta_in = AMF::parse((unsigned char*)data + 11, len - 15);
    if (meta_in.getContentP(0) && (meta_in.getContentP(0)->StrValue() == "onMetaData") && meta_in.getContentP(1)){
      AMF::Object * tmp = meta_in.getContentP(1);
      if (tmp->getContentP("videocodecid")){
        switch ((unsigned int)tmp->getContentP("videocodecid")->NumValue()){
          case 2:
            metadata["video"]["codec"] = "H263";
            break;
          case 4:
            metadata["video"]["codec"] = "VP6";
            break;
          case 7:
            metadata["video"]["codec"] = "H264";
            break;
          default:
            metadata["video"]["codec"] = "?";
            break;
        }
      }
      if (tmp->getContentP("audiocodecid")){
        switch ((unsigned int)tmp->getContentP("audiocodecid")->NumValue()){
          case 2:
            metadata["audio"]["codec"] = "MP3";
            break;
          case 10:
            metadata["audio"]["codec"] = "AAC";
            break;
          default:
            metadata["audio"]["codec"] = "?";
            break;
        }
      }
      if (tmp->getContentP("width")){
        metadata["video"]["width"] = (long long int)tmp->getContentP("width")->NumValue();
      }
      if (tmp->getContentP("height")){
        metadata["video"]["height"] = (long long int)tmp->getContentP("height")->NumValue();
      }
      if (tmp->getContentP("framerate")){
        metadata["video"]["fpks"] = (long long int)(tmp->getContentP("framerate")->NumValue() * 1000.0);
      }
      if (tmp->getContentP("videodatarate")){
        metadata["video"]["bps"] = (long long int)(tmp->getContentP("videodatarate")->NumValue() * 1024) / 8;
      }
      if (tmp->getContentP("audiodatarate")){
        metadata["audio"]["bps"] = (long long int)(tmp->getContentP("audiodatarate")->NumValue() * 1024) / 8;
      }
      if (tmp->getContentP("audiosamplerate")){
        metadata["audio"]["rate"] = (long long int)tmp->getContentP("audiosamplerate")->NumValue();
      }
      if (tmp->getContentP("audiosamplesize")){
        metadata["audio"]["size"] = (long long int)tmp->getContentP("audiosamplesize")->NumValue();
      }
      if (tmp->getContentP("stereo")){
        if (tmp->getContentP("stereo")->NumValue() == 1){
          metadata["audio"]["channels"] = 2;
        }else{
          metadata["audio"]["channels"] = 1;
        }
      }
    }
    if ( !metadata.isMember("length")){
      metadata["length"] = 0;
    }
    if (metadata.isMember("video")){
      if ( !metadata["video"].isMember("width")){
        metadata["video"]["width"] = 0;
      }
      if ( !metadata["video"].isMember("height")){
        metadata["video"]["height"] = 0;
      }
      if ( !metadata["video"].isMember("fpks")){
        metadata["video"]["fpks"] = 0;
      }
      if ( !metadata["video"].isMember("bps")){
        metadata["video"]["bps"] = 0;
      }
      if ( !metadata["video"].isMember("keyms")){
        metadata["video"]["keyms"] = 0;
      }
      if ( !metadata["video"].isMember("keyvar")){
        metadata["video"]["keyvar"] = 0;
      }
    }
    return pack_out; //empty
  }
  if (data[0] == 0x08){
    char audiodata = data[11];
    if (needsInitData() && isInitData()){
      if ((audiodata & 0xF0) == 0xA0){
        metadata["audio"]["init"] = std::string((char*)data + 13, (size_t)len - 17);
      }else{
        metadata["audio"]["init"] = std::string((char*)data + 12, (size_t)len - 16);
      }
      return pack_out; //skip rest of parsing, get next tag.
    }
    pack_out["datatype"] = "audio";
    pack_out["time"] = tagTime();
    if ( !metadata["audio"].isMember("codec") || metadata["audio"]["size"].asString() == ""){
      switch (audiodata & 0xF0){
        case 0x20:
          metadata["audio"]["codec"] = "MP3";
          break;
        case 0xA0:
          metadata["audio"]["codec"] = "AAC";
          break;
      }
    }
    if ( !metadata["audio"].isMember("rate") || metadata["audio"]["rate"].asInt() < 1){
      switch (audiodata & 0x0C){
        case 0x0:
          metadata["audio"]["rate"] = 5512;
          break;
        case 0x4:
          metadata["audio"]["rate"] = 11025;
          break;
        case 0x8:
          metadata["audio"]["rate"] = 22050;
          break;
        case 0xC:
          metadata["audio"]["rate"] = 44100;
          break;
      }
    }
    if ( !metadata["audio"].isMember("size") || metadata["audio"]["size"].asInt() < 1){
      switch (audiodata & 0x02){
        case 0x0:
          metadata["audio"]["size"] = 8;
          break;
        case 0x2:
          metadata["audio"]["size"] = 16;
          break;
      }
    }
    if ( !metadata["audio"].isMember("channels") || metadata["audio"]["channels"].asInt() < 1){
      switch (audiodata & 0x01){
        case 0x0:
          metadata["audio"]["channels"] = 1;
          break;
        case 0x1:
          metadata["audio"]["channels"] = 2;
          break;
      }
    }
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
    if (needsInitData() && isInitData()){
      if ((videodata & 0x0F) == 7){
        if (len < 21){
          return JSON::Value();
        }
        metadata["video"]["init"] = std::string((char*)data + 16, (size_t)len - 20);
      }else{
        if (len < 17){
          return JSON::Value();
        }
        metadata["video"]["init"] = std::string((char*)data + 12, (size_t)len - 16);
      }
      return pack_out; //skip rest of parsing, get next tag.
    }
    if ( !metadata["video"].isMember("codec") || metadata["video"]["codec"].asString() == ""){
      switch (videodata & 0x0F){
        case 2:
          metadata["video"]["codec"] = "H263";
          break;
        case 4:
          metadata["video"]["codec"] = "VP6";
          break;
        case 7:
          metadata["video"]["codec"] = "H264";
          break;
      }
    }
    pack_out["datatype"] = "video";
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
      int offset = (data[13] << 16) + (data[14] << 8) + data[15];
      offset = (offset << 8) >> 8;
      pack_out["offset"] = offset;
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
