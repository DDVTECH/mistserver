#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "h264.h"
#include "bitfields.h"
#include "bitstream.h"
#include "defines.h"
#include <cmath>
#include <cstring>
#include <iomanip>

namespace h264{

  /// Helper function to determine if a H264 NAL unit is a keyframe or not
  bool isKeyframe(const char *data, uint32_t len){
    uint8_t nalType = (data[0] & 0x1F);
    if (nalType == 0x05){return true;}
    if (nalType != 0x01){return false;}
    Utils::bitstream bs;
    for (size_t i = 1; i < 10 && i < len; ++i){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        bs.append(data + i, 2);
        i += 2;
      }else{
        bs.append(data + i, 1);
      }
    }
    bs.getExpGolomb(); // Discard first_mb_in_slice
    uint64_t sliceType = bs.getUExpGolomb();
    // Slice types:
    //  0: P - Predictive slice (at most 1 reference)
    //  1: B - Bi-predictive slice (at most 2 references)
    //  2: I - Intra slice (no external references)
    //  3: SP - Switching predictive slice (at most 1 reference)
    //  4: SI - Switching intra slice (no external references)
    //  5-9: 0-4, but all in picture of same type
    if (sliceType == 2 || sliceType == 4 || sliceType == 7 || sliceType == 9){return true;}
    return false;
  }

  std::deque<nalu::nalData> analysePackets(const char *data, unsigned long len){
    std::deque<nalu::nalData> res;

    int offset = 0;
    // Make sure entire packet is within len
    while (offset + 5 < len && Bit::btohl(data + offset) + offset + 4 <= len){
      nalu::nalData entry;
      entry.nalSize = Bit::btohl(data + offset);
      entry.nalType = (data + offset)[4] & 0x1F;
      res.push_back(entry);
      offset += entry.nalSize + 4;
    }
    return res;
  }

  sequenceParameterSet::sequenceParameterSet(const char *_data, size_t _dataLen)
      : data(_data), dataLen(_dataLen){}

  // DTSC Initdata is the payload for an avcc box. init[8+] is data, init[6-7] is a network-encoded
  // length
  void sequenceParameterSet::fromDTSCInit(const std::string &dtscInit){
    data = dtscInit.data() + 8;
    dataLen = Bit::btohs(dtscInit.data() + 6);
  }

  void skipScalingList(Utils::bitstream &bs, size_t listSize){
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

  SPSMeta sequenceParameterSet::getCharacteristics() const{
    SPSMeta result;
    result.sep_col_plane = false;

    // For calculating width
    uint32_t widthInMbs = 0;
    uint32_t cropHorizontal = 0;

    // For calculating height
    uint32_t heightInMapUnits = 0;
    uint32_t cropVertical = 0;

    uint32_t sar_width = 0;
    uint32_t sar_height = 0;

    // Fill the bitstream
    Utils::bitstream bs;
    for (size_t i = 1; i < dataLen; i++){
      if (i + 2 < dataLen &&
          (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }

    char profileIdc = bs.get(8);
    result.profile = profileIdc;
    // Start skipping unused data
    bs.skip(8);
    result.level = bs.get(8);
    bs.getUExpGolomb();
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 ||
        profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 ||
        profileIdc == 128){
      // chroma format idc
      char chromaFormatIdc = bs.getUExpGolomb();
      if (chromaFormatIdc == 3){result.sep_col_plane = (bs.get(1) == 1);}
      bs.getUExpGolomb(); // luma
      bs.getUExpGolomb(); // chroma
      bs.skip(1);         // transform bypass
      if (bs.get(1)){// Scaling matrix is present
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
    if (!result.cnt_type){
      result.log2_max_order_cnt = bs.getUExpGolomb() + 4;
    }else if (result.cnt_type == 1){
      ERROR_MSG("This part of the implementation is incomplete(2), to be continued. If this "
                "message is shown, contact developers immediately.");
    }
    result.max_ref_frames = bs.getUExpGolomb(); // max_num_ref_frames
    result.gaps = (bs.get(1) == 1);             // gaps in frame num allowed
    // Stop skipping data and start doing useful stuff

    widthInMbs = bs.getUExpGolomb() + 1;
    heightInMapUnits = bs.getUExpGolomb() + 1;

    result.mbs_only = (bs.get(1) == 1); // Gets used in height calculation
    if (!result.mbs_only){bs.skip(1);}
    bs.skip(1);
    // cropping flag
    if (bs.get(1)){
      cropHorizontal = bs.getUExpGolomb();  // leftOffset
      cropHorizontal += bs.getUExpGolomb(); // rightOffset
      cropVertical = bs.getUExpGolomb();    // topOffset
      cropVertical += bs.getUExpGolomb();   // bottomOffset
    }

    // vuiParameters
    result.fps = 0;
    if (bs.get(1)){
      // Skipping all the paramters we dont use
      if (bs.get(1)){
        uint8_t aspect_ratio_idc = bs.get(8);
        switch (aspect_ratio_idc){
        case 255:
          sar_width = bs.get(16);
          sar_height = bs.get(16);
          break;
        case 2:
          sar_width = 12;
          sar_height = 11;
          break;
        case 3:
          sar_width = 10;
          sar_height = 11;
          break;
        case 4:
          sar_width = 16;
          sar_height = 11;
          break;
        case 5:
          sar_width = 40;
          sar_height = 33;
          break;
        case 6:
          sar_width = 24;
          sar_height = 11;
          break;
        case 7:
          sar_width = 20;
          sar_height = 11;
          break;
        case 8:
          sar_width = 32;
          sar_height = 11;
          break;
        case 9:
          sar_width = 80;
          sar_height = 33;
          break;
        case 10:
          sar_width = 18;
          sar_height = 11;
          break;
        case 11:
          sar_width = 15;
          sar_height = 11;
          break;
        case 12:
          sar_width = 64;
          sar_height = 33;
          break;
        case 13:
          sar_width = 160;
          sar_height = 99;
          break;
        case 14:
          sar_width = 4;
          sar_height = 3;
          break;
        case 15:
          sar_width = 3;
          sar_height = 2;
          break;
        case 16:
          sar_width = 2;
          sar_height = 1;
          break;
        default: break;
        }
      }
      if (bs.get(1)){bs.skip(1);}
      if (bs.get(1)){
        bs.skip(4);
        if (bs.get(1)){bs.skip(24);}
      }
      if (bs.get(1)){
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }

      // Decode timing info
      if (bs.get(1)){
        uint32_t unitsInTick = bs.get(32);
        uint32_t timeScale = bs.get(32);
        result.fps = (double)timeScale / (2 * unitsInTick);
        bs.skip(1);
      }
    }

    result.width = (widthInMbs * 16) - (cropHorizontal * 2);
    result.height = ((result.mbs_only ? 1 : 2) * heightInMapUnits * 16) - (cropVertical * 2);

    if (sar_width != sar_height){
      if (sar_width > sar_height){
        result.width = ((result.width * sar_width) / sar_height);
      }else{
        result.height = ((result.height * sar_height) / sar_width);

      }
    }
    return result;
  }

}

