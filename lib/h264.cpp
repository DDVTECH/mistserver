#include "h264.h"
#include <cstdlib>
#include <cstring>
#include "bitfields.h"
#include "defines.h"

namespace h264 {
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

  unsigned long fromAnnexB(const char * data, unsigned long dataSize, char *& result){
    if (!result){
      //first compute the new size. This might be the same as the annex b version, but this is not guaranteed
      int offset = 0;
      int newSize = 0;
      while (offset < dataSize){
        const char * begin = (const char*)memmem(data + offset, dataSize - offset, "\000\000\001", 3);
        begin += 3;//Initialize begin after the first 0x000001 pattern.
        const char * end = (const char*)memmem(begin, dataSize - (begin - data), "\000\000\001", 3);
        if (end - data > dataSize){
          end = data + dataSize;
        }
        //Check for 4-byte lead in's. Yes, we access -1 here
        if (end[-1] == 0x00){
          end--;
        }
        newSize += 4 + (end - begin);//end - begin = nalSize
        offset = end - data;
      }
      result = (char *)malloc(newSize);
    }
    int offset = 0;
    int newOffset = 0;
    while (offset < dataSize){
      const char * begin = ((const char*)memmem(data + offset, dataSize - offset, "\000\000\001", 3)) + 3;//Initialize begin after the first 0x000001 pattern.
      const char * end = (const char*)memmem(begin, dataSize - (begin - data), "\000\000\001", 3);
      if (end - data > dataSize){
        end = data + dataSize;
      }
      //Check for 4-byte lead in's. Yes, we access -1 here
      if (end[-1] == 0x00){
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

