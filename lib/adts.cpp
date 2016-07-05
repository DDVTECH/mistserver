#include "adts.h"
#include <cstdlib>
#include <cstring>

#include <sstream>

#include "defines.h"

namespace aac {
  adts::adts(){
    data = NULL;
    len = 0;
  }

  adts::adts(char * _data, unsigned long _len){
    len = _len;
    data = (char*)malloc(len);
    memcpy(data, _data, len);
  }


  bool adts::sameHeader(const adts & rhs) const {
    if (len < 7 || rhs.len < 7){return false;}
    return (memcmp(data, rhs.data, 7) == 0);
  }

  adts::adts(const adts & rhs){
    data = NULL;
    len = 0;
    *this = rhs;
  }

  adts& adts::operator = (const adts & rhs){
    if (data){
      free(data);
    }
    len = rhs.len;
    data = (char*)malloc(len);
    memcpy(data, rhs.data, len);
    return * this;
  }

  adts::~adts(){
    if (data){
      free(data);
    }
  }

  unsigned long adts::getAACProfile(){
    if (!data || !len){
      return 0;
    }
    return ((data[2] >> 6) & 0x03) + 1;
  }

  unsigned long adts::getFrequencyIndex(){
    if (!data || !len){
      return 0;
    }
    return ((data[2] >> 2) & 0x0F);

  }

  unsigned long adts::getFrequency(){
    if (!data || len < 3){
      return 0;
    }
    switch(getFrequencyIndex()){
      case 0:  return 96000; break;
      case 1:  return 88200; break;
      case 2:  return 64000; break;
      case 3:  return 48000; break;
      case 4:  return 44100; break;
      case 5:  return 32000; break;
      case 6:  return 24000; break;
      case 7:  return 22050; break;
      case 8:  return 16000; break;
      case 9:  return 12000; break;
      case 10: return 11025; break;
      case 11: return 8000; break;
      case 12: return 7350; break;
      default: return 0; break;
    }
  }

  unsigned long adts::getChannelConfig(){
    if (!data || !len){
      return 0;
    }
    return ((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03);
  }

  unsigned long adts::getChannelCount(){
    if (!data || !len){
      return 0;
    }
    return (getChannelConfig() == 7 ? 8 : getChannelConfig());
  }

  unsigned long adts::getHeaderSize(){
    if (!data || !len){
      return 0;
    }
    return (data[1] & 0x01 ? 7 : 9);
  }

  unsigned long adts::getPayloadSize(){
    if (!data || len < 6){
      return 0;
    }
    unsigned long ret = (((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] >> 5) & 0x07));
    if (!ret){return ret;}//catch zero length
    if (ret >= getHeaderSize()){
      ret -= getHeaderSize();
    }else{
      return 0;//catch size less than header size (corrupt data)
    }
    if (len < ret + getHeaderSize() ){
      ret = len - getHeaderSize();
      //catch size less than length (corrupt data)
    }
    return ret;
  }

  unsigned long adts::getSampleCount(){
    if (!data || len < 7){
      return 0;
    }
    return ((data[6] & 0x03) + 1) * 1024;//Number of samples in this frame * 1024
  }
  
  char * adts::getPayload() {
    if (!data || !len){
      return 0;
    }
    return data + getHeaderSize();
  }
  std::string adts::toPrettyString(){
    std::stringstream res;
    res << "SyncWord: " << std::hex << (((int)data[0] << 4) | ((data[1] >> 4) & 0x0F)) << std::endl;
    res << "HeaderSize: " << std::dec << getHeaderSize() << std::endl;
    res << "PayloadSize: " << std::dec << getPayloadSize() << std::endl;
    return res.str();
  }
}
