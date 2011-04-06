#include "flv_tag.h"
#include <stdio.h> //for Tag::FileLoader
#include <unistd.h> //for Tag::FileLoader
#include <fcntl.h> //for Tag::FileLoader
#include <stdlib.h> //malloc
#include <string.h> //memcpy
#include "ddv_socket.h" //socket functions

char FLV::Header[13]; ///< Holds the last FLV header parsed.
bool FLV::Parse_Error = false; ///< This variable is set to true if a problem is encountered while parsing the FLV.

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


/// Returns a std::string describing the tag in detail.
/// The string includes information about whether the tag is
/// audio, video or metadata, what encoding is used, and the details
/// of the encoding itself.
std::string FLV::Tag::tagType(){
  std::string R = "";
  switch (data[0]){
    case 0x09:
      switch (data[11] & 0x0F){
        case 1: R += "JPEG"; break;
        case 2: R += "H263"; break;
        case 3: R += "ScreenVideo1"; break;
        case 4: R += "VP6"; break;
        case 5: R += "VP6Alpha"; break;
        case 6: R += "ScreenVideo2"; break;
        case 7: R += "AVC"; break;
        default: R += "unknown"; break;
      }
    R += " video ";
    switch (data[11] & 0xF0){
        case 0x10: R += "keyframe"; break;
        case 0x20: R += "iframe"; break;
        case 0x30: R += "disposableiframe"; break;
        case 0x40: R += "generatedkeyframe"; break;
        case 0x50: R += "videoinfo"; break;
      }
      if ((data[11] & 0x0F) == 7){
        switch (data[12]){
          case 0: R += " header"; break;
          case 1: R += " NALU"; break;
          case 2: R += " endofsequence"; break;
        }
      }
      break;
    case 0x08:
      switch (data[11] & 0xF0){
        case 0x00: R += "linear PCM PE"; break;
        case 0x10: R += "ADPCM"; break;
        case 0x20: R += "MP3"; break;
        case 0x30: R += "linear PCM LE"; break;
        case 0x40: R += "Nelly16kHz"; break;
        case 0x50: R += "Nelly8kHz"; break;
        case 0x60: R += "Nelly"; break;
        case 0x70: R += "G711A-law"; break;
        case 0x80: R += "G711mu-law"; break;
        case 0x90: R += "reserved"; break;
        case 0xA0: R += "AAC"; break;
        case 0xB0: R += "Speex"; break;
        case 0xE0: R += "MP38kHz"; break;
        case 0xF0: R += "DeviceSpecific"; break;
        default: R += "unknown"; break;
      }
      switch (data[11] & 0x0C){
        case 0x0: R += " 5.5kHz"; break;
        case 0x4: R += " 11kHz"; break;
        case 0x8: R += " 22kHz"; break;
        case 0xC: R += " 44kHz"; break;
      }
      switch (data[11] & 0x02){
        case 0: R += " 8bit"; break;
        case 2: R += " 16bit"; break;
      }
      switch (data[11] & 0x01){
        case 0: R += " mono"; break;
        case 1: R += " stereo"; break;
      }
      R += " audio";
      if ((data[12] == 0) && ((data[11] & 0xF0) == 0xA0)){
        R += " initdata";
      }
      break;
    case 0x12:
      R += "(meta)data";
      break;
    default:
      R += "unknown";
      break;
  }
  return R;
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
  len = 0; buf = 0; data = 0; isKeyframe = false;
}//empty constructor

/// Copy constructor, copies the contents of an existing tag.
/// The buffer length is initialized to the actual size of the tag
/// that is being copied, and later automaticallt increased if
/// neccesary.
FLV::Tag::Tag(const Tag& O){
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

/// Assignment operator - works exactly like the copy constructor.
/// This operator checks for self-assignment.
FLV::Tag & FLV::Tag::operator= (const FLV::Tag& O){
  if (this != &O){//no self-assignment
    if (data != 0){free(data);}
    buf = O.len;
    len = buf;
    if (len > 0){
      data = (char*)malloc(len);
      memcpy(data, O.data, len);
    }else{
      data = 0;
    }
    isKeyframe = O.isKeyframe;
  }
  return *this;
}//assignment operator

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
  static bool done = true;
  static unsigned int sofar = 0;
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
          }else{FLV::Parse_Error = true; return false;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
        if (data[0] > 0x12){FLV::Parse_Error = true; return false;}
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
bool FLV::Tag::SockReadUntil(char * buffer, unsigned int count, unsigned int & sofar, DDV::Socket & sock){
  if (sofar == count){return true;}
  int r = sock.read(buffer + sofar,count-sofar);
  if (r < 0){
    if (errno != EWOULDBLOCK){
      FLV::Parse_Error = true;
      fprintf(stderr, "ReadUntil fail: %s. All Hell Broke Loose!\n", strerror(errno));
    }
    return false;
  }
  sofar += r;
  if (sofar == count){return true;}
  if (sofar > count){
    FLV::Parse_Error = true;
    fprintf(stderr, "ReadUntil fail: %s. Read too much. All Hell Broke Loose!\n", strerror(errno));
  }
  return false;
}//Tag::SockReadUntil

/// Try to load a tag from a socket.
/// This is a stateful function - if fed incorrect data, it will most likely never return true again!
/// While this function returns false, the Tag might not contain valid data.
/// \param sock The socket to read from.
/// \return True if a whole tag is succesfully read, false otherwise.
bool FLV::Tag::SockLoader(DDV::Socket sock){
  static bool done = true;
  static unsigned int sofar = 0;
  if (buf < 15){data = (char*)realloc(data, 15); buf = 15;}
  if (done){
    if (SockReadUntil(data, 11, sofar, sock)){
      //if its a correct FLV header, throw away and read tag header
      if (FLV::is_header(data)){
        if (SockReadUntil(data, 13, sofar, sock)){
          if (FLV::check_header(data)){
            sofar = 0;
            memcpy(FLV::Header, data, 13);
          }else{FLV::Parse_Error = true; return false;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
        if (data[0] > 0x12){FLV::Parse_Error = true; return false;}
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
  return SockLoader(DDV::Socket(sock));
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
  if (r < 0){FLV::Parse_Error = true; return false;}
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
  static bool done = true;
  static unsigned int sofar = 0;
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
          }else{FLV::Parse_Error = true;}
        }
      }else{
        //if a tag header, calculate length and read tag body
        len = data[3] + 15;
        len += (data[2] << 8);
        len += (data[1] << 16);
        if (buf < len){data = (char*)realloc(data, len); buf = len;}
        if (data[0] > 0x12){FLV::Parse_Error = true;}
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
