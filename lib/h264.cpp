#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
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
    //Make sure entire packet is within len
    while (offset+5 < len && Bit::btohl(data + offset)+offset+4 <= len){
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

  void skipScalingList(Utils::bitstream & bs, size_t listSize){
    size_t lastScale = 8;
    size_t nextScale = 8;
    for (size_t i = 0; i < listSize; i++){
      if (nextScale){
        uint64_t deltaScale = bs.getExpGolomb();
        nextScale = (lastScale + deltaScale + 256) % 256;
      }
      lastScale = (nextScale ? nextScale : lastScale);
    }
  }

  SPSMeta sequenceParameterSet::getCharacteristics()  const {
    SPSMeta result;
    result.sep_col_plane = false;

    //For calculating width
    unsigned int widthInMbs = 0;
    unsigned int cropHorizontal = 0;

    //For calculating height
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
    result.profile = profileIdc;
    //Start skipping unused data
    bs.skip(8);
    result.level = bs.get(8);
    bs.getUExpGolomb();
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 || profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128) {
      //chroma format idc
      char chromaFormatIdc = bs.getUExpGolomb();
      if (chromaFormatIdc == 3) {
        result.sep_col_plane = (bs.get(1) == 1);
      }
      bs.getUExpGolomb();//luma
      bs.getUExpGolomb();//chroma
      bs.skip(1);//transform bypass
      if (bs.get(1)) {//Scaling matrix is present
        char listSize = (chromaFormatIdc == 3 ? 12 : 8);
        for (size_t i = 0; i < listSize; i++){
          bool thisListPresent = bs.get(1);
          if (thisListPresent){
            if (i < 6){
              skipScalingList(bs, 16);
            }else{
              skipScalingList(bs, 64);
            }
          }
        }
      }
    }
    result.log2_max_frame_num = bs.getUExpGolomb() + 4;
    result.cnt_type = bs.getUExpGolomb();
    if (!result.cnt_type) {
      result.log2_max_order_cnt = bs.getUExpGolomb() + 4;
    } else if (result.cnt_type == 1) {
      DEBUG_MSG(DLVL_DEVEL, "This part of the implementation is incomplete(2), to be continued. If this message is shown, contact developers immediately.");
    }
    result.max_ref_frames = bs.getUExpGolomb();//max_num_ref_frames
    result.gaps = (bs.get(1) == 1);//gaps in frame num allowed
    //Stop skipping data and start doing useful stuff


    widthInMbs = bs.getUExpGolomb() + 1;
    heightInMapUnits = bs.getUExpGolomb() + 1;

    result.mbs_only = (bs.get(1) == 1);//Gets used in height calculation
    if (!result.mbs_only) {
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
    result.height = ((result.mbs_only ? 1 : 2) * heightInMapUnits * 16) - (cropVertical * 2);
    return result;
  }

}

