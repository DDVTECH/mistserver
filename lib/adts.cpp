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

  adts::adts(const char * _data, unsigned long _len){
    len = _len;
    data = (char*)malloc(len);
    memcpy(data, _data, len);
  }


  bool adts::sameHeader(const adts & rhs) const {
    if (!rhs || !*this){return false;}
    return (getAACProfile() == rhs.getAACProfile() && getFrequencyIndex() == rhs.getFrequencyIndex() && getChannelConfig() == rhs.getChannelConfig());
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

  unsigned long adts::getAACProfile() const{
    if (!data || !len){
      return 0;
    }
    return ((data[2] >> 6) & 0x03) + 1;
  }

  unsigned long adts::getFrequencyIndex() const{
    if (!data || !len){
      return 0;
    }
    return ((data[2] >> 2) & 0x0F);

  }

  unsigned long adts::getFrequency() const{
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

  unsigned long adts::getChannelConfig() const{
    if (!data || !len){
      return 0;
    }
    return ((data[2] & 0x01) << 2) | ((data[3] >> 6) & 0x03);
  }

  unsigned long adts::getChannelCount() const{
    if (!data || !len){
      return 0;
    }
    return (getChannelConfig() == 7 ? 8 : getChannelConfig());
  }

  unsigned long adts::getHeaderSize() const{
    if (!data || !len){
      return 0;
    }
    return (data[1] & 0x01 ? 7 : 9);
  }

  unsigned long adts::getCompleteSize() const{
    if (!data || len < 6){
      return 0;
    }
    return (((data[3] & 0x03) << 11) | (data[4] << 3) | ((data[5] >> 5) & 0x07));
  }

  unsigned long adts::getPayloadSize() const{
    unsigned long ret = getCompleteSize();
    if (!ret){return ret;}//catch zero length
    if (ret >= getHeaderSize()){
      ret -= getHeaderSize();
    }else{
      return 0;//catch size less than header size (corrupt data)
    }
    return ret;
  }

  unsigned long adts::getSampleCount() const{
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
  std::string adts::toPrettyString() const{
    std::stringstream res;
    res << "ADTS packet (payload size: " << getPayloadSize() << ")" << std::endl;
    int syncWord = (((int)data[0] << 4) | ((data[1] >> 4) & 0x0F));
    if (syncWord != 0xfff){
      res << "  Sync word " << std::hex << syncWord << " != fff!" << std::endl;
    }
    if ((data[1] & 0x8) == 0x8){
      res << "  MPEG-2" << std::endl;
    }else{
      res << "  MPEG-4" << std::endl;
    }
    if ((data[1] & 0x6) != 0){
      res << "  Non-zero layer!" << std::endl;
    }
    if ((data[1] & 0x1) == 0x0){
      res << "  CRC present" << std::endl;
    }
    res << "  MPEG-4 audio object type: " << getAACProfile() << std::endl;
    res << "  Frequency: " << getFrequency() << "Hz" << std::endl;
    res << "  Channels: " << getChannelCount() << std::endl;
    res << "  Samples: " << getSampleCount() << std::endl;

    return res.str();
  }
  adts::operator bool() const{
    return hasSync() && len && len >= getHeaderSize() && getFrequency() && getChannelCount() && getSampleCount();
  }
  bool adts::hasSync() const{
    return len && (((int)data[0] << 4) | ((data[1] >> 4) & 0x0F)) == 0xfff;
  }
}
