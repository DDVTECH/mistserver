#include "opus.h"
#include <sstream>

namespace Opus{


  uint16_t getPreSkip(const char * initData){
    return initData[10] + initData[11]* 256;
  }

  unsigned int Opus_getDuration(const char *part){
    const char config = part[0] >> 3;
    const char code = part[0] & 3;
    double dur = 0;
    if (config < 14){
      switch (config % 4){
        case 0: dur = 10; break;
        case 1: dur = 20; break;
        case 2: dur = 40; break;
        case 3: dur = 60; break;
      }
    } else if (config < 16){
      if (config % 2 == 0){
        dur = 10;
      }else{
        dur = 20;
      }
    } else {
      switch (config % 4){
        case 0: dur = 2.5; break;
        case 1: dur = 5; break;
        case 2: dur = 10; break;
        case 3: dur = 20; break;
      }
    }
    if (code == 0){return (unsigned int)dur;}
    if (code < 3){return (unsigned int)(dur*2);}
    return (unsigned int)(dur*(part[1] & 63));
  }

  std::string Opus_prettyPacket(const char *part, int len){
    if (len < 1){return "Invalid packet (0 byte length)";}
    std::stringstream r;
    const char config = part[0] >> 3;
    const char code = part[0] & 3;
    if ((part[0] & 4) == 4){
      r << "Stereo, ";
    }else{
      r << "Mono, ";
    }
    if (config < 14){
      r << "SILK, ";
      if (config < 4){r << "NB, ";}
      if (config < 8 && config > 3){r << "MB, ";}
      if (config < 14 && config > 7){r << "WB, ";}
      if (config % 4 == 0){r << "10ms";}
      if (config % 4 == 1){r << "20ms";}
      if (config % 4 == 2){r << "40ms";}
      if (config % 4 == 3){r << "60ms";}
    }
    if (config < 16 && config > 13){
      r << "Hybrid, ";
      if (config < 14){
        r << "SWB, ";
      }else{
        r << "FB, ";
      }
      if (config % 2 == 0){
        r << "10ms";
      }else{
        r << "20ms";
      }
    }
    if (config > 15){
      r << "CELT, ";
      if (config < 20){r << "NB, ";}
      if (config < 24 && config > 19){r << "WB, ";}
      if (config < 28 && config > 23){r << "SWB, ";}
      if (config > 27){r << "FB, ";}
      if (config % 4 == 0){r << "2.5ms";}
      if (config % 4 == 1){r << "5ms";}
      if (config % 4 == 2){r << "10ms";}
      if (config % 4 == 3){r << "20ms";}
    }
    if (code == 0){
      r << ": 1 packet (" << (len - 1) << "b)";
      return r.str();
    }
    if (code == 1){
      r << ": 2 packets (" << ((len - 1) / 2) << "b / " << ((len - 1) / 2) << "b)";
      return r.str();
    }
    if (code == 2){
      if (len < 2){return "Invalid packet (code 2 must be > 1 byte long)";}
      if (part[1] < 252){
        r << ": 2 packets (" << (int)part[1] << "b / " << (int)(len - 2 - part[1]) << "b)";
      }else{
        int ilen = part[1] + part[2] * 4;
        r << ": 2 packets (" << ilen << "b / " << (int)(len - 3 - ilen) << "b)";
      }
      return r.str();
    }
    // code 3
    bool VBR = (part[1] & 128) == 128;
    bool pad = (part[1] & 64) == 64;
    bool packets = (part[1] & 63);
    r << ": " << packets << " packets (VBR = " << VBR << ", padding = " << pad << ")";
    return r.str();
  }
}

