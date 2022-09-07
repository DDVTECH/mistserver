#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "bitfields.h"
#include "bitstream.h"
#include "defines.h"
#include "h264.h"
#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

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

  std::deque<nalu::nalData> analysePackets(const char *data, size_t len){
    std::deque<nalu::nalData> res;

    size_t offset = 0;
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

  bool sequenceParameterSet::validate() const{
    Utils::bitstream bs;
    for (size_t i = 1; i < dataLen; i++){
      if (i + 2 < dataLen && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    if (bs.size() < 24){return false;}//static size data
    char profileIdc = bs.get(8);
    bs.skip(16);
    bs.getUExpGolomb();//ID
    if (profileIdc == 100 || profileIdc == 110 || profileIdc == 122 || profileIdc == 244 ||
        profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128){
      // chroma format idc
      char chromaFormatIdc = bs.getUExpGolomb();
      if (chromaFormatIdc == 3){bs.get(1);}
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
    bs.getUExpGolomb();
    size_t cnt_type = bs.getUExpGolomb();
    if (!cnt_type){
      bs.getUExpGolomb();
    }else if (cnt_type == 1){
      ERROR_MSG("This part of the implementation is incomplete(2), to be continued. If this "
                "message is shown, contact developers immediately.");
    }
    if (!bs.size()){return false;}
    bs.getUExpGolomb(); // max_num_ref_frames
    bs.get(1);
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    if (!bs.size()){return false;}
    bool mbs_only = (bs.get(1) == 1); // Gets used in height calculation
    if (!mbs_only){bs.skip(1);}
    bs.skip(1);
    // cropping flag
    if (bs.get(1)){
      bs.getUExpGolomb();  // leftOffset
      bs.getUExpGolomb(); // rightOffset
      bs.getUExpGolomb();    // topOffset
      bs.getUExpGolomb();   // bottomOffset
    }

    if (!bs.size()){return false;}
    if (bs.get(1)){
      if (bs.get(1)){bs.skip(8);}
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
      if (!bs.size()){return false;}
      if (bs.get(1)){
        return (bs.size() >= 65);
      }
    }
    return true;
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
      if (i + 2 < dataLen && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
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
        profileIdc == 44 || profileIdc == 83 || profileIdc == 86 || profileIdc == 118 || profileIdc == 128){
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

  void spsUnit::scalingList(uint64_t *scalingList, size_t sizeOfScalingList,
                            bool &useDefaultScalingMatrixFlag, Utils::bitstream &bs){
    int32_t lastScale = 8;
    int32_t nextScale = 8;
    for (int i = 0; i < sizeOfScalingList; i++){
      if (nextScale){
        int64_t deltaScale = bs.getExpGolomb();
        nextScale = (lastScale + deltaScale + 256) % 256;
        useDefaultScalingMatrixFlag = (i == 0 && nextScale == 0);
      }
      scalingList[i] = (nextScale == 0 ? lastScale : nextScale);
      lastScale = scalingList[i];
    }
  }

  spsUnit::spsUnit(const char *data, size_t len) : nalUnit(data, len){
    scalingListPresentFlags = NULL;
    scalingList4x4 = NULL;
    useDefaultScalingMatrix4x4Flag = NULL;
    scalingList8x8 = NULL;
    useDefaultScalingMatrix8x8Flag = NULL;
    derived_subWidthC = 0;
    derived_subHeightC = 0;
    derived_mbWidthC = 0;
    derived_mbHeightC = 0;
    derived_scalingList4x4Amount = 0;
    derived_scalingList8x8Amount = 0;

    // Fill the bitstream
    Utils::bitstream bs;
    for (size_t i = 1; i < len; i++){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    profileIdc = bs.get(8);
    constraintSet0Flag = bs.get(1);
    constraintSet1Flag = bs.get(1);
    constraintSet2Flag = bs.get(1);
    constraintSet3Flag = bs.get(1);
    constraintSet4Flag = bs.get(1);
    constraintSet5Flag = bs.get(1);
    bs.skip(2);
    levelIdc = bs.get(8);
    seqParameterSetId = bs.getUExpGolomb();
    switch (profileIdc){
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
      chromaFormatIdc = bs.getUExpGolomb();
      if (chromaFormatIdc == 3){separateColourPlaneFlag = bs.get(1);}
      if (chromaFormatIdc == 1){
        derived_subWidthC = 2;
        derived_subHeightC = 2;
      }
      if (chromaFormatIdc == 2){
        derived_subWidthC = 2;
        derived_subHeightC = 1;
      }
      if (chromaFormatIdc == 3 && !separateColourPlaneFlag){
        derived_subWidthC = 1;
        derived_subHeightC = 1;
      }
      if (derived_subWidthC){
        derived_mbWidthC = 16 / derived_subWidthC;
        derived_mbHeightC = 16 / derived_subHeightC;
      }

      bitDepthLumaMinus8 = bs.getUExpGolomb();
      derived_bitDepth_Y = 8 + bitDepthLumaMinus8;
      derived_qpBdOffset_Y = 6 * bitDepthLumaMinus8;
      bitDepthChromaMinus8 = bs.getUExpGolomb();
      derived_bitDepth_C = 8 + bitDepthChromaMinus8;
      derived_qpBdOffset_C = 6 * bitDepthChromaMinus8;
      derived_rawMbBits = 256 * derived_bitDepth_Y + 2 * derived_mbWidthC * derived_mbHeightC * derived_bitDepth_C;
      qpprimeYZeroTransformBypassFlag = bs.get(1);
      seqScalingMatrixPresentFlag = bs.get(1);
      if (seqScalingMatrixPresentFlag){
        derived_scalingListSize = (chromaFormatIdc == 3 ? 12 : 8);
        scalingListPresentFlags = (uint8_t *)malloc(derived_scalingListSize * sizeof(uint8_t));

        derived_scalingList4x4Amount = 6;
        scalingList4x4 = (uint64_t **)malloc(derived_scalingList4x4Amount * sizeof(uint64_t *));
        useDefaultScalingMatrix4x4Flag = (bool *)malloc(derived_scalingList4x4Amount * sizeof(bool));
        for (int i = 0; i < derived_scalingList4x4Amount; i++){
          scalingList4x4[i] = NULL;
          useDefaultScalingMatrix4x4Flag[i] = false;
        }

        derived_scalingList8x8Amount = derived_scalingListSize - 6;
        scalingList8x8 = (uint64_t **)malloc(derived_scalingList8x8Amount * sizeof(uint64_t *));
        useDefaultScalingMatrix8x8Flag = (bool *)malloc(derived_scalingList8x8Amount * sizeof(bool));
        for (int i = 0; i < derived_scalingList8x8Amount; i++){
          scalingList8x8[i] = NULL;
          useDefaultScalingMatrix8x8Flag[i] = false;
        }

        for (size_t i = 0; i < derived_scalingListSize; i++){
          scalingListPresentFlags[i] = bs.get(1);
          if (scalingListPresentFlags[i]){
            if (i < 6){
              scalingList4x4[i] = (uint64_t *)malloc(16 * sizeof(uint64_t));
              scalingList(scalingList4x4[i], 16, useDefaultScalingMatrix4x4Flag[i], bs);
            }else{
              scalingList8x8[i - 6] = (uint64_t *)malloc(64 * sizeof(uint64_t));
              scalingList(scalingList8x8[i - 6], 64, useDefaultScalingMatrix8x8Flag[i - 6], bs);
            }
          }
        }
      }
      break;
    default: break;
    }
    log2MaxFrameNumMinus4 = bs.getUExpGolomb();
    derived_maxFrameNum = pow(2, log2MaxFrameNumMinus4 + 4);
    picOrderCntType = bs.getUExpGolomb();
    if (!picOrderCntType){
      log2MaxPicOrderCntLsbMinus4 = bs.getUExpGolomb();
      derived_maxPicOrderCntLsb = pow(2, log2MaxPicOrderCntLsbMinus4 + 4);
    }else if (picOrderCntType == 1){
      deltaPicOrderAlwaysZeroFlag = bs.get(1);
      bs.getExpGolomb(); // offset_for_non_ref_pic
      bs.getExpGolomb(); // offset_for_top_to_bottom_field
      numRefFramesInPicOrderCntCycle = bs.getUExpGolomb();
      for (size_t i = 0; i < numRefFramesInPicOrderCntCycle; i++){
        bs.getExpGolomb(); // offset_for_ref_frame[i]
      }
      return;
    }
    maxNumRefFrames = bs.getUExpGolomb();
    gapsInFrameNumValueAllowedFlag = bs.get(1);
    picWidthInMbsMinus1 = bs.getUExpGolomb();
    derived_picWidthInMbs = picWidthInMbsMinus1 + 1;
    derived_picWidthInSamples_L = derived_picWidthInMbs * 16;
    derived_picWidthInSamples_C = derived_picWidthInMbs * derived_mbWidthC;
    picHeightInMapUnitsMinus1 = bs.getUExpGolomb();
    derived_picHeightInMapUnits = picHeightInMapUnitsMinus1 + 1;
    derived_picSizeInMapUnits = derived_picWidthInMbs * derived_picHeightInMapUnits;
    frameMbsOnlyFlag = bs.get(1);
    derived_frameHeightInMbs = (2 - (frameMbsOnlyFlag ? 1 : 0)) * derived_picHeightInMapUnits;
    if (!frameMbsOnlyFlag){mbAdaptiveFrameFieldFlag = bs.get(1);}
    direct8x8InferenceFlag = bs.get(1);
    frameCroppingFlag = bs.get(1);
    if (frameCroppingFlag){
      frameCropLeftOffset = bs.getUExpGolomb();
      frameCropRightOffset = bs.getUExpGolomb();
      frameCropTopOffset = bs.getUExpGolomb();
      frameCropBottomOffset = bs.getUExpGolomb();
    }
    vuiParametersPresentFlag = bs.get(1);
    if (vuiParametersPresentFlag){vuiParams = vui_parameters(bs);}
  }

  void spsUnit::setSPSNumber(size_t newNumber){
    // for now, can only convert from 0 to 16
    if (seqParameterSetId != 0){return;}
    seqParameterSetId = 16;
    payload.insert(4, 1, 0x08);
  }

  const char* spsUnit::profile(){
    if (profileIdc == 66){
      if (constraintSet1Flag){return "Constrained baseline";}
      return "Baseline";
    }
    if (profileIdc == 77){return "Main";}
    if (profileIdc == 88){return "Extended";}
    if (profileIdc == 100){return "High";}
    if (profileIdc == 110){
      if (constraintSet3Flag){return "High-10 Intra";}
      return "High-10";
    }
    if (profileIdc == 122){
      if (constraintSet3Flag){return "High-4:2:2 Intra";}
      return "High-4:2:2";
    }
    if (profileIdc == 244){
      if (constraintSet3Flag){return "High-4:4:4 Intra";}
      return "High-4:4:4";
    }
    if (profileIdc == 44){return "CAVLC 4:4:4 Intra";}
    return "Unknown";
  }

  const char* spsUnit::level(){
    if (levelIdc == 9){return "1b";}
    if (levelIdc == 10){return "1";}
    if (levelIdc == 11){
      if (constraintSet3Flag){return "1b";}
      return "1.1";
    }
    if (levelIdc == 12){return "1.2";}
    if (levelIdc == 13){return "1.3";}
    if (levelIdc == 20){return "2";}
    if (levelIdc == 21){return "2.1";}
    if (levelIdc == 21){return "2.2";}
    if (levelIdc == 30){return "3";}
    if (levelIdc == 31){return "3.1";}
    if (levelIdc == 31){return "3.2";}
    if (levelIdc == 40){return "4";}
    if (levelIdc == 41){return "4.1";}
    if (levelIdc == 41){return "4.2";}
    if (levelIdc == 50){return "5";}
    if (levelIdc == 51){return "5.1";}
    return "Unknown";
  }

  void spsUnit::toPrettyString(std::ostream &out){
    out << "Nal unit of type " << (((uint8_t)payload[0]) & 0x1F) << " [Sequence Parameter Set] , "
        << payload.size() << " bytes long" << std::endl;
    out << "  profile_idc: 0x" << std::setw(2) << std::setfill('0') << std::hex << (int)profileIdc
        << std::dec << " (" << (int)profileIdc << ") = " << profile() << std::endl;
    out << "  contraints: " << (constraintSet0Flag ? "0 " : "") << (constraintSet1Flag ? "1 " : "")
        << (constraintSet2Flag ? "2 " : "") << (constraintSet3Flag ? "3 " : "")
        << (constraintSet4Flag ? "4 " : "") << (constraintSet5Flag ? "5" : "") << std::endl;
    out << "  level_idc: 0x" << std::setw(2) << std::setfill('0') << std::hex << (int)levelIdc
        << std::dec << " (" << (int)levelIdc << ") = " << level() << std::endl;
    out << "  seq_parameter_set_id: " << seqParameterSetId
        << (seqParameterSetId >= 32 ? " INVALID" : "") << std::endl;
    switch (profileIdc){
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
      out << "  chroma_format_idc: " << chromaFormatIdc << (chromaFormatIdc >= 4 ? " INVALID" : "")
          << std::endl;
      if (chromaFormatIdc == 3){
        out << "  separate_colour_plane_flag: " << (separateColourPlaneFlag ? 1 : 0) << std::endl;
      }
      out << "  bit_depth_luma_minus_8: " << bitDepthLumaMinus8
          << (bitDepthLumaMinus8 >= 7 ? " INVALID" : "") << std::endl;
      out << "    -> BitDepth_Y: " << derived_bitDepth_Y << std::endl;
      out << "    -> QpBdOffset_Y: " << derived_qpBdOffset_Y << std::endl;
      out << "  bit_depth_chroma_minus_8: " << bitDepthChromaMinus8
          << (bitDepthChromaMinus8 >= 7 ? " INVALID" : "") << std::endl;
      out << "    -> BitDepth_C: " << derived_bitDepth_C << std::endl;
      out << "    -> QpBdOffset_C: " << derived_qpBdOffset_C << std::endl;
      out << "    -> RawMbBits: " << derived_rawMbBits << std::endl;
      out << "  qpprime_y_zero-transform_bypass_flag: " << (qpprimeYZeroTransformBypassFlag ? 1 : 0)
          << std::endl;
      out << "  seq_scaling_matrix_present_flag: " << (seqScalingMatrixPresentFlag ? 1 : 0) << std::endl;
      if (seqScalingMatrixPresentFlag){
        for (int i = 0; i < derived_scalingListSize; i++){
          out << "    seq_scaling_list_present_flag[" << i
              << "]: " << (scalingListPresentFlags[i] ? 1 : 0) << std::endl;
          if (scalingListPresentFlags[i]){
            if (i < 6){
              for (int j = 0; j < 16; j++){
                out << "      scalingMatrix4x4[" << i << "][" << j << "]: " << scalingList4x4[i][j]
                    << std::endl;
              }
              out << "  useDefaultScalingMatrix4x4Flag[" << i
                  << "]: " << (useDefaultScalingMatrix4x4Flag[i] ? 1 : 0) << std::endl;
            }else{
              for (int j = 0; j < 64; j++){
                out << "      scalingMatrix8x8[" << i - 6 << "][" << j
                    << "]: " << scalingList8x8[i - 6][j] << std::endl;
              }
              out << "  useDefaultScalingMatrix8x8Flag[" << i - 6
                  << "]: " << (useDefaultScalingMatrix8x8Flag[i - 6] ? 1 : 0) << std::endl;
            }
          }
        }
      }
      break;
    default: break;
    }
    out << "  log2_max_frame_num_minus4: " << log2MaxFrameNumMinus4
        << (log2MaxFrameNumMinus4 >= 13 ? " INVALID" : "") << std::endl;
    out << "    -> MaxFrameNum: " << derived_maxFrameNum << std::endl;
    out << "  pic_order_cnt_type: " << picOrderCntType << (picOrderCntType >= 3 ? " INVALID" : "") << std::endl;
    if (!picOrderCntType){
      out << "  log2_max_pic_order_cnt_lsb_minus4: " << log2MaxPicOrderCntLsbMinus4
          << (log2MaxPicOrderCntLsbMinus4 >= 13 ? " INVALID" : "") << std::endl;
      out << "    -> MaxPicOrderCntLsb: " << derived_maxPicOrderCntLsb << std::endl;
    }
    out << "  max_num_ref_frames: " << maxNumRefFrames << std::endl;
    out << "  gaps_in_frame_num_value_allowed_flag: " << (gapsInFrameNumValueAllowedFlag ? 1 : 0) << std::endl;
    out << "  pic_width_in_mbs_minus_1: " << picWidthInMbsMinus1 << std::endl;
    out << "    -> PicWidthInMbs: " << derived_picWidthInMbs << std::endl;
    out << "    -> PicWidthInSamples_L: " << derived_picWidthInSamples_L << std::endl;
    out << "    -> PicWidthInSamples_C: " << derived_picWidthInSamples_C << std::endl;
    out << "  pic_height_in_map_units_minus_1: " << picHeightInMapUnitsMinus1 << std::endl;
    out << "    -> PicHeightInMapUnits: " << derived_picHeightInMapUnits << std::endl;
    out << "    -> PicSizeInMapUnits: " << derived_picSizeInMapUnits << std::endl;
    out << "  frame_mbs_only_flag: " << frameMbsOnlyFlag << std::endl;
    out << "    -> FrameHeightInMbs: " << derived_frameHeightInMbs << std::endl;
    if (!frameMbsOnlyFlag){
      out << "  mb_adaptive_frame_field_flag: " << mbAdaptiveFrameFieldFlag << std::endl;
    }
    out << "  direct_8x8_inference_flag: " << direct8x8InferenceFlag << std::endl;
    out << "  frame_cropping_flag: " << frameCroppingFlag << std::endl;
    if (frameCroppingFlag){
      out << "  frame_crop_left_offset: " << frameCropLeftOffset << std::endl;
      out << "  frame_crop_right_offset: " << frameCropRightOffset << std::endl;
      out << "  frame_crop_top_offset: " << frameCropTopOffset << std::endl;
      out << "  frame_crop_bottom_offset: " << frameCropBottomOffset << std::endl;
    }
    out << "  vui_parameter_present_flag: " << vuiParametersPresentFlag << std::endl;
    if (vuiParametersPresentFlag){vuiParams.toPrettyString(out, 2);}
  }

  std::string spsUnit::generate(){
    Utils::bitWriter bw;
    bw.append(0x07, 8);
    bw.append(profileIdc, 8);
    bw.append(constraintSet0Flag ? 1 : 0, 1);
    bw.append(constraintSet1Flag ? 1 : 0, 1);
    bw.append(constraintSet2Flag ? 1 : 0, 1);
    bw.append(constraintSet3Flag ? 1 : 0, 1);
    bw.append(constraintSet4Flag ? 1 : 0, 1);
    bw.append(constraintSet5Flag ? 1 : 0, 1);
    bw.append(0x00, 2);
    bw.append(levelIdc, 8);
    bw.appendUExpGolomb(seqParameterSetId);
    switch (profileIdc){
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
      bw.appendUExpGolomb(chromaFormatIdc);
      if (chromaFormatIdc == 3){bw.append(separateColourPlaneFlag ? 1 : 0, 1);}
      bw.appendUExpGolomb(bitDepthLumaMinus8);
      bw.appendUExpGolomb(bitDepthChromaMinus8);
      bw.append(qpprimeYZeroTransformBypassFlag, 1);
      bw.append(seqScalingMatrixPresentFlag, 1);
      if (seqScalingMatrixPresentFlag){
        for (int i = 0; i < derived_scalingListSize; i++){bw.append(0, 1);}
      }
      break;
    default: break;
    }
    bw.appendUExpGolomb(log2MaxFrameNumMinus4);
    bw.appendUExpGolomb(picOrderCntType);
    if (picOrderCntType == 0){bw.appendUExpGolomb(log2MaxPicOrderCntLsbMinus4);}
    bw.appendUExpGolomb(maxNumRefFrames);
    bw.append(gapsInFrameNumValueAllowedFlag ? 1 : 0, 1);
    bw.appendUExpGolomb(picWidthInMbsMinus1);
    bw.appendUExpGolomb(picHeightInMapUnitsMinus1);
    bw.append(frameMbsOnlyFlag ? 1 : 0, 1);
    if (!frameMbsOnlyFlag){bw.append(mbAdaptiveFrameFieldFlag ? 1 : 0, 1);}
    bw.append(direct8x8InferenceFlag ? 1 : 0, 1);
    bw.append(frameCroppingFlag ? 1 : 0, 1);
    if (frameCroppingFlag){
      bw.appendUExpGolomb(frameCropLeftOffset);
      bw.appendUExpGolomb(frameCropRightOffset);
      bw.appendUExpGolomb(frameCropTopOffset);
      bw.appendUExpGolomb(frameCropBottomOffset);
    }
    bw.append(vuiParametersPresentFlag ? 1 : 0, 1);
    if (vuiParametersPresentFlag){vuiParams.generate(bw);}
    bw.append(1, 1);
    std::string tmp = bw.str();
    std::string res;
    for (int i = 0; i < tmp.size(); i++){
      if (res.size() > 2 && res[res.size() - 1] == 0x00 && res[res.size() - 2] == 0x00){
        if (tmp[i] == 0x00 || tmp[i] == 0x01 || tmp[i] == 0x02 || tmp[i] == 0x03){
          res += (char)0x03;
        }
      }
      res += tmp[i];
    }
    return res;
  }

  bool more_rbsp_data(Utils::bitstream &bs){
    if (bs.size() < 8){return false;}
    return true;
  }

  void ppsUnit::scalingList(uint64_t *scalingList, size_t sizeOfScalingList,
                            bool &useDefaultScalingMatrixFlag, Utils::bitstream &bs){
    int lastScale = 8;
    int nextScale = 8;
    for (int i = 0; i < sizeOfScalingList; i++){
      if (nextScale){
        int64_t deltaScale = bs.getExpGolomb();
        nextScale = (lastScale + deltaScale + 256) % 256;
        useDefaultScalingMatrixFlag = (i == 0 && nextScale == 0);
      }
      scalingList[i] = (nextScale == 0 ? lastScale : nextScale);
      lastScale = scalingList[i];
    }
  }

  bool ppsValidate(const char *data, size_t len){
    Utils::bitstream bs;
    for (size_t i = 1; i < len; i++){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.get(2);
    if (bs.getUExpGolomb() > 0){
      WARN_MSG("num_slice_groups_minus1 > 0, unimplemented structure");
      return false;
    }
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.get(3);
    bs.getExpGolomb();
    bs.getExpGolomb();
    bs.getExpGolomb();
    bs.get(2);
    if (!bs.size()){return false;}
    bs.get(1);
    if (!more_rbsp_data(bs)){return true;}
    bs.get(1);
    if (bs.get(1)){
      //tricky scaling stuff, assume we're good.
      /// \TODO Maybe implement someday? Do we care? Doubt.
      return true;
    }
    return bs.size();
  }

  ppsUnit::ppsUnit(const char *data, size_t len, uint8_t chromaFormatIdc) : nalUnit(data, len){
    picScalingMatrixPresentFlags = NULL;
    Utils::bitstream bs;
    for (size_t i = 1; i < len; i++){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    picParameterSetId = bs.getUExpGolomb();
    seqParameterSetId = bs.getUExpGolomb();
    entropyCodingModeFlag = bs.get(1);
    bottomFieldPicOrderInFramePresentFlag = bs.get(1);
    numSliceGroupsMinus1 = bs.getUExpGolomb();
    if (numSliceGroupsMinus1 > 0){
      WARN_MSG("num_slice_groups_minus1 > 0, unimplemented structure");
      return;
    }
    numrefIdx10DefaultActiveMinus1 = bs.getUExpGolomb();
    numrefIdx11DefaultActiveMinus1 = bs.getUExpGolomb();
    weightedPredFlag = bs.get(1);
    weightedBipredIdc = bs.get(2);
    picInitQpMinus26 = bs.getExpGolomb();
    picInitQsMinus26 = bs.getExpGolomb();
    chromaQpIndexOffset = bs.getExpGolomb();
    deblockingFilterControlPresentFlag = bs.get(1);
    constrainedIntraPredFlag = bs.get(1);
    redundantPicCntPresentFlag = bs.get(1);
    if (!more_rbsp_data(bs)){
      status_moreRBSP = false;
      return;
    }
    status_moreRBSP = true;
    transform8x8ModeFlag = bs.get(1);
    picScalingMatrixPresentFlag = bs.get(1);
    if (picScalingMatrixPresentFlag){
      derived_scalingListSize =
          6 + (chromaFormatIdc ? ((chromaFormatIdc == 3 ? 6 : 2) * transform8x8ModeFlag) : 0);
      picScalingMatrixPresentFlags = (uint8_t *)malloc(derived_scalingListSize * sizeof(uint8_t));

      derived_scalingList4x4Amount = 6;
      scalingList4x4 = (uint64_t **)malloc(derived_scalingList4x4Amount * sizeof(uint64_t *));
      useDefaultScalingMatrix4x4Flag = (bool *)malloc(derived_scalingList4x4Amount * sizeof(bool));
      for (int i = 0; i < derived_scalingList4x4Amount; i++){
        scalingList4x4[i] = NULL;
        useDefaultScalingMatrix4x4Flag[i] = false;
      }

      derived_scalingList8x8Amount = derived_scalingListSize - 6;
      scalingList8x8 = (uint64_t **)malloc(derived_scalingList8x8Amount * sizeof(uint64_t *));
      useDefaultScalingMatrix8x8Flag = (bool *)malloc(derived_scalingList8x8Amount * sizeof(bool));
      for (int i = 0; i < derived_scalingList8x8Amount; i++){
        scalingList8x8[i] = NULL;
        useDefaultScalingMatrix8x8Flag[i] = false;
      }

      for (size_t i = 0; i < derived_scalingListSize; i++){
        picScalingMatrixPresentFlags[i] = bs.get(1);
        if (picScalingMatrixPresentFlags[i]){
          if (i < 6){
            scalingList4x4[i] = (uint64_t *)malloc(16 * sizeof(uint64_t));
            scalingList(scalingList4x4[i], 16, useDefaultScalingMatrix4x4Flag[i], bs);
          }else{
            scalingList8x8[i - 6] = (uint64_t *)malloc(64 * sizeof(uint64_t));
            scalingList(scalingList8x8[i - 6], 64, useDefaultScalingMatrix8x8Flag[i - 6], bs);
          }
        }
      }
    }
    secondChromaQpIndexOffset = bs.getExpGolomb();
  }

  void ppsUnit::setPPSNumber(size_t newNumber){
    // for now, can only convert from 0 to 16
    picParameterSetId = newNumber;
  }

  void ppsUnit::setSPSNumber(size_t newNumber){
    // for now, can only convert from 0 to 16
    if (seqParameterSetId != 0 || picParameterSetId != 16){return;}
    seqParameterSetId = 16;
    payload.insert(2, 1, 0x84);
    payload[3] &= 0x7F;
  }

  void ppsUnit::toPrettyString(std::ostream &out){
    out << "Nal unit of type " << (((uint8_t)payload[0]) & 0x1F) << " [Picture Parameter Set] , "
        << payload.size() << " bytes long" << std::endl;
    out << "  pic_parameter_set_id: " << picParameterSetId
        << (picParameterSetId >= 256 ? " INVALID" : "") << std::endl;
    out << "  seq_parameter_set_id: " << seqParameterSetId
        << (seqParameterSetId >= 32 ? " INVALID" : "") << std::endl;
    out << "  entropy_coding_mode_flag: " << (entropyCodingModeFlag ? 1 : 0) << std::endl;
    out << "  bottom_field_pic_order_in_frame_present_flag: "
        << (bottomFieldPicOrderInFramePresentFlag ? 1 : 0) << std::endl;
    out << "  num_slice_groups_minus1: " << numSliceGroupsMinus1 << std::endl;
    if (numSliceGroupsMinus1 > 0){return;}
    out << "  num_ref_idx_10_default_active_minus1: " << numrefIdx10DefaultActiveMinus1 << std::endl;
    out << "  num_ref_idx_11_default_active_minus1: " << numrefIdx11DefaultActiveMinus1 << std::endl;
    out << "  weighted_pred_flag: " << (weightedPredFlag ? 1 : 0) << std::endl;
    out << "  weighted_bipred_idc: " << (uint32_t)weightedBipredIdc << std::endl;
    out << "  pic_init_qp_minus26: " << picInitQpMinus26 << std::endl;
    out << "  pic_init_qs_minus26: " << picInitQsMinus26 << std::endl;
    out << "  chroma_qp_index_offset: " << chromaQpIndexOffset << std::endl;
    out << "  deblocking_filter_control_present_flag: " << deblockingFilterControlPresentFlag << std::endl;
    out << "  constrained_intra_pred_flag: " << constrainedIntraPredFlag << std::endl;
    out << "  redundant_pic_cnt_present_flag: " << redundantPicCntPresentFlag << std::endl;
    if (status_moreRBSP){
      out << "  transform_8x8_mode_flag: " << transform8x8ModeFlag << std::endl;
      out << "  pic_scaling_matrix_present_flag: " << picScalingMatrixPresentFlag << std::endl;
      if (picScalingMatrixPresentFlag){
        for (int i = 0; i < derived_scalingListSize; i++){
          out << "    pic_scaling_matrix_present_flag[" << i
              << "]: " << (picScalingMatrixPresentFlags[i] ? 1 : 0) << std::endl;
          if (picScalingMatrixPresentFlags[i]){
            if (i < 6){
              for (int j = 0; j < 16; j++){
                out << "      scalingMatrix4x4[" << i << "][" << j << "]: " << scalingList4x4[i][j]
                    << std::endl;
              }
              out << "      useDefaultScalingMatrix4x4Flag[" << i
                  << "]: " << (useDefaultScalingMatrix4x4Flag[i] ? 1 : 0) << std::endl;
            }else{
              for (int j = 0; j < 64; j++){
                out << "      scalingMatrix8x8[" << i - 6 << "][" << j
                    << "]: " << scalingList8x8[i - 6][j] << std::endl;
              }
              out << "      useDefaultScalingMatrix8x8Flag[" << i - 6
                  << "]: " << (useDefaultScalingMatrix8x8Flag[i - 6] ? 1 : 0) << std::endl;
            }
          }
        }
      }
      out << "    second_chroma_qp_index_offset: " << secondChromaQpIndexOffset << std::endl;
    }
  }

  std::string ppsUnit::generate(){
    Utils::bitWriter bw;
    bw.append(0x08, 8);
    bw.appendUExpGolomb(picParameterSetId);
    bw.appendUExpGolomb(seqParameterSetId);
    bw.append(entropyCodingModeFlag ? 1 : 0, 1);
    bw.append(bottomFieldPicOrderInFramePresentFlag ? 1 : 0, 1);
    if (numSliceGroupsMinus1 > 0){INFO_MSG("Forcing to numSliceGroupsMinus1 == 0");}
    bw.appendUExpGolomb(0); // numSliceGroupsMinus1
    bw.appendUExpGolomb(numrefIdx10DefaultActiveMinus1);
    bw.appendUExpGolomb(numrefIdx11DefaultActiveMinus1);
    bw.append(weightedPredFlag ? 1 : 0, 1);
    bw.append(weightedBipredIdc, 2);
    bw.appendExpGolomb(picInitQpMinus26);
    bw.appendExpGolomb(picInitQsMinus26);
    bw.appendExpGolomb(chromaQpIndexOffset);
    bw.append(deblockingFilterControlPresentFlag ? 1 : 0, 1);
    bw.append(constrainedIntraPredFlag ? 1 : 0, 1);
    bw.append(redundantPicCntPresentFlag ? 1 : 0, 1);

    if (status_moreRBSP){
      bw.append(transform8x8ModeFlag ? 1 : 0, 1);
      bw.append(picScalingMatrixPresentFlag, 1);
      if (picScalingMatrixPresentFlag){
        for (int i = 0; i < derived_scalingListSize; i++){
          bw.append(0, 1); // picScalingMatrixPresnetFlags[i]
        }
      }
      bw.appendExpGolomb(secondChromaQpIndexOffset);
    }
    bw.append(1, 1);

    std::string tmp = bw.str();
    std::string res;
    for (int i = 0; i < tmp.size(); i++){
      if (res.size() > 2 && res[res.size() - 1] == 0x00 && res[res.size() - 2] == 0x00){
        if (tmp[i] == 0x00 || tmp[i] == 0x01 || tmp[i] == 0x02 || tmp[i] == 0x03){
          res += (char)0x03;
        }
      }
      res += tmp[i];
    }
    return res;
  }

  codedSliceUnit::codedSliceUnit(const char *data, size_t len) : nalUnit(data, len){
    Utils::bitstream bs;
    // We only want to parse a part of the header here. From standard it seems
    // that maximum possible lenght will be less than 100 bits, so using 16
    // bytes for safety
    size_t l = len < 16 ? len : 16;
    for (size_t i = 1; i < l; i++){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    firstMbInSlice = bs.getUExpGolomb();
    sliceType = bs.getUExpGolomb();
    picParameterSetId = bs.getUExpGolomb();
  }

  codedSliceUnit::codedSliceUnit(const char *data, size_t len, const spsUnit &sps, const ppsUnit &pps)
      : nalUnit(data, len){
    Utils::bitstream bs;
    // We only want to parse slice header here. Maximum possible length seems
    // to be around 50 bytes, so using 64 bytes for safety
    size_t l = len < 64 ? len : 64;
    for (size_t i = 1; i < l; i++){
      if (i + 2 < l && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    firstMbInSlice = bs.getUExpGolomb();
    sliceType = bs.getUExpGolomb();
    picParameterSetId = bs.getUExpGolomb();

    frameNum = bs.get(sps.log2MaxFrameNumMinus4 + 4);
    if (!sps.frameMbsOnlyFlag){
      fieldPicFlag = bs.get(1);
      if (fieldPicFlag){bottomFieldFlag = bs.get(1);}
    }

    // IDR frames have idrPicId here
    if (5 == getType()){idrPicId = bs.getUExpGolomb();}

    picOrderCntLsb = deltaPicOrderCntBottom = 0;
    if (0 == sps.picOrderCntType){
      picOrderCntLsb = bs.get(sps.log2MaxPicOrderCntLsbMinus4 + 4);
      if (pps.bottomFieldPicOrderInFramePresentFlag && !fieldPicFlag){
        deltaPicOrderCntBottom = bs.getExpGolomb();
      }
    }

    deltaPicOrderCnt[0] = deltaPicOrderCnt[1] = 0;
    if ((1 == sps.picOrderCntType) && !sps.deltaPicOrderAlwaysZeroFlag){
      deltaPicOrderCnt[0] = bs.getExpGolomb();
      if (pps.bottomFieldPicOrderInFramePresentFlag && fieldPicFlag){
        deltaPicOrderCnt[1] = bs.getExpGolomb();
      }
    }

    if (pps.redundantPicCntPresentFlag){redundantPicCnt = bs.getUExpGolomb();}
  }

  void codedSliceUnit::setPPSNumber(size_t newNumber){
    // for now, can only convert from 0 to 16
    if (picParameterSetId != 0){return;}
    size_t bitOffset = 0;
    bitOffset += Utils::bitstream::bitSizeUExpGolomb(firstMbInSlice);
    bitOffset += Utils::bitstream::bitSizeUExpGolomb(sliceType);
    size_t byteOffset = bitOffset / 8;
    bitOffset -= (byteOffset * 8);
    INFO_MSG("Offset for this value: %zu bytes and %zu bits", byteOffset, bitOffset);
    size_t firstBitmask = ((1 << bitOffset) - 1) << (8 - bitOffset);
    size_t secondBitmask = (1 << (8 - bitOffset)) - 1;
    INFO_MSG("Bitmasks: %.2zX, %.2zX", firstBitmask, secondBitmask);
    char toCopy = payload[1 + byteOffset];
    payload.insert(1 + byteOffset, 1, toCopy);
    payload[1 + byteOffset] &= firstBitmask;
    payload[1 + byteOffset] |= (0x08 >> bitOffset);
    payload[2 + byteOffset] &= secondBitmask;
    payload[2 + byteOffset] |= (0x08 << (8 - bitOffset));
    INFO_MSG("Translated %.2X to %.2X %.2X", toCopy, payload[1 + byteOffset], payload[2 + byteOffset]);
  }

  void codedSliceUnit::toPrettyString(std::ostream &out){
    std::string strSliceType = "Unknown";
    switch (sliceType){
    case 5:
    case 0: strSliceType = "P - Predictive slice (at most 1 reference)"; break;
    case 6:
    case 1: strSliceType = " B - Bi-predictive slice (at most 2 references)"; break;
    case 7:
    case 2: strSliceType = " I - Intra slice (no external references)"; break;
    case 8:
    case 3: strSliceType = " SP - Switching predictive slice (at most 1 reference)"; break;
    case 9:
    case 4: strSliceType = " SI - Switching intra slice (no external references)"; break;
    }
    out << "Nal unit of type " << (((uint8_t)payload[0]) & 0x1F) << " [Coded Slice] , "
        << payload.size() << " bytes long" << std::endl;
    out << "  first_mb_in_slice: " << firstMbInSlice << std::endl;
    out << "  slice_type " << sliceType << ": " << strSliceType << std::endl;
    out << "  pic_parameter_set_id: " << picParameterSetId
        << (picParameterSetId >= 256 ? " INVALID" : "") << std::endl;
  }

  seiUnit::seiUnit(const char *data, size_t len) : nalUnit(data, len){
    Utils::bitstream bs;
    payloadOffset = 1;
    for (size_t i = 1; i < len; i++){
      if (i + 2 < len && (memcmp(data + i, "\000\000\003", 3) == 0)){// Emulation prevention bytes
        // Yes, we increase i here
        bs.append(data + i, 2);
        i += 2;
      }else{
        // No we don't increase i here
        bs.append(data + i, 1);
      }
    }
    uint8_t tmp = bs.get(8);
    ++payloadOffset;
    payloadType = 0;
    while (tmp == 0xFF){
      payloadType += tmp;
      tmp = bs.get(8);
      ++payloadOffset;
    }
    payloadType += tmp;

    tmp = bs.get(8);
    ++payloadOffset;
    payloadSize = 0;
    while (tmp == 0xFF){
      payloadSize += tmp;
      tmp = bs.get(8);
      ++payloadOffset;
    }
    payloadSize += tmp;
  }

  void seiUnit::toPrettyString(std::ostream &out){
    out << "Nal unit of type " << (((uint8_t)payload[0]) & 0x1F)
        << " [Supplemental Enhancement Unit] , " << payload.size() << " bytes long" << std::endl;
    switch (payloadType){
    case 5:{// User data, unregistered
      out << "  Type 5: User data, unregistered." << std::endl;
      std::stringstream uuid;
      for (uint32_t i = payloadOffset; i < payloadOffset + 16; ++i){
        uuid << std::setw(2) << std::setfill('0') << std::hex << (int)(payload.data()[i]);
      }
      if (uuid.str() == "dc45e9bde6d948b7962cd820d923eeef"){
        uuid.str("x264 encoder configuration");
      }
      out << "   UUID: " << uuid.str() << std::endl;
      out << "   Payload: " << std::string(payload.data() + payloadOffset + 16, payloadSize - 17) << std::endl;
    }break;
    default:
      out << "  Message of type " << payloadType << ", " << payloadSize << " bytes long" << std::endl;
    }
  }

  nalUnit *nalFactory(const char *_data, size_t _len, size_t &offset, bool annexb){
    if (annexb){
      // check if we have a start marker at the beginning, if so, move the offset over
      if (_len > offset && !_data[offset]){
        for (size_t i = offset + 1; i < _len; ++i){
          if (_data[i] > 1){
            FAIL_MSG("Encountered bullshit AnnexB data..?");
            return 0;
          }
          if (_data[i] == 1){
            offset = i + 1;
            break;
          }
        }
      }
      // now we know we're starting at real data. Yay!
    }
    if (_len < offset + 4){
      WARN_MSG("Not at least 4 bytes available - cancelling");
      return 0;
    }
    uint32_t pktLen = 0;
    if (!annexb){
      // read the 4b size in front
      pktLen = Bit::btohl(_data + offset);
      if (_len - offset < 4 + pktLen){
        WARN_MSG("Not at least 4+%" PRIu32 " bytes available - cancelling", pktLen);
        return 0;
      }
      offset += 4;
    }
    const char *data = _data + offset;
    size_t len = _len - offset;
    if (annexb){
      // search for the next start marker
      for (size_t i = 1; i < len - 2; ++i){
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1){
          while (i && !data[i]){--i;}
          pktLen = i + 1;
          offset += pktLen;
          break;
        }
      }
    }else{
      offset += pktLen;
    }
    if (!pktLen){
      WARN_MSG("Cannot determine packet length - cancelling");
      return 0;
    }
    switch (data[0] & 0x1F){
    case 1:
    case 5:
    case 19: return new codedSliceUnit(data, pktLen);
    case 6: return new seiUnit(data, pktLen);
    case 7: return new spsUnit(data, pktLen);
    case 8: return new ppsUnit(data, pktLen);
    default: return new nalUnit(data, pktLen);
    }
  }

  nalUnit *nalFactory(FILE *in, bool annexb){
    nalUnit *result = NULL;
    char size[4];
    if (fread(size, 4, 1, in)){
      if (annexb){
        size_t curPos = ftell(in);
        if (size[2] == 0x01){curPos--;}
        fseek(in, curPos, SEEK_SET);
        char *data = (char *)malloc(1024 * 1024 * sizeof(char)); // allocate 1MB in size
        size_t len = fread(data, 1, 1024 * 1024, in);
        if (len){
          std::string str(data, len);
          size_t nextPos = str.find("\000\000\001", 0, 3);
          if (nextPos == std::string::npos && feof(in)){nextPos = len;}
          if (nextPos != std::string::npos){
            if (str[nextPos - 1] == 0x00){nextPos--;}
            switch (data[0] & 0x1F){
            case 1:
            case 5:
            case 19: result = new codedSliceUnit(data, nextPos); break;
            case 6: result = new seiUnit(data, nextPos); break;
            case 7: result = new spsUnit(data, nextPos); break;
            case 8: result = new ppsUnit(data, nextPos); break;
            default: result = new nalUnit(data, nextPos); break;
            }
            fseek(in, curPos + nextPos, SEEK_SET);
          }else{
            FAIL_MSG(
                "NAL Unit of over 1MB, unexpected behaviour until next AnnexB boundary in file");
          }
        }
        free(data);
      }else{
        uint32_t len = Bit::btohl(size);
        char *data = (char *)malloc(len * sizeof(char));
        if (fread(data, len, 1, in)){
          switch (data[0] & 0x1F){
          case 7: result = new spsUnit(data, len); break;
          default: result = new nalUnit(data, len); break;
          }
        }
        free(data);
      }
    }
    return result;
  }

  vui_parameters::vui_parameters(Utils::bitstream &bs){
    aspectRatioInfoPresentFlag = bs.get(1);
    if (aspectRatioInfoPresentFlag){
      aspectRatioIdc = bs.get(8);
      if (aspectRatioIdc == 255){
        sarWidth = bs.get(16);
        sarHeight = bs.get(16);
      }
    }
    overscanInfoPresentFlag = bs.get(1);
    if (overscanInfoPresentFlag){overscanAppropriateFlag = bs.get(1);}
    videoSignalTypePresentFlag = bs.get(1);
    if (videoSignalTypePresentFlag){
      videoFormat = bs.get(3);
      videoFullRangeFlag = bs.get(1);
      colourDescriptionPresentFlag = bs.get(1);
      if (colourDescriptionPresentFlag){
        colourPrimaries = bs.get(8);
        transferCharacteristics = bs.get(8);
        matrixCoefficients = bs.get(8);
      }
    }
    chromaLocInfoPresentFlag = bs.get(1);
    if (chromaLocInfoPresentFlag){
      chromaSampleLocTypeTopField = bs.getUExpGolomb();
      chromaSampleLocTypeBottomField = bs.getUExpGolomb();
    }
    timingInfoPresentFlag = bs.get(1);
    derived_fps = 0.0;
    if (timingInfoPresentFlag){
      numUnitsInTick = bs.get(32);
      timeScale = bs.get(32);
      fixedFrameRateFlag = bs.get(1);
      derived_fps = (double)timeScale / (2 * numUnitsInTick);
    }
    nalHrdParametersPresentFlag = bs.get(1);
    // hrd param nal
    vclHrdParametersPresentFlag = bs.get(1);
    // hrd param vcl
    if (nalHrdParametersPresentFlag || vclHrdParametersPresentFlag){lowDelayHrdFlag = bs.get(1);}
    picStructPresentFlag = bs.get(1);
    bitstreamRestrictionFlag = bs.get(1);
    if (bitstreamRestrictionFlag){
      motionVectorsOverPicBoundariesFlag = bs.get(1);
      maxBytesPerPicDenom = bs.getUExpGolomb();
      maxBitsPerMbDenom = bs.getUExpGolomb();
      log2MaxMvLengthHorizontal = bs.getUExpGolomb();
      log2MaxMvLengthVertical = bs.getUExpGolomb();
      numReorderFrames = bs.getUExpGolomb();
      maxDecFrameBuffering = bs.getUExpGolomb();
    }
  }

  void vui_parameters::generate(Utils::bitWriter &bw){
    bw.append(aspectRatioInfoPresentFlag ? 1 : 0, 1);
    if (aspectRatioInfoPresentFlag){
      bw.append(aspectRatioIdc, 8);
      if (aspectRatioIdc == 0xFF){
        bw.append(sarWidth, 16);
        bw.append(sarHeight, 16);
      }
    }
    bw.append(overscanInfoPresentFlag ? 1 : 0, 1);
    if (overscanInfoPresentFlag){bw.append(overscanAppropriateFlag ? 1 : 0, 1);}
    bw.append(videoSignalTypePresentFlag ? 1 : 0, 1);
    if (videoSignalTypePresentFlag){
      bw.append(videoFormat, 3);
      bw.append(videoFullRangeFlag, 1);
      bw.append(colourDescriptionPresentFlag ? 1 : 0, 1);
      if (colourDescriptionPresentFlag){
        bw.append(colourPrimaries, 8);
        bw.append(transferCharacteristics, 8);
        bw.append(matrixCoefficients, 8);
      }
    }
    bw.append(chromaLocInfoPresentFlag ? 1 : 0, 1);
    if (chromaLocInfoPresentFlag){
      bw.appendUExpGolomb(chromaSampleLocTypeTopField);
      bw.appendUExpGolomb(chromaSampleLocTypeBottomField);
    }
    bw.append(timingInfoPresentFlag ? 1 : 0, 1);
    if (timingInfoPresentFlag){
      bw.append(numUnitsInTick, 32);
      bw.append(timeScale, 32);
      bw.append(fixedFrameRateFlag ? 1 : 0, 1);
    }
    bw.append(nalHrdParametersPresentFlag ? 1 : 0, 1);
    if (nalHrdParametersPresentFlag){}
    bw.append(vclHrdParametersPresentFlag ? 1 : 0, 1);
    if (vclHrdParametersPresentFlag){}
    if (nalHrdParametersPresentFlag || vclHrdParametersPresentFlag){
      bw.append(lowDelayHrdFlag ? 1 : 0, 1);
    }
    bw.append(picStructPresentFlag ? 1 : 0, 1);
    bw.append(bitstreamRestrictionFlag ? 1 : 0, 1);
    if (bitstreamRestrictionFlag){
      bw.append(motionVectorsOverPicBoundariesFlag ? 1 : 0, 1);
      bw.appendUExpGolomb(maxBytesPerPicDenom);
      bw.appendUExpGolomb(maxBitsPerMbDenom);
      bw.appendUExpGolomb(log2MaxMvLengthHorizontal);
      bw.appendUExpGolomb(log2MaxMvLengthVertical);
      bw.appendUExpGolomb(numReorderFrames);
      bw.appendUExpGolomb(maxDecFrameBuffering);
    }
  }

  void vui_parameters::toPrettyString(std::ostream &out, size_t indent){
    out << std::string(indent, ' ') << "Vui parameters" << std::endl;
    out << std::string(indent + 2, ' ')
        << "aspect_ratio_info_present_flag: " << aspectRatioInfoPresentFlag << std::endl;
    if (aspectRatioInfoPresentFlag){
      out << std::string(indent + 2, ' ') << "aspect_ratio_idc: " << (int32_t)aspectRatioIdc << std::endl;
      if (aspectRatioIdc == 255){
        out << std::string(indent + 2, ' ') << "sar_width: " << sarWidth << std::endl;
        out << std::string(indent + 2, ' ') << "sar_height: " << sarHeight << std::endl;
      }
    }
    out << std::string(indent + 2, ' ') << "overscan_info_present_flag: " << overscanInfoPresentFlag
        << std::endl;
    if (overscanInfoPresentFlag){
      out << std::string(indent + 2, ' ')
          << "overscan_appropriate_present_flag: " << overscanAppropriateFlag << std::endl;
    }
    out << std::string(indent + 2, ' ')
        << "video_signal_type_present_flag: " << videoSignalTypePresentFlag << std::endl;
    if (videoSignalTypePresentFlag){
      out << std::string(indent + 2, ' ') << "video_format" << videoFormat << std::endl;
      out << std::string(indent + 2, ' ') << "video_full_range_flag" << videoFullRangeFlag << std::endl;
      out << std::string(indent + 2, ' ') << "colour_description_present_flag"
          << colourDescriptionPresentFlag << std::endl;
      if (colourDescriptionPresentFlag){
        out << std::string(indent + 2, ' ') << "colour_primaries" << colourPrimaries << std::endl;
        out << std::string(indent + 2, ' ') << "transfer_characteristics" << transferCharacteristics
            << std::endl;
        out << std::string(indent + 2, ' ') << "matrix_coefficients" << matrixCoefficients << std::endl;
      }
    }
    out << std::string(indent + 2, ' ')
        << "chroma_loc_info_present_flag: " << chromaLocInfoPresentFlag << std::endl;
    if (chromaLocInfoPresentFlag){
      out << std::string(indent + 2, ' ') << "chroma_sample_loc_type_top_field"
          << chromaSampleLocTypeTopField << std::endl;
      out << std::string(indent + 2, ' ') << "chroma_sample_loc_type_bottom_field"
          << chromaSampleLocTypeBottomField << std::endl;
    }
    out << std::string(indent + 2, ' ') << "timing_info_present_flag: " << timingInfoPresentFlag << std::endl;
    if (timingInfoPresentFlag){
      out << std::string(indent + 2, ' ') << "num_units_in_tick: " << numUnitsInTick << std::endl;
      out << std::string(indent + 2, ' ') << "time_scale: " << timeScale << std::endl;
      out << std::string(indent + 2, ' ') << "fixed_frame_rate_flag: " << fixedFrameRateFlag << std::endl;
    }
    out << std::string(indent + 2, ' ')
        << "nal_hrd_parameters_present_flag: " << nalHrdParametersPresentFlag << std::endl;

    out << std::string(indent + 2, ' ')
        << "vcl_hrd_parameters_present_flag: " << vclHrdParametersPresentFlag << std::endl;
    if (nalHrdParametersPresentFlag || vclHrdParametersPresentFlag){
      out << std::string(indent + 2, ' ') << "low_delay_hrd_flag: " << lowDelayHrdFlag << std::endl;
    }
    out << std::string(indent + 2, ' ') << "pic_struct_present_flag: " << picStructPresentFlag << std::endl;
    out << std::string(indent + 2, ' ') << "bitstream_restiction_flag: " << bitstreamRestrictionFlag
        << std::endl;
    if (bitstreamRestrictionFlag){
      out << std::string(indent + 2, ' ')
          << "motion_vectors_over_pic_boundaries_flag: " << motionVectorsOverPicBoundariesFlag << std::endl;
      out << std::string(indent + 2, ' ') << "max_bytes_per_pic_denom: " << maxBytesPerPicDenom << std::endl;
      out << std::string(indent + 2, ' ') << "max_bits_per_mb_denom: " << maxBitsPerMbDenom << std::endl;
      out << std::string(indent + 2, ' ')
          << "log2_max_mv_length_horizontal: " << log2MaxMvLengthHorizontal << std::endl;
      out << std::string(indent + 2, ' ')
          << "log2_max_mv_length_vertical: " << log2MaxMvLengthVertical << std::endl;
      out << std::string(indent + 2, ' ') << "num_reorder_frames: " << numReorderFrames << std::endl;
      out << std::string(indent + 2, ' ') << "max_dec_frame_buffering: " << maxDecFrameBuffering << std::endl;
    }
  }
}// namespace h264
