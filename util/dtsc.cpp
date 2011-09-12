/// \file dtsc.cpp
/// Holds all code for DDVTECH Stream Container parsing/generation.

#include "dtsc.h"
#include "string.h" //for memcmp
#include "arpa/inet.h" //for htonl/ntohl

char * DTSC::Magic_Header = "DTSC";
char * DTSC::Magic_Packet = "DTPD";

DTSC::Stream::Stream(){
  datapointer = 0;
}

bool DTSC::Stream::parsePacket(std::string & buffer){
  uint32_t len;
  if (buffer.length() > 8){
    if (memcmp(buffer.c_str(), DTSC::Magic_Header, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len+8){return false;}
      metadata = DTSC::parseDTMI(buffer.c_str() + 8, len);
    }
    if (memcmp(buffer.c_str(), DTSC::Magic_Packet, 4) == 0){
      len = ntohl(((uint32_t *)buffer.c_str())[1]);
      if (buffer.length() < len+8){return false;}
      lastPacket = DTSC::parseDTMI(buffer.c_str() + 8, len);
      datapointertype = INVALID;
      if (lastPacket.getContentP("data")){
        datapointer = lastPacket.getContentP("data")->StrValue.c_str();
        if (lastPacket.getContentP("datatype")){
          std::string tmp = lastPacket.getContentP("datatype")->StrValue();
          if (tmp == "video"){datapointertype = VIDEO;}
          if (tmp == "audio"){datapointertype = AUDIO;}
          if (tmp == "meta"){datapointertype = META;}
        }
      }else{
        datapointer = 0;
      }
    }
  }
  return false;
}

char * DTSC::Stream::lastData(){
  return datapointer;
}

DTSC::datatype DTSC::Stream::lastType(){
  return datapointertype;
}

bool DTSC::Stream::hasVideo(){
  return (metadata.getContentP("video") != 0);
}

bool DTSC::Stream::hasAudio(){
  return (metadata.getContentP("audio") != 0);
}
