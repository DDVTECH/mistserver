#include "vorbis.h"
#include <stdlib.h>
#include <string.h>
#include <sstream>
#include <arpa/inet.h>
#include "bitstream.h"
#include <deque>

#include <cstdio>
#include <iostream>

namespace vorbis{
  long long unsigned int reverseByte16(long long unsigned int input){
    return ((input & 0xFF00) >> 8) | ((input & 0xFF) << 8);
  }

  long long unsigned int reverseByte24(long long unsigned int input){
    return ((input & 0xFF0000) >> 16)| (input & 0xFF00) | ((input & 0xFF) << 16);
  }

  long long unsigned int reverseByte32(long long unsigned int input){
    return ((input & 0xFF000000) >> 24)| ((input & 0xFF0000) >> 8) | ((input & 0xFF00) << 8) | ((input & 0xFF) << 24);
  }


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
      return data[28] & 0x0F;
    }else{
      return 0;
    }
  }

  char header::getBlockSize1(){
    if (getHeaderType() == 1){
      return (data[28]>>4) & 0x0F;
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
        if (datasize!=30){
          return false;
        }
        if (getVorbisVersion()!=0){
          return false;
        }
        if (getAudioChannels()<=0){
          return false;
        };
        if (getAudioSampleRate()<=0) {
          return false;
        }
        if (getBlockSize0()>getBlockSize1()){
          return false;
        };
        if (getFramingFlag()!=1){
          return false;
        };
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
    if(memcmp(newData+1, "vorbis", 6)!=0){
      return false;
    }

    if (checkDataSize(length)){
      memcpy(data, newData, length);
    }else{
      return false;
    }
    return true;
  }
  
  std::deque<mode> header::readModeDeque(char audioChannels){
    Utils::bitstreamLSBF stream;
    stream.append(data,datasize);
    long long unsigned int beginsize = stream.size();
    stream.skip(28); //skipping common header part
    stream.skip(28); //skipping common header part
    char codebook_count = stream.get(8) + 1;
    //std::cerr << "Codebook Count: " << (int)codebook_count << std::endl;
    for (int i = 0; i < codebook_count; i++){
      //std::cerr << "codebook entry: " << i << std::endl;
      long long unsigned int CMN = stream.get(24);
      //std::cerr << "  Codebook magic number: " << std::hex << CMN << std::dec << std::endl;
      if (CMN != 0x564342){
        exit(1);
      }
      unsigned short codebook_dimensions = stream.get(16);
      unsigned int codebook_entries = stream.get(24);
      //std::cerr << "  Codebook_dimensions, entries: "<< std::hex << codebook_dimensions << ", " <<codebook_entries << std::dec<< std::endl;
      bool orderedFlag = stream.get(1);
      //std::cerr << "  Ordered Flag: " << orderedFlag << std::endl;
      if (!orderedFlag){
        bool sparseFlag = stream.get(1);
        //std::cerr << "  Sparse Flag: " << sparseFlag << std::endl;
        if (sparseFlag){//sparse flag
          //sparse handling
          for (int o = 0; o < codebook_entries; o++){
            if (stream.get(1)){
              //long long unsigned int length = stream.get(5)+1;
              //std::cerr << "  codeword length: "<< length  <<std::endl;
              stream.skip(5);
            }
          }
          //exit(1);
        }else{
          for (int o = 0; o < codebook_entries; o++){
            //long long unsigned int length = stream.get(5)+1;
            //std::cerr << "  codeword length: "<< length  <<std::endl;
            stream.skip(5);
          }
        }
      }else{
        //ordered handling
        //std::cerr << "WARNING: Ordered codebook decoding in vorbis setup header NOT tested!" << std::endl;
        long long unsigned int length = stream.get(5) + 1;
        for (int o = 0; o < codebook_entries; o++){
          int readnow = (std::log(codebook_entries-o))/(std::log(2))+1;
          //std::cerr << "  Read amount of bits: " << readnow << ";" << codebook_entries-o << std::endl;
          o+=stream.get(readnow);
          
        }
        //exit(2);
      }
      char codebook_lookup_type = stream.get(4);
      //std::cerr << "  codebook_lookup_type: " <<std::hex<< (int)codebook_lookup_type <<std::dec << std::endl;
      if (codebook_lookup_type != 0){
        stream.skip(32);
        stream.skip(32);
        char codebook_value_bits = stream.get(4) + 1;
        //std::cerr << "  codebook_value_bits: " << (int)codebook_value_bits << std::endl;
        bool codebook_sequence_flag = stream.get(1);
        unsigned int codebook_lookup_value;
        if (codebook_lookup_type == 1){
          codebook_lookup_value = std::pow(codebook_entries, (1.0/codebook_dimensions));
          //std::cerr << "  std::pow(" << codebook_entries << ", (1/" << codebook_dimensions << ")) =";
        }else{
          codebook_lookup_value = codebook_entries * codebook_dimensions;
          //std::cerr << "  " << codebook_entries << " * " << codebook_dimensions << " = ";
        }
        //std::cerr << " codebook_lookup_value: " << codebook_lookup_value << std::endl << "  ";
        for (int i = 0; i < codebook_lookup_value; i++){
          //(int)stream.get(codebook_value_bits) << " ";
          stream.skip(codebook_value_bits);
        }
        //std::cerr << std::endl;
      }
    }
    //end of codebooks
    //std::cerr << "bits in to time domain transforms: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;
    //time domain transforms
    long long unsigned int TDT = stream.get(6) + 1;
    //std::cerr << "Time domain Transforms: " << TDT << std::endl;
    for (int i = 0; i < TDT; i++){
      stream.skip(16);
    }
    //std::cerr << "bits in to floors: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;
    //Floors
    long long unsigned int floors = stream.get(6) + 1;
    //std::cerr << "Floors: " << floors << std::endl;
    for (int i = 0; i < floors; i++){
      long long unsigned int floorType = stream.get(16);
      //std::cerr << "FloorType: " << floorType << std::endl;
      switch(floorType){
        case 0:{
          std::cerr << "WARNING: FloorType 0 in vorbis setup header not tested!" << std::endl;
          stream.skip(8);//order
          stream.skip(16);//rate
          stream.skip(16);//bark_map_size
          stream.skip(6);//amplitude bits
          stream.skip(8);//amplitude offset
          long long unsigned int numberOfBooks = stream.get(4)+1;
          for (int o = 0; o < numberOfBooks; o++){
            stream.skip(8);//book list array
          }
        break;
        }
        case 1:{
          long long unsigned int floorPartitions = stream.get(5);
          long long int max = -1;
          //std::cerr << "  floorPartitions: " << floorPartitions << std::endl;
          std::deque<int> partition_class;
          for (int o = 0; o < floorPartitions; o++){
            long long int temp = stream.get(4);
            partition_class.push_back(temp);
            //std::cerr << "    partition_class: " << temp << std::endl;
            if (temp>max) max = temp;
          }
          std::deque<int> class_dimensions;
          //std::cerr << "  Max: " << max << std::endl;
          for (int o = 0; o <= max; o++){
            class_dimensions.push_back(stream.get(3)+1);//class dimensions PUT IN ARRAY!
            //std::cerr << "    class dimension: " << class_dimensions.back() << std::endl;
            int class_subclass = stream.get(2);
            if (class_subclass !=0){
              stream.skip(8);//class_master_books
            }
            for (int p = 0; p < (1<<class_subclass); p++){
              stream.skip(8);
            }
          }
          stream.skip(2);//floor1_multiplier
          int rangebits = stream.get(4);//rangebits
          //std::cerr << "  rangebits: " << rangebits << std::endl;
          long long unsigned int count = 0;
          long long unsigned int skipper = 0;
          for (int o = 0; o < floorPartitions; o++){
            count += class_dimensions[(partition_class[o])];
            //std::cerr << "    count: " << count << std::endl << "    ";
            while (skipper < count){
              stream.skip(rangebits);
              skipper ++;
            }
            //std::cerr << std::endl;
          } 
        break;
        }
        default:
        exit(0);
      }
    }
    //std::cerr << "bits in to Residues: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;

    //Residues
    long long unsigned int residues = stream.get(6) + 1;
    //std::cerr << "Residue count: " << residues << std::endl;
    for(int i = 0; i < residues; i++){
      std::deque<char> residueCascade;
      long long unsigned int residueType = stream.get(16);
      //std::cerr << "ResidueType: " << residueType << std::endl;
      if(residueType<=2){
        stream.skip(24);//residue begin
        stream.skip(24);//residue end
        stream.skip(24);//residue partition size
        long long unsigned int residueClass = stream.get(6)+1;//residue classifications
        //std::cerr<< "  ResidueCLassification: " << residueClass << std::endl;
        stream.skip(8);//residue classbook
        for (int o = 0; o < residueClass; o++){
          char temp = stream.get(3);//low bits
          bool bitFlag = stream.get(1);
          //std::cerr << "    bitFlag: " << bitFlag << std::endl;
          if (bitFlag){
            temp += stream.get(5) << 3;
          }
          //std::cerr << "    temp: " << (int)temp << std::endl;
          residueCascade.push_back(temp);
        }
        for (int o = 0; o < residueClass; o++){
          //std::cerr << "    ";
          for (int p = 0; p < 7; p++){
            if (((residueCascade[o] >> p) & 1) == 1){
              //std::cerr << "1";
              stream.skip(8);
            }else{
              //std::cerr << "0";
            }
          }
          //std::cerr << std::endl;
        }
      }else{
        exit(0);
      }
    }
    //std::cerr << "bits in to Mappings: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;
    //Mappings
    long long unsigned int mappings = stream.get(6) + 1;
    //std::cerr << "Mapping count: " << mappings << std::endl;
    for(int i = 0; i < mappings; i++){
      long long unsigned int mapType = stream.get(16);
      //std::cerr << "MapType: " << mapType << std::endl;
      if (mapType == 0){
        char mappingSubmaps = 1;
        if (stream.get(1)==1){
          mappingSubmaps = stream.get(4);//vorbis mapping submaps
        }
        long long unsigned int coupling_steps = 0;
        if (stream.get(1)==1){
          coupling_steps = stream.get(8)+1;
          //std::cerr << "  coupling steps: " << coupling_steps << std::endl;
          //std::cerr << "  AudioChannels: " << (int)audioChannels << std::endl;
          for (int o = 0; o<coupling_steps; o++){
            int temp = (std::log((audioChannels-o)-1))/(std::log(2)) + 1;
            //std::cerr << "  ilogAudioChannels: " << temp << std::endl;
            if (temp>0){
              stream.skip(temp);//mapping magnitude
              stream.skip(temp);//mapping angle
            }
          }
        }
        char meh = stream.get(2);
        if (meh != 0){
          std::cerr << "  Sanity Check ==0 : " << (int)meh << std::endl;
          exit(0);
        }
        //std::cerr << "  Mapping Submaps: " << mappingSubmaps << std::endl;
        if (mappingSubmaps > 1){
          for (int o = 0; o < audioChannels; o++){
            stream.skip(4);
          }
        }
        for (int o = 0; o < mappingSubmaps; o++){
          stream.skip(8);//placeholder
          stream.skip(8);//vorbis Mapping subfloor
          stream.skip(8);//vorbis mapping submap residue
        }
        
      }else{
        exit(0);
      }
    }
    //Modes
    //std::cerr << "bits in to Modes: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;
    long long unsigned int modes = stream.get(6) + 1;
    //std::cerr << "Mode count: " << modes << std::endl;
    std::deque<mode> retVal;
    for (int i = 0; i < modes; i++){
      mode temp;
      temp.blockFlag = stream.get(1);
      //std::cerr << "  blockFlag: " << temp.blockFlag << std::endl;
      temp.windowType = stream.get(16);
      //std::cerr << "  windowType: " << temp.windowType << std::endl;
      temp.transformType = stream.get(16);
      //std::cerr << "  transformType: " << temp.transformType << std::endl;
      temp.mapping = stream.get(8);
      //std::cerr << "  mapping: " << (int)temp.mapping << std::endl;
      retVal.push_back(temp);
    }
    //std::cerr << "Ending Bitflag (!=0): " << stream.get(1) << std::endl;
    stream.skip(1);
    //std::cerr << "bits left: " << (beginsize-stream.size()) << ", " << stream.size() << std::endl;
    return retVal;
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
    r << std::string(indent+1,' ') << "Vorbis Header" << std::endl;
    r << std::string(indent+2,' ') << "Magic Number: " << std::string(data + 1,6) << std::endl;
    r << std::string(indent+2,' ') << "Header Type: " << getHeaderType() << std::endl;
    if (getHeaderType() == 1){
      r << std::string(indent+2,' ') << "ID Header" << std::endl;
      r << std::string(indent+2,' ') << "VorbisVersion: " << getVorbisVersion() << std::endl;
      r << std::string(indent+2,' ') << "AudioChannels: " << (int)getAudioChannels() << std::endl;
      r << std::string(indent+2,' ') << "BitrateMaximum: " << std::hex << getBitrateMaximum() << std::dec << std::endl;
      r << std::string(indent+2,' ') << "BitrateNominal: " << std::hex << getBitrateNominal() << std::dec << std::endl;
      r << std::string(indent+2,' ') << "BitrateMinimum: " << std::hex << getBitrateMinimum() << std::dec << std::endl;
      r << std::string(indent+2,' ') << "BlockSize0: " << (int)getBlockSize0() << std::endl;
      r << std::string(indent+2,' ') << "BlockSize1: " << (int)getBlockSize1() << std::endl;
      r << std::string(indent+2,' ') << "FramingFlag: " << (int)getFramingFlag() << std::endl;
    } else if (getHeaderType() == 3){
      r << std::string(indent+2,' ') << "Comment Header" << std::endl;
    } else if (getHeaderType() == 5){
      r << std::string(indent+2,' ') << "Setup Header" << std::endl;
    }
    return r.str();
  }
}
