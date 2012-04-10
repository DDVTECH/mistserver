/// \file flv_tag.cpp
/// Holds all code for the FLV namespace.

#include "flv_tag.h"
#include "amf.h"
#include "rtmpchunks.h"
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
}//FLV::check_header

/// Checks the first 3 bytes for the string "FLV". Implementing a basic FLV header check,
/// returning true if it is, false if not.
bool FLV::is_header(char * header){
  if (header[0] != 'F') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'V') return false;
  return true;
}//FLV::is_header

/// True if this media type requires init data.
/// Will always return false if the tag type is not 0x08 or 0x09.
/// Returns true for H263, AVC (H264), AAC.
/// \todo Check if MP3 does or does not require init data...
bool FLV::Tag::needsInitData(){
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0x0F){
        case 2: return true; break;//H263 requires init data
        case 7: return true; break;//AVC requires init data
        default: return false; break;//other formats do not
      }
      break;
    case 0x08:
      switch (data[11] & 0xF0){
        case 0x20: return false; break;//MP3 does not...? Unsure.
        case 0xA0: return true; break;//AAC requires init data
        case 0xE0: return false; break;//MP38kHz does not...?
        default: return false; break;//other formats do not
      }
      break;
  }
  return false;//only audio/video can require init data
}

/// True if current tag is init data for this media type.
bool FLV::Tag::isInitData(){
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0xF0){
        case 0x50: return true; break;
      }
      if ((data[11] & 0x0F) == 7){
        switch (data[12]){
          case 0: return true; break;
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
        case 1: R << "JPEG"; break;
        case 2: R << "H263"; break;
        case 3: R << "ScreenVideo1"; break;
        case 4: R << "VP6"; break;
        case 5: R << "VP6Alpha"; break;
        case 6: R << "ScreenVideo2"; break;
        case 7: R << "H264"; break;
        default: R << "unknown"; break;
      }
    R << " video ";
    switch (data[11] & 0xF0){
        case 0x10: R << "keyframe"; break;
        case 0x20: R << "iframe"; break;
        case 0x30: R << "disposableiframe"; break;
        case 0x40: R << "generatedkeyframe"; break;
        case 0x50: R << "videoinfo"; break;
      }
      if ((data[11] & 0x0F) == 7){
        switch (data[12]){
          case 0: R << " header"; break;
          case 1: R << " NALU"; break;
          case 2: R << " endofsequence"; break;
        }
      }
      break;
    case 0x08:
      switch (data[11] & 0xF0){
        case 0x00: R << "linear PCM PE"; break;
        case 0x10: R << "ADPCM"; break;
        case 0x20: R << "MP3"; break;
        case 0x30: R << "linear PCM LE"; break;
        case 0x40: R << "Nelly16kHz"; break;
        case 0x50: R << "Nelly8kHz"; break;
        case 0x60: R << "Nelly"; break;
        case 0x70: R << "G711A-law"; break;
        case 0x80: R << "G711mu-law"; break;
        case 0x90: R << "reserved"; break;
        case 0xA0: R << "AAC"; break;
        case 0xB0: R << "Speex"; break;
        case 0xE0: R << "MP38kHz"; break;
        case 0xF0: R << "DeviceSpecific"; break;
        default: R << "unknown"; break;
      }
      switch (data[11] & 0x0C){
        case 0x0: R << " 5.5kHz"; break;
        case 0x4: R << " 11kHz"; break;
        case 0x8: R << " 22kHz"; break;
        case 0xC: R << " 44kHz"; break;
      }
      switch (data[11] & 0x02){
        case 0: R << " 8bit"; break;
        case 2: R << " 16bit"; break;
      }
      switch (data[11] & 0x01){
        case 0: R << " mono"; break;
        case 1: R << " stereo"; break;
      }
      R << " audio";
      if ((data[12] == 0) && ((data[11] & 0xF0) == 0xA0)){
        R << " initdata";
      }
      break;
    case 0x12:{
      R << "(meta)data: ";
      AMF::Object metadata = AMF::parse((unsigned char*)data+11, len-15);
      R << metadata.Print();
      break;
    }
    default:
      R << "unknown";
      break;
  }
  return R.str();
}//FLV::Tag::tagtype

