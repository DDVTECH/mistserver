#include <stdint.h>
#include <string>
#include "bitstream.h"

namespace aac {
  class adts {
    public:
      adts();
      adts(char * _data, unsigned long _len);
      adts(const adts & rhs);
      ~adts();
      adts& operator = (const adts & rhs);
      bool sameHeader(const adts & rhs) const;
      unsigned long getAACProfile() const;
      unsigned long getFrequencyIndex() const;
      unsigned long getFrequency() const;
      unsigned long getChannelConfig() const;
      unsigned long getChannelCount() const;
      unsigned long getHeaderSize() const;
      unsigned long getPayloadSize() const;
      unsigned long getCompleteSize() const;
      unsigned long getSampleCount() const;
      bool hasSync() const;
      char * getPayload();
      std::string toPrettyString() const;
      operator bool() const;
    private:
      char * data;
      unsigned long len;
  };

  class AudSpecConf{
    public:
      static inline uint32_t rate(const std::string & conf){
        Utils::bitstream bs;
        bs.append(conf.data(), conf.size());
        if (bs.get(5) == 31){bs.skip(6);}//skip object type
        switch (bs.get(4)){//frequency index
          case 0: return 96000;
          case 1: return 88200;
          case 2: return 64000;
          case 3: return 48000;
          case 4: return 44100;
          case 5: return 32000;
          case 6: return 24000;
          case 7: return 22050;
          case 8: return 16000;
          case 9: return 12000;
          case 10: return 11025;
          case 11: return 8000;
          case 12: return 7350;
          case 15: return bs.get(24);
          default: return 0;
        }
      }
      static inline uint16_t channels(const std::string & conf){
        Utils::bitstream bs;
        bs.append(conf.data(), conf.size());
        if (bs.get(5) == 31){bs.skip(6);}//skip object type
        if (bs.get(4) == 15){bs.skip(24);}//frequency index
        return bs.get(4);//channel configuration
      }
      static inline uint8_t objtype(const std::string & conf){
        Utils::bitstream bs;
        bs.append(conf.data(), conf.size());
        uint8_t ot = bs.get(5);
        if (ot == 31){return bs.get(6)+32;}
        return ot;
      }
      static inline uint16_t samples(const std::string & conf){
        Utils::bitstream bs;
        bs.append(conf.data(), conf.size());
        if (bs.get(5) == 31){bs.skip(6);}//skip object type
        if (bs.get(4) == 15){bs.skip(24);}//frequency index
        bs.skip(4);//channel configuration
        if (bs.get(1)){
          return 960;
        }else{
          return 1024;
        }
      }
  };



}
