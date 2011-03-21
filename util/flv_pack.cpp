#pragma once

class FLV_Pack {
  public:
    int len;
    int buf;
    bool isKeyframe;
    char * data;
    std::string tagType(){
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
    };//tagtype
    unsigned int tagTime(){
      return (data[4] << 16) + (data[5] << 8) + data[6] + (data[7] << 24);
    }//tagTime getter
    void tagTime(unsigned int T){
      data[4] = ((T >> 16) & 0xFF);
      data[5] = ((T >> 8) & 0xFF);
      data[6] = (T & 0xFF);
      data[7] = ((T >> 24) & 0xFF);
    }//tagTime setter
    FLV_Pack(){
      len = 0; buf = 0; data = 0; isKeyframe = false;
    }//empty constructor
    FLV_Pack(const FLV_Pack& O){
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
    FLV_Pack & operator= (const FLV_Pack& O){
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
};//FLV_Pack