/// Returns the 32-bit timestamp of this tag.
unsigned int FLV::Tag::tagTime(){
  return (data[4] << 16) + (data[5] << 8) + data[6] + (data[7] << 24);
}//tagTime getter

/// Sets the 32-bit timestamp of this tag.
void FLV::Tag::tagTime(unsigned int T){
  data[4] = ((T >> 16) & 0xFF);
  data[5] = ((T >> 8) & 0xFF);
  data[6] = (T & 0xFF);
  data[7] = ((T >> 24) & 0xFF);
}//tagTime setter

/// Constructor for a new, empty, tag.
/// The buffer length is initialized to 0, and later automatically
/// increased if neccesary.
FLV::Tag::Tag(){
  len = 0; buf = 0; data = 0; isKeyframe = false; done = true; sofar = 0;
}//empty constructor

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
}//copy constructor


/// Copy constructor from a RTMP chunk.
/// Copies the contents of a RTMP chunk into a valid FLV tag.
/// Exactly the same as making a chunk by through the default (empty) constructor
/// and then calling FLV::Tag::ChunkLoader with the chunk as argument.
FLV::Tag::Tag(const RTMPStream::Chunk& O){
  len = 0; buf = 0; data = 0; isKeyframe = false; done = true; sofar = 0;
  ChunkLoader(O);
}

/// Assignment operator - works exactly like the copy constructor.
/// This operator checks for self-assignment.
FLV::Tag & FLV::Tag::operator= (const FLV::Tag& O){
  if (this != &O){//no self-assignment
    len = O.len;
    if (len > 0){
      if (!data){
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
}//assignment operator

/// FLV loader function from DTSC.
/// Takes the DTSC data and makes it into FLV.
bool FLV::Tag::DTSCLoader(DTSC::Stream & S){
  switch (S.lastType()){
    case DTSC::VIDEO:
      len = S.lastData().length() + 16;
      if (S.metadata.getContentP("video") && S.metadata.getContentP("video")->getContentP("codec")){
        if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H264"){len += 4;}
      }
      break;
    case DTSC::AUDIO:
      len = S.lastData().length() + 16;
      if (S.metadata.getContentP("audio") && S.metadata.getContentP("audio")->getContentP("codec")){
        if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "AAC"){len += 1;}
      }
      break;
    case DTSC::META:
      len = S.lastData().length() + 15;
      break;
    default://ignore all other types (there are currently no other types...)
      break;
  }
  if (len > 0){
    if (!data){
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
          memcpy(data+12, S.lastData().c_str(), S.lastData().length());
        }else{
          memcpy(data+16, S.lastData().c_str(), S.lastData().length());
          if (S.getPacket().getContentP("nalu")){data[12] = 1;}else{data[12] = 2;}
          int offset = S.getPacket().getContentP("offset")->NumValue();
          data[13] = (offset >> 16) & 0xFF;
          data[14] = (offset >> 8) & 0XFF;
          data[15] = offset & 0xFF;
        }
        data[11] = 0;
        if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H264"){data[11] += 7;}
        if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H263"){data[11] += 2;}
        if (S.getPacket().getContentP("keyframe")){data[11] += 0x10;}
        if (S.getPacket().getContentP("interframe")){data[11] += 0x20;}
        if (S.getPacket().getContentP("disposableframe")){data[11] += 0x30;}
        break;
      case DTSC::AUDIO:{
        if ((unsigned int)len == S.lastData().length() + 16){
          memcpy(data+12, S.lastData().c_str(), S.lastData().length());
        }else{
          memcpy(data+13, S.lastData().c_str(), S.lastData().length());
          data[12] = 1;//raw AAC data, not sequence header
        }
        data[11] = 0;
        if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "AAC"){data[11] += 0xA0;}
        if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "MP3"){data[11] += 0x20;}
        unsigned int datarate = S.metadata.getContentP("audio")->getContentP("rate")->NumValue();
        if (datarate >= 44100){
          data[11] += 0x0C;
        }else if(datarate >= 22050){
          data[11] += 0x08;
        }else if(datarate >= 11025){
          data[11] += 0x04;
        }
        if (S.metadata.getContentP("audio")->getContentP("size")->NumValue() == 16){data[11] += 0x02;}
        if (S.metadata.getContentP("audio")->getContentP("channels")->NumValue() > 1){data[11] += 0x01;}
        break;
      }
      case DTSC::META:
        memcpy(data+11, S.lastData().c_str(), S.lastData().length());
        break;
      default: break;
    }
  }
  setLen();
  switch (S.lastType()){
    case DTSC::VIDEO: data[0] = 0x09; break;
    case DTSC::AUDIO: data[0] = 0x08; break;
    case DTSC::META: data[0] = 0x12; break;
    default: break;
  }
  data[1] = ((len-15) >> 16) & 0xFF;
  data[2] = ((len-15) >> 8) & 0xFF;
  data[3] = (len-15) & 0xFF;
  data[8] = 0;
  data[9] = 0;
  data[10] = 0;
  tagTime(S.getPacket().getContentP("time")->NumValue());
  return true;
}

