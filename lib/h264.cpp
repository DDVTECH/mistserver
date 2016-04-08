#include "h264.h"
#include <cstdlib>
#include <cstring>
#include "bitfields.h"
#include "bitstream.h"
#include "defines.h"

namespace h264 {
  std::deque<nalu::nalData> analysePackets(const char * data, unsigned long len){
    std::deque<nalu::nalData> res;

    int offset = 0;
    while (offset < len){
      nalu::nalData entry;
      entry.nalSize = Bit::btohl(data + offset);
      entry.nalType = (data + offset)[4] & 0x1F;
      res.push_back(entry);
      offset += entry.nalSize + 4;
    }
    return res;
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
      if (end > begin && end[-1] == 0x00){
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

  sequenceParameterSet::sequenceParameterSet(const char * _data, unsigned long _dataLen) : data(_data), dataLen(_dataLen) {}

  //DTSC Initdata is the payload for an avcc box. init[8+] is data, init[6-7] is a network-encoded length
  void sequenceParameterSet::fromDTSCInit(const std::string & dtscInit){
    data = dtscInit.data() + 8;
    dataLen = Bit::btohs(dtscInit.data() + 6);
  }

  SPSMeta sequenceParameterSet::getCharacteristics()  const {
    SPSMeta result;

    //For calculating width
    unsigned int widthInMbs = 0;
    unsigned int cropHorizontal = 0;

    //For calculating height
    bool mbsOnlyFlag = 0;
    unsigned int heightInMapUnits = 0;
    unsigned int cropVertical = 0;

    //Fill the bitstream
    Utils::bitstream bs;
    for (unsigned int i = 1; i < dataLen; i++) {
      if (i + 2 < dataLen && (memcmp(data + i, "\000\000\003", 3) == 0)){//Emulation prevention bytes
        //Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      } else {
        //No we don't increase i here
        bs.append(data + i, 1);
      }
    }

    char profileIdc = bs.get(8);
    //Start skipping unused data
    bs.skip(16);
    bs.getUExpGolomb();
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 || profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128) {
      //chroma format idc
      if (bs.getUExpGolomb() == 3) {
        bs.skip(1);
      }
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.skip(1);
      if (bs.get(1)) {
        DEBUG_MSG(DLVL_DEVEL, "Scaling matrix not implemented yet");
      }
    }
    bs.getUExpGolomb();
    unsigned int pic_order_cnt_type = bs.getUExpGolomb();
    if (!pic_order_cnt_type) {
      bs.getUExpGolomb();
    } else if (pic_order_cnt_type == 1) {
      DEBUG_MSG(DLVL_DEVEL, "This part of the implementation is incomplete(2), to be continued. If this message is shown, contact developers immediately.");
    }
    bs.getUExpGolomb();
    bs.skip(1);
    //Stop skipping data and start doing usefull stuff


    widthInMbs = bs.getUExpGolomb() + 1;
    heightInMapUnits = bs.getUExpGolomb() + 1;

    mbsOnlyFlag = bs.get(1);//Gets used in height calculation
    if (!mbsOnlyFlag) {
      bs.skip(1);
    }
    bs.skip(1);
    //cropping flag
    if (bs.get(1)) {
      cropHorizontal = bs.getUExpGolomb();//leftOffset
      cropHorizontal += bs.getUExpGolomb();//rightOffset
      cropVertical = bs.getUExpGolomb();//topOffset
      cropVertical += bs.getUExpGolomb();//bottomOffset
    }

    //vuiParameters
    if (bs.get(1)) {
      //Skipping all the paramters we dont use
      if (bs.get(1)) {
        if (bs.get(8) == 255) {
          bs.skip(32);
        }
      }
      if (bs.get(1)) {
        bs.skip(1);
      }
      if (bs.get(1)) {
        bs.skip(4);
        if (bs.get(1)) {
          bs.skip(24);
        }
      }
      if (bs.get(1)) {
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }

      //Decode timing info
      if (bs.get(1)) {
        unsigned int unitsInTick = bs.get(32);
        unsigned int timeScale = bs.get(32);
        result.fps = (double)timeScale / (2 * unitsInTick);
        bs.skip(1);
      }
    }

    result.width = (widthInMbs * 16) - (cropHorizontal * 2);
    result.height = ((mbsOnlyFlag ? 1 : 2) * heightInMapUnits * 16) - (cropVertical * 2);
    return result;
  }

}

