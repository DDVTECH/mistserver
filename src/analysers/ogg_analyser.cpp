#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <fstream>
#include <string>
#include <map>
#include <string.h>
#include <mist/ogg.h>
#include <mist/config.h>
#include <mist/theora.h>

namespace Analysers{
  std::string Opus_prettyPacket(char * part,int len){
    if (len < 1){
      return "Invalid packet (0 byte length)";
    }
    std::stringstream r;
    char config = part[0] >> 3;
    char code = part[0] & 3;
    if ((part[0] & 4) == 4){r << "Stereo, ";}else{r << "Mono, ";}
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
      if (config < 14){r << "SWB, ";}else{r << "FB, ";}
      if (config % 2 == 0){r << "10ms";}else{r << "20ms";}
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
      r << ": 1 packet (" << (len-1) << "b)";
      return r.str();
    }
    if (code == 1){
      r << ": 2 packets (" << ((len-1)/2) << "b / " << ((len-1)/2) << "b)";
      return r.str();
    }
    if (code == 2){
      if (len < 2){
        return "Invalid packet (code 2 must be > 1 byte long)";
      }
      if (part[1] < 252){
        r << ": 2 packets (" << (int)part[1] << "b / " << (int)(len-2-part[1]) << "b)";
      }else{
        int ilen = part[1] + part[2]*4;
        r << ": 2 packets (" << ilen << "b / " << (int)(len-3-ilen) << "b)";
      }
      return r.str();
    }
    //code 3
    bool VBR = (part[1] & 128) == 128;
    bool pad = (part[1] & 64) == 64;
    bool packets = (part[1] & 63);
    r << ": " << packets << " packets (VBR = " << VBR << ", padding = " << pad << ")";
    return r.str();
  }
  
  int analyseOGG(int argc, char ** argv){
    Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
    conf.addOption("pages", JSON::fromString("{\"long\":\"pages\", \"short\":\"p\", \"long_off\":\"nopages\", \"short_off\":\"P\", \"default\":0, \"help\":\"Enable/disable printing of Ogg pages\"}"));
    conf.parseArgs(argc, argv);
    
    std::map<int,std::string> sn2Codec;
    std::string oggBuffer;
    OGG::Page oggPage;
    //Read all of std::cin to oggBuffer
    //while stream busy
    while (std::cin.good()){
      for (unsigned int i = 0; (i < 1024) && (std::cin.good()); i++){
        oggBuffer += std::cin.get();
      }
      //while OGG::page check function read
      while (oggPage.read(oggBuffer)){//reading ogg to string
        //print the Ogg page details, if requested
        if (conf.getBool("pages")){
          std::cout << oggPage.toPrettyString() << std::endl;
        }
        
        //attempt to detect codec if this is the first page of a stream
        if (oggPage.getHeaderType() & OGG::BeginOfStream){
          if (memcmp("theora",oggPage.getFullPayload() + 1,6) == 0){
            sn2Codec[oggPage.getBitstreamSerialNumber()] = "Theora";
          }
          if (memcmp("vorbis",oggPage.getFullPayload() + 1,6) == 0){
            sn2Codec[oggPage.getBitstreamSerialNumber()] = "Vorbis";
          }
          if (memcmp("OpusHead",oggPage.getFullPayload(),8) == 0){
            sn2Codec[oggPage.getBitstreamSerialNumber()] = "Opus";
          }
          if (sn2Codec[oggPage.getBitstreamSerialNumber()] != ""){
            std::cout << "Bitstream " << oggPage.getBitstreamSerialNumber() << " recognized as " << sn2Codec[oggPage.getBitstreamSerialNumber()] << std::endl;
          }else{
            std::cout << "Bitstream " << oggPage.getBitstreamSerialNumber() << " could not be recognized as any known codec" << std::endl;
          }
          
        }
        
        if (sn2Codec[oggPage.getBitstreamSerialNumber()] == "Theora"){
          std::cout << "Theora data" << std::endl;
          int offset = 0;
          for (unsigned int i = 0; i < oggPage.getSegmentTableDeque().size(); i++){
            theora::header tmpHeader;
            int len = oggPage.getSegmentTableDeque()[i];
            if (tmpHeader.read(oggPage.getFullPayload()+offset,len)){
              std::cout << tmpHeader.toPrettyString(2);
            }
            theora::frame tmpFrame;
            if (tmpFrame.read(oggPage.getFullPayload()+offset,len)){
              std::cout << tmpFrame.toPrettyString(2);
            }
            offset += len;
          }
        }else if(sn2Codec[oggPage.getBitstreamSerialNumber()] == "Vorbis"){
          std::cout << "Vorbis data" << std::endl;
          int offset = 0;
          for (unsigned int i = 0; i < oggPage.getSegmentTableDeque().size(); i++){
            vorbis::header tmpHeader;
            int len = oggPage.getSegmentTableDeque()[i];
            if (tmpHeader.read(oggPage.getFullPayload()+offset,len)){
              std::cout << tmpHeader.toPrettyString(2);
            }
            offset += len;
          }
        }else if(sn2Codec[oggPage.getBitstreamSerialNumber()] == "Opus"){
          std::cout << "Opus data" << std::endl;
          int offset = 0;
          for (unsigned int i = 0; i < oggPage.getSegmentTableDeque().size(); i++){
            int len = oggPage.getSegmentTableDeque()[i];
            char * part = oggPage.getFullPayload() + offset;
            if (len >= 8 && memcmp(part, "Opus", 4) == 0){
              if (memcmp(part, "OpusHead", 8) == 0){
                std::cout << "  Version: " << (int)(part[8]) << std::endl;
                std::cout << "  Channels: " << (int)(part[9]) << std::endl;
                std::cout << "  Pre-skip: " << (int)(part[10] + (part[11] << 8)) << std::endl;
                std::cout << "  Orig. sample rate: " << (int)(part[12] + (part[13] << 8) + (part[14] << 16) + (part[15] << 24)) << std::endl;
                std::cout << "  Gain: " << (int)(part[16] + (part[17] << 8)) << std::endl;
                std::cout << "  Channel map: " << (int)(part[18]) << std::endl;
                if (part[18] > 0){
                  std::cout << "  Channel map family " << (int)(part[18]) << " not implemented - output incomplete" << std::endl;
                }
              }
              if (memcmp(part, "OpusTags", 8) == 0){
                unsigned int vendor_len = part[8] + (part[9]<<8) + (part[10]<<16) + (part[11]<<24);
                std::cout << "  Vendor: " << std::string(part+12, vendor_len) << std::endl;
                char * str_data = part+12+vendor_len;
                unsigned int strings = str_data[0] + (str_data[1]<<8) + (str_data[2]<<16) + (str_data[3]<<24);
                std::cout << "  Tags: (" << strings << ")" << std::endl;
                str_data += 4;
                for (unsigned int j = 0; j < strings; j++){
                  unsigned int strlen = str_data[0] + (str_data[1]<<8) + (str_data[2]<<16) + (str_data[3]<<24);
                  str_data += 4;
                  std::cout << "    [" << j << "] " << std::string(str_data, strlen) << std::endl;
                  str_data += strlen;
                }
              }
            }else{
              std::cout << "  " << Opus_prettyPacket(part,len) << std::endl;
            }
            offset += len;
          }
        }
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  return Analysers::analyseOGG(argc, argv);
}