/// Helper function that properly sets the tag length from the internal len variable.
void FLV::Tag::setLen(){
  int len4 = len - 4;
  int i = len;
  data[--i] = (len4) & 0xFF;
  len4 >>= 8;
  data[--i] = (len4) & 0xFF;
  len4 >>= 8;
  data[--i] = (len4) & 0xFF;
  len4 >>= 8;
  data[--i] = (len4) & 0xFF;
}

/// FLV Video init data loader function from DTSC.
/// Takes the DTSC Video init data and makes it into FLV.
/// Assumes init data is available - so check before calling!
bool FLV::Tag::DTSCVideoInit(DTSC::Stream & S){
  if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H264"){
    len = S.metadata.getContentP("video")->getContentP("init")->StrValue().length() + 20;
  }
  if (len > 0){
    if (!data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data+16, S.metadata.getContentP("video")->getContentP("init")->StrValue().c_str(), len-20);
    data[12] = 0;//H264 sequence header
    data[13] = 0;
    data[14] = 0;
    data[15] = 0;
    data[11] = 0x17;//H264 keyframe (0x07 & 0x10)
  }
  setLen();
  data[0] = 0x09;
  data[1] = ((len-15) >> 16) & 0xFF;
  data[2] = ((len-15) >> 8) & 0xFF;
  data[3] = (len-15) & 0xFF;
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
  if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "AAC"){
    len = S.metadata.getContentP("audio")->getContentP("init")->StrValue().length() + 17;
  }
  if (len > 0){
    if (!data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data+13, S.metadata.getContentP("audio")->getContentP("init")->StrValue().c_str(), len-17);
    data[12] = 0;//AAC sequence header
    data[11] = 0;
    if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "AAC"){data[11] += 0xA0;}
    if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "MP3"){data[11] += 0x20;}
    unsigned int datarate = S.metadata.getContentP("audio")->getContentP("rate")->NumValue();
    if (datarate >= 44100){
      data[11] += 0x0C;
    }else if(datarate >= 22050){
      data[11] += 0x08;
    }else if(datarate >= 11025){
      data[11] += 0x04;
    }
    if (S.metadata.getContentP("audio")->getContentP("size")->NumValue() == 16){data[11] += 0x02;}
    if (S.metadata.getContentP("audio")->getContentP("channels")->NumValue() > 1){data[11] += 0x01;}
  }
  setLen();
  data[0] = 0x08;
  data[1] = ((len-15) >> 16) & 0xFF;
  data[2] = ((len-15) >> 8) & 0xFF;
  data[3] = (len-15) & 0xFF;
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
  AMF::Object amfdata("root", AMF::AMF0_DDV_CONTAINER);

  amfdata.addContent(AMF::Object("", "onMetaData"));
  amfdata.addContent(AMF::Object("", AMF::AMF0_ECMA_ARRAY));
  if (S.metadata.getContentP("video")){
    amfdata.getContentP(1)->addContent(AMF::Object("hasVideo", 1, AMF::AMF0_BOOL));
    if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H264"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 7, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "VP6"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 4, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("codec")->StrValue() == "H263"){
      amfdata.getContentP(1)->addContent(AMF::Object("videocodecid", 2, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("width")){
      amfdata.getContentP(1)->addContent(AMF::Object("width", S.metadata.getContentP("video")->getContentP("width")->NumValue(), AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("height")){
      amfdata.getContentP(1)->addContent(AMF::Object("height", S.metadata.getContentP("video")->getContentP("height")->NumValue(), AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("fpks")){
      amfdata.getContentP(1)->addContent(AMF::Object("framerate", (double)S.metadata.getContentP("video")->getContentP("fpks")->NumValue() / 1000.0, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("video")->getContentP("bps")){
      amfdata.getContentP(1)->addContent(AMF::Object("videodatarate", ((double)S.metadata.getContentP("video")->getContentP("bps")->NumValue() * 8.0) / 1024.0, AMF::AMF0_NUMBER));
    }
  }
  if (S.metadata.getContentP("audio")){
    amfdata.getContentP(1)->addContent(AMF::Object("hasAudio", 1, AMF::AMF0_BOOL));
    amfdata.getContentP(1)->addContent(AMF::Object("audiodelay", 0, AMF::AMF0_NUMBER));
    if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "AAC"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", 10, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("audio")->getContentP("codec")->StrValue() == "MP3"){
      amfdata.getContentP(1)->addContent(AMF::Object("audiocodecid", 2, AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("audio")->getContentP("channels")){
      if (S.metadata.getContentP("audio")->getContentP("channels")->NumValue() > 1){
        amfdata.getContentP(1)->addContent(AMF::Object("stereo", 1, AMF::AMF0_BOOL));
      }else{
        amfdata.getContentP(1)->addContent(AMF::Object("stereo", 0, AMF::AMF0_BOOL));
      }
    }
    if (S.metadata.getContentP("audio")->getContentP("rate")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplerate", S.metadata.getContentP("audio")->getContentP("rate")->NumValue(), AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("audio")->getContentP("size")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiosamplesize", S.metadata.getContentP("audio")->getContentP("size")->NumValue(), AMF::AMF0_NUMBER));
    }
    if (S.metadata.getContentP("audio")->getContentP("bps")){
      amfdata.getContentP(1)->addContent(AMF::Object("audiodatarate", ((double)S.metadata.getContentP("audio")->getContentP("bps")->NumValue() * 8.0) / 1024.0, AMF::AMF0_NUMBER));
    }
  }
  
  std::string tmp = amfdata.Pack();
  len = tmp.length() + 15;
  if (len > 0){
    if (!data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data+11, tmp.c_str(), len-15);
  }
  setLen();
  data[0] = 0x12;
  data[1] = ((len-15) >> 16) & 0xFF;
  data[2] = ((len-15) >> 8) & 0xFF;
  data[3] = (len-15) & 0xFF;
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
    if (!data){
      data = (char*)malloc(len);
      buf = len;
    }else{
      if (buf < len){
        data = (char*)realloc(data, len);
        buf = len;
      }
    }
    memcpy(data+11, &(O.data[0]), O.len);
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
  if (sofar >= count){return true;}
  int r = 0;
  if (P+(count-sofar) > S){r = S-P;}else{r = count-sofar;}
  memcpy(buffer+sofar, D+P, r);
  P += r;
  sofar += r;
  if (sofar >= count){return true;}
  return false;
}//Tag::MemReadUntil


/// Try to load a tag from a data buffer in memory.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param D The location of the data buffer.
/// \param S The size of the data buffer.
/// \param P The current position in the data buffer. Will be updated to reflect new position.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::MemLoader(char * D, unsigned int S, unsigned int & P){
  if (buf < 15){data = (char*)realloc(data, 15); buf = 15;}
  if (done){
    //read a header
    if (MemReadUntil(data, 11, sofar, D, S, P)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (MemReadUntil(data, 13, sofar, D, S, P)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{FLV::Parse_Error = true; Error_Str = "Invalid header received."; return false;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
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
      if ((data[0] == 0x09) && (((data[11] & 0xf0) >> 4) == 1)){isKeyframe = true;}else{isKeyframe = false;}
      done = true;
      sofar = 0;
      return true;
    }
  }
  return false;
}//Tag::MemLoader


/// Helper function for FLV::SockLoader.
/// This function will try to read count bytes from socket sock into buffer.
/// This function should be called repeatedly until true.
/// \param buffer The target buffer.
/// \param count Amount of bytes to read.
/// \param sofar Current amount read.
/// \param sock Socket to read from.
/// \return True if count bytes are read succesfully, false otherwise.
bool FLV::Tag::SockReadUntil(char * buffer, unsigned int count, unsigned int & sofar, Socket::Connection & sock){
  if (sofar >= count){return true;}
  int r = 0;
  r = sock.iread(buffer + sofar,count-sofar);
  sofar += r;
  if (sofar >= count){return true;}
  return false;
}//Tag::SockReadUntil

/// Try to load a tag from a socket.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param sock The socket to read from.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::SockLoader(Socket::Connection sock){
  if (buf < 15){data = (char*)realloc(data, 15); buf = 15;}
  if (done){
    if (SockReadUntil(data, 11, sofar, sock)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (SockReadUntil(data, 13, sofar, sock)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{FLV::Parse_Error = true; Error_Str = "Invalid header received."; return false;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
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
    if (SockReadUntil(data, len, sofar, sock)){
      //calculate keyframeness, next time read header again, return true
      if ((data[0] == 0x09) && (((data[11] & 0xf0) >> 4) == 1)){isKeyframe = true;}else{isKeyframe = false;}
      done = true;
      sofar = 0;
      return true;
    }
  }
  return false;
}//Tag::SockLoader

/// Try to load a tag from a socket.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param sock The socket to read from.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::SockLoader(int sock){
  return SockLoader(Socket::Connection(sock));
}//Tag::SockLoader

/// Helper function for FLV::FileLoader.
/// This function will try to read count bytes from file f into buffer.
/// This function should be called repeatedly until true.
/// \param buffer The target buffer.
/// \param count Amount of bytes to read.
/// \param sofar Current amount read.
/// \param f File to read from.
/// \return True if count bytes are read succesfully, false otherwise.
bool FLV::Tag::FileReadUntil(char * buffer, unsigned int count, unsigned int & sofar, FILE * f){
  if (sofar >= count){return true;}
  int r = 0;
  r = fread(buffer + sofar,1,count-sofar,f);
  if (r < 0){FLV::Parse_Error = true; Error_Str = "File reading error."; return false;}
  sofar += r;
  if (sofar >= count){return true;}
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
  if (buf < 15){data = (char*)realloc(data, 15); buf = 15;}

  if (done){
    //read a header
    if (FileReadUntil(data, 11, sofar, f)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (FileReadUntil(data, 13, sofar, f)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{FLV::Parse_Error = true; Error_Str = "Invalid header received."; return false;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
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
    if (FileReadUntil(data, len, sofar, f)){
      //calculate keyframeness, next time read header again, return true
      if ((data[0] == 0x09) && (((data[11] & 0xf0) >> 4) == 1)){isKeyframe = true;}else{isKeyframe = false;}
      done = true;
      sofar = 0;
      fcntl(fileno(f), F_SETFL, preflags);
      return true;
    }
  }
  fcntl(fileno(f), F_SETFL, preflags);
  return false;
}//FLV_GetPacket
