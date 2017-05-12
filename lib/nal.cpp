#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <cstdlib>
#include <cstring>
#include <math.h>//for log

#include "nal.h"
#include "bitstream.h"
#include "bitfields.h"
#include "defines.h"

namespace nalu {
  std::deque<int> parseNalSizes(DTSC::Packet & pack){
    std::deque<int> result;
    char * data;
    unsigned int dataLen;
    pack.getString("data", data, dataLen);
    int offset = 0;
    while (offset < dataLen){
      int nalSize = Bit::btohl(data + offset);
      result.push_back(nalSize + 4);
      offset += nalSize + 4;
    }
    return result;
  }

  std::string removeEmulationPrevention(const std::string & data) {
    std::string result;
    result.resize(data.size());
    result[0] = data[0];
    result[1] = data[1];
    unsigned int dataPtr = 2;
    unsigned int dataLen = data.size();
    unsigned int resPtr = 2;
    while (dataPtr + 2 < dataLen) {
      if (!data[dataPtr] && !data[dataPtr + 1] && data[dataPtr + 2] == 3){ //We have found an emulation prevention
        result[resPtr++] = data[dataPtr++];
        result[resPtr++] = data[dataPtr++];
        dataPtr++; //Skip the emulation prevention byte
      } else {
        result[resPtr++] = data[dataPtr++];
      }
    }

    while (dataPtr < dataLen){
      result[resPtr++] = data[dataPtr++];
    }
    return result.substr(0, resPtr);
  }

  unsigned long toAnnexB(const char * data, unsigned long dataSize, char *& result){
    //toAnnexB keeps the same size.
    if (!result){
      result = (char *)malloc(dataSize);
    }
    int offset = 0;
    while (offset < dataSize){
      //Read unit size
      unsigned long unitSize = Bit::btohl(data + offset);
      //Write annex b header
      memset(result + offset, 0x00, 3);
      result[offset + 3] = 0x01;
      //Copy the nal unit
      memcpy(result + offset + 4, data + offset + 4, unitSize);
      //Update the offset
      offset += 4 + unitSize;
    }
    return dataSize;
  }

  const char* nalEndPosition(const char * data, uint32_t dataSize){
    while(dataSize > 0 && memcmp(data+dataSize-1, "\000",1) == 0 ){
      dataSize--;
    }
    
    return data+dataSize;
  }

  ///scan data stream for startcode. return pointer to location when found, NULL otherwise
  void scanAnnexB(const char * data, uint32_t dataSize, const char *& packetPointer){
    int offset = 0;

    while(offset+2 < dataSize){
      const char * begin = data + offset;
//      int t = ((((int*)begin)[0]) >> 8) & 0x00FFFFFF;
      int t = (int)((begin[0] << 8)|((begin[1]) << 8)|(begin[2])); 
      //int t = (int)((begin[0]|begin[1]) << 1)|(begin[2]); 
      //search for startcode

      //if(memcmp(begin, "\000\000\001",3) != 0){
      if(t != 1){

        //if((t & 0x0000FF != 0 )) 
        if((int)begin[2] != 0 )                                   //XX1
        {
          offset +=3;
        }else if(((int)begin[1] == 1) && ((int)begin[2] ==0)){    //X10 
          offset +=2; 
        }else{
          offset++;   //[X00]? incr with 1 because the startcode could be one at 1byte offset.
        }
/*
        if(t != 0 )
        {
          offset += 3;
        }else{
          offset++;
        }
*/

//        offset++;
      }else{
        packetPointer = begin;
        return;
      }
    }

    packetPointer = NULL;
  }

  unsigned long fromAnnexB(const char * data, unsigned long dataSize, char *& result){
    const char * lastCheck = data + dataSize - 3;
    if (!result){
      FAIL_MSG("No output buffer given to FromAnnexB");
      return 0;
    }
    int offset = 0;
    int newOffset = 0;
    while (offset < dataSize){
      const char * begin = data + offset;
      while ( begin < lastCheck && !(!begin[0] && !begin[1] && begin[2] == 0x01)){
        begin++;
        if (begin < lastCheck && begin[0]){
          begin++;
        }
      }
      begin += 3;//Initialize begin after the first 0x000001 pattern.
      if (begin > data + dataSize){
        offset = dataSize;
        continue;
      }
      const char * end = (const char*)memmem(begin, dataSize - (begin - data), "\000\000\001", 3);
      if (!end) {
        end = data + dataSize;
      }
      //Check for 4-byte lead in's. Yes, we access -1 here
      if (end > begin && (end - data) != dataSize && end[-1] == 0x00){
        end--;
      }
      unsigned int nalSize = end - begin;
      Bit::htobl(result + newOffset, nalSize);
      memcpy(result + newOffset + 4, begin, nalSize);

      newOffset += 4 + nalSize;
      offset = end - data;
    }
    return newOffset;
  }
}
