#include "flac.h"

/// Checks the first 4 bytes for the string "flaC". Implementing a basic FLAC header check,
/// returning true if it is, false if not.
bool FLAC::is_header(const char *header){
  if (header[0] != 'f') return false;
  if (header[1] != 'L') return false;
  if (header[2] != 'a') return false;
  if (header[3] != 'C') return false;
  return true;
}// FLAC::is_header

size_t FLAC::utfBytes(char p){
  if ((p & 0x80) == 0x00){return 1;}
  if ((p & 0xE0) == 0xC0){return 2;}
  if ((p & 0xF0) == 0xE0){return 3;}
  if ((p & 0xF8) == 0xF0){return 4;}
  if ((p & 0xFC) == 0xF8){return 5;}
  if ((p & 0xFE) == 0xFC){return 6;}
  if ((p & 0xFF) == 0xFE){return 7;}
  return 9;
}

uint32_t FLAC::utfVal(char *p){
  size_t bytes = utfBytes(*p);
  uint32_t ret = 0;

  if (bytes == 1){
    ret = (uint32_t)*p;
  }else if (bytes == 2){
    ret = (uint32_t)(*p & 0x1F) << 6;
    ret = ret | (*(p + 1) & 0x3f);
  }else if (bytes == 3){
    ret = (uint32_t)(*p & 0x1F) << 6;
    ret = (ret | (*(p + 1) & 0x3f)) << 6;
    ret = ret | (*(p + 2) & 0x3f);
  }else if (bytes == 4){
    ret = (uint32_t)(*p & 0x1F) << 6;
    ret = (ret | (*(p + 1) & 0x3f)) << 6;
    ret = (ret | (*(p + 2) & 0x3f)) << 6;
    ret = ret | (*(p + 3) & 0x3f);
  }

  return ret;
}

FLAC::Frame::Frame(char *pkt){
  data = pkt;
  if (data[0] != 0xFF || (data[1] & 0xFC) != 0xF8){
    WARN_MSG("Sync code incorrect! Ignoring FLAC frame");
    FAIL_MSG("%x %x", data[0], data[1]);
    data = 0;
  }
}

uint16_t FLAC::Frame::samples(){
  if (!data){return 0;}
  switch ((data[2] & 0xF0) >> 4){
  case 0: return 0; // reserved
  case 1: return 192;
  case 2: return 576;
  case 3: return 1152;
  case 4: return 2304;
  case 5: return 4608;
  case 6: return 1; // 1b at end
  case 7: return 2; // 2b at end
  default: return 256 << (((data[2] & 0xf0) >> 4) - 8);
  }
}
uint32_t FLAC::Frame::rate(){
  if (!data){return 0;}
  switch (data[2] & 0x0F){
  case 0: return 0; // get from STREAMINFO
  case 1: return 88200;
  case 2: return 176400;
  case 3: return 192000;
  case 4: return 8000;
  case 5: return 16000;
  case 6: return 22050;
  case 7: return 24000;
  case 8: return 32000;
  case 9: return 44100;
  case 10: return 48000;
  case 11: return 96000;
  case 12: return 1; // 1b at end, *1000
  case 13: return 2; // 2b at end
  case 14: return 3; // 2b at end, *10
  case 15: return 0; // invalid, get from STREAMINFO
  default: return 0;
  }
}

uint8_t FLAC::Frame::channels(){
  if (!data){return 0;}
  uint8_t ret = ((data[3] & 0xF0) >> 4) + 1;
  if (ret > 8 && ret < 12){return 2;}// special stereo
  return ret;
}
uint8_t FLAC::Frame::size(){
  if (!data){return 0;}
  switch (data[3] & 0x0E){
  case 0: return 0; // get from STREAMINFO
  case 1: return 8;
  case 2: return 12;
  case 3: return 0; // reserved
  case 4: return 16;
  case 5: return 20;
  case 6: return 24;
  case 7: return 0; // reserved
  default: return 0;
  }
}

uint32_t FLAC::Frame::utfVal(){
  return FLAC::utfVal(data + 4);
}

std::string FLAC::Frame::toPrettyString(){
  if (!data){return "Invalid frame";}
  std::stringstream r;
  r << "FLAC frame" << std::endl;
  r << "  Block size: " << ((data[1] & 0x1) ? "variable" : "fixed") << std::endl;
  r << "  Samples: " << samples() << std::endl;
  r << "  Rate: " << rate() << "Hz" << std::endl;
  r << "  Channels: " << (int)channels() << std::endl;
  r << "  Size: " << (int)size() << "-bit" << std::endl;
  return r.str();
}
