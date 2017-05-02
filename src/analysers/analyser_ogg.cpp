#include "analyser_ogg.h"
#include <mist/opus.h>

/// \TODO EW EW EW EW EW EW EW EW EW EW EW

void AnalyserOGG::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserOGG::AnalyserOGG(Util::Config &conf) : Analyser(conf){}

bool AnalyserOGG::parsePacket(){
  if (!oggPage.read(stdin)){return false;}

  // We now have an Ogg page
  // Print it, if we're at high detail level.
  DETAIL_HI("%s", oggPage.toPrettyString().c_str());

  // attempt to detect codec if this is the first page of a stream
  if (oggPage.getHeaderType() & OGG::BeginOfStream){
    if (memcmp("theora", oggPage.getSegment(0) + 1, 6) == 0){
      sn2Codec[oggPage.getBitstreamSerialNumber()] = "Theora";
    }
    if (memcmp("vorbis", oggPage.getSegment(0) + 1, 6) == 0){
      sn2Codec[oggPage.getBitstreamSerialNumber()] = "Vorbis";
    }
    if (memcmp("OpusHead", oggPage.getSegment(0), 8) == 0){
      sn2Codec[oggPage.getBitstreamSerialNumber()] = "Opus";
    }
    if (sn2Codec[oggPage.getBitstreamSerialNumber()] != ""){
      INFO_MSG("Bitstream %llu recognized as %s", oggPage.getBitstreamSerialNumber(),
               sn2Codec[oggPage.getBitstreamSerialNumber()]);
    }else{
      WARN_MSG("Bitstream %llu not recognized!", oggPage.getBitstreamSerialNumber());
    }
  }

  if (sn2Codec[oggPage.getBitstreamSerialNumber()] == "Theora"){
    if (detail >= 2){
      std::cout << "  Theora data" << std::endl;
    }
    static unsigned int numParts = 0;
    static unsigned int keyCount = 0;
    for (unsigned int i = 0; i < oggPage.getAllSegments().size(); i++){
      theora::header tmpHeader((char *)oggPage.getSegment(i), oggPage.getAllSegments()[i].size());
      if (tmpHeader.isHeader()){
        if (tmpHeader.getHeaderType() == 0){kfgshift = tmpHeader.getKFGShift();}
      }else{
        if (!(oggPage.getHeaderType() == OGG::Continued) &&
            tmpHeader.getFTYPE() == 0){// if keyframe
          if (detail >= 3){
            std::cout << "keyframe " << keyCount << " has " << numParts << " parts and granule " << (oggPage.getGranulePosition() >> kfgshift) << std::endl;
          }
          numParts = 0;
          keyCount++;
        }
        if (oggPage.getHeaderType() != OGG::Continued || i){numParts++;}
      }
      if (detail >= 2){
        std::cout << tmpHeader.toPrettyString(4);
      }
    }
  }else if (sn2Codec[oggPage.getBitstreamSerialNumber()] == "Vorbis"){
    if (detail >= 2){
      std::cout << "  Vorbis data" << std::endl;
    }
    for (unsigned int i = 0; i < oggPage.getAllSegments().size(); i++){
      int len = oggPage.getAllSegments()[i].size();
      vorbis::header tmpHeader((char *)oggPage.getSegment(i), len);
      if (tmpHeader.isHeader() && detail >= 2){std::cout << tmpHeader.toPrettyString(4);}
    }
  }else if (sn2Codec[oggPage.getBitstreamSerialNumber()] == "Opus"){
    if (detail >= 2){
      std::cout << "  Opus data" << std::endl;
    }
    int offset = 0;
    for (unsigned int i = 0; i < oggPage.getAllSegments().size(); i++){
      int len = oggPage.getAllSegments()[i].size();
      const char *part = oggPage.getSegment(i);
      if (len >= 8 && memcmp(part, "Opus", 4) == 0){
        if (memcmp(part, "OpusHead", 8) == 0 && detail >= 2){
          std::cout << "  Version: " << (int)(part[8]) << std::endl;
          std::cout << "  Channels: " << (int)(part[9]) << std::endl;
          std::cout << "  Pre-skip: " << (int)(part[10] + (part[11] << 8)) << std::endl;
          std::cout << "  Orig. sample rate: "
                    << (int)(part[12] + (part[13] << 8) + (part[14] << 16) + (part[15] << 24))
                    << std::endl;
          std::cout << "  Gain: " << (int)(part[16] + (part[17] << 8)) << std::endl;
          std::cout << "  Channel map: " << (int)(part[18]) << std::endl;
          if (part[18] > 0){
            std::cout << "  Channel map family " << (int)(part[18])
                      << " not implemented - output incomplete" << std::endl;
          }
        }
        if (memcmp(part, "OpusTags", 8) == 0 && detail >= 3){
          unsigned int vendor_len = part[8] + (part[9] << 8) + (part[10] << 16) + (part[11] << 24);
          std::cout << "  Vendor: " << std::string(part + 12, vendor_len) << std::endl;
          const char *str_data = part + 12 + vendor_len;
          unsigned int strings =
              str_data[0] + (str_data[1] << 8) + (str_data[2] << 16) + (str_data[3] << 24);
          std::cout << "  Tags: (" << strings << ")" << std::endl;
          str_data += 4;
          for (unsigned int j = 0; j < strings; j++){
            unsigned int strlen =
                str_data[0] + (str_data[1] << 8) + (str_data[2] << 16) + (str_data[3] << 24);
            str_data += 4;
            std::cout << "    [" << j << "] " << std::string((char *)str_data, strlen) << std::endl;
            str_data += strlen;
          }
        }
      }else{
        if (detail >= 4){
          std::cout << "  " << Opus::Opus_prettyPacket(part, len) << std::endl;
        }
      }
      offset += len;
    }
  }
  return true;
}

