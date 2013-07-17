#include"vorbis.h"
#include<stdlib.h>
#include<string.h>
#include<sstream>
#include <arpa/inet.h>

namespace vorbis{

  header::header(){
    data = NULL;
    datasize = 0;
  }
  
  int header::getHeaderType(){
    return (int)(data[0]);
  }
  
  long unsigned int header::getVorbisVersion(){
    if (getHeaderType() == 1){
      return getInt32(7);
    }else{
      return 0;
    }
  }
  
  char header::getAudioChannels(){
    if (getHeaderType() == 1){
      return data[11];
    }else{
      return 0;
    }
  }
  
  long unsigned int header::getAudioSampleRate(){
    if (getHeaderType() == 1){
      return getInt32(12);
    }else{
      return 0;
    }
  }
  
  long unsigned int header::getBitrateMaximum(){
    if (getHeaderType() == 1){
      return getInt32(16);
    }else{
      return 0;
    }
  }
  
  long unsigned int header::getBitrateNominal(){
    if (getHeaderType() == 1){
      return getInt32(20);
    }else{
      return 0;
    }
  }
  
  long unsigned int header::getBitrateMinimum(){
    if (getHeaderType() == 1){
      return getInt32(24);
    }else{
      return 0;
    }

  }
  
  char header::getBlockSize0(){
    if (getHeaderType() == 1){
      return data[28]>>4;
    }else{
      return 0;
    }
  }
  
  char header::getBlockSize1(){
    if (getHeaderType() == 1){
      return data[28] & 0x0F;
    }else{
      return 0;
    }
  }
  
  char header::getFramingFlag(){
    if (getHeaderType() == 1){
      return data[29];
    }else{
      return 0;
    }
  }
  
  bool header::checkDataSize(unsigned int size){
    if (size > datasize){
      void* tmp = realloc(data,size);
      if (tmp){
        data = (char*)tmp;
        datasize = size;
        return true;
      }else{
        return false;
      }
    }else{
      return true;
    }
  }

  bool header::validate(){
    switch(getHeaderType()){
      case 1://ID header
        if (datasize!=30) return false;
        if (getVorbisVersion()!=0) return false;
        if (getAudioChannels()<=0) return false;
        if (getAudioSampleRate()<=0) return false;
        if (getBlockSize0()>getBlockSize1()) return false;
        if (getFramingFlag()!=1) return false;
      break;      
      case 3://comment header
      break;      
      case 5://init header
      break;      
      default:
        return false;
      break;
    }
    return true;
  }

  bool header::read(char* newData, unsigned int length){
    if (length < 7){
      return false;
    }
    /*if (! (newData[0] & 0x80)){
      return false;
    }*/
    if(memcmp(newData+1, "vorbis", 6)!=0){
      return false;
    }

    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    return validate();
  }



  uint32_t header::getInt32(size_t index){
    if (datasize >= (index + 3)){
      return (data[index] << 24) + (data[index + 1] << 16) + (data[index + 2] << 8) + data[index + 3];
    }
    return 0;
  }

  uint32_t header::getInt24(size_t index){
    if (datasize >= (index + 2)){
      return 0 + (data[index] << 16) + (data[index + 1] << 8) + data[index + 2];
    }
    return 0;
  }

  uint16_t header::getInt16(size_t index){
    if (datasize >= (index + 1)){
      return 0 + (data[index] << 8) + data[index + 1];
    }
    return 0;
  }
  
  std::string header::toPrettyString(size_t indent){
    std::stringstream r;
    r << "Testing vorbis Pretty string" << std::endl;
    return r.str();
  }

}
