#include "h265.h"
#include "bitfields.h"
#include "defines.h"

namespace h265 {
  const char * typeToStr(uint8_t type){
    switch (type){
      case 0:
      case 1: return "Trailing slice";
      case 2:
      case 3: return "TSA slice";
      case 4:
      case 5: return "STSA slice";
      case 6:
      case 7: return "Decodable leading slice";
      case 8:
      case 9: return "Skipped leading slice";
      case 16:
      case 17:
      case 18: return "BLA slice";
      case 19:
      case 20: return "IDR (keyframe) slice";
      case 21: return "CRA slice";
      case 32: return "VPS";
      case 33: return "SPS";
      case 34: return "PPS";
      case 35: return "Access unit delimiter";
      case 36: return "End of sequence";
      case 37: return "End of bitstream";
      case 38: return "Filler data";
      case 39:
      case 40: return "SEI";
      case 48: return "RTP Aggregation Packet";
      case 49: return "RTP Fragmentation Unit";
      case 50: return "RTP PAyload Content Information (PACI)";
      default: return "UNKNOWN";
    }
  }

  bool isKeyframe(const char * data, uint32_t len){
    if (!len){return false;}
    uint8_t nalType = (data[0] & 0x7E) >> 1;
    return (nalType >= 16 && nalType <= 21);
  }

  std::deque<nalu::nalData> analysePackets(const char * data, unsigned long len){
    std::deque<nalu::nalData> res;

    int offset = 0;
    while (offset < len){
      nalu::nalData entry;
      entry.nalSize = Bit::btohl(data + offset);
      entry.nalType = ((data + offset)[4] & 0x7E) >> 1;
      res.push_back(entry);
      offset += entry.nalSize + 4;
    }
    return res;
  }

  initData::initData() {}

  initData::initData(const std::string& hvccData) {
    MP4::HVCC hvccBox;
    hvccBox.setPayload(hvccData);
    std::deque<MP4::HVCCArrayEntry> arrays = hvccBox.getArrays();
    for (std::deque<MP4::HVCCArrayEntry>::iterator it = arrays.begin(); it != arrays.end(); it++){
      for (std::deque<std::string>::iterator nalIt = it->nalUnits.begin(); nalIt != it->nalUnits.end(); nalIt++){
        nalUnits[it->nalUnitType].insert(*nalIt);
      }
    }
  }

  const std::set<std::string> & initData::getVPS() const{
    static std::set<std::string> empty;
    if (!nalUnits.count(32)){return empty;}
    return nalUnits.at(32);
  }

  const std::set<std::string> & initData::getSPS() const{
    static std::set<std::string> empty;
    if (!nalUnits.count(33)){return empty;}
    return nalUnits.at(33);
  }

  const std::set<std::string> & initData::getPPS() const{
    static std::set<std::string> empty;
    if (!nalUnits.count(34)){return empty;}
    return nalUnits.at(34);
  }


  void initData::addUnit(char * data) {
    unsigned long nalSize = Bit::btohl(data);
    unsigned long nalType = (data[4] & 0x7E) >> 1;
    switch (nalType) {
      case 32: //vps
      case 33: //sps
      case 34: //pps
        nalUnits[nalType].insert(std::string(data + 4, nalSize));
    }
  }

  void initData::addUnit(const std::string & data) {
    if (data.size() <= 1){return;}
    unsigned long nalType = (data[0] & 0x7E) >> 1;
    switch (nalType) {
      case 32: //vps
      case 33: //sps
      case 34: //pps
        nalUnits[nalType].insert(data);
    }
    INFO_MSG("added nal of type %u" , nalType);
    if (nalType == 32){
      vpsUnit vps(data);
      std::cout << vps.toPrettyString(0);
    }
  }

  bool initData::haveRequired() {
    return (nalUnits.count(32) && nalUnits.count(33) && nalUnits.count(34));
  }
  
  std::string initData::generateHVCC(){
    MP4::HVCC hvccBox;
    hvccBox.setConfigurationVersion(1);
    hvccBox.setParallelismType(0);
    std::set<std::string>::iterator nalIt;

    //We first loop over all VPS Units
    for (nalIt = nalUnits[32].begin(); nalIt != nalUnits[32].end(); nalIt++){
      vpsUnit vps(*nalIt);
      vps.updateHVCC(hvccBox);
    }
    for (nalIt = nalUnits[33].begin(); nalIt != nalUnits[33].end(); nalIt++){
      spsUnit sps(*nalIt);
      sps.updateHVCC(hvccBox);
    }
    //NOTE: We dont parse the ppsUnit, as the only information it contains is parallelism mode, which is set to 0 for 'unknown'
    std::deque<MP4::HVCCArrayEntry> hvccArrays;
    hvccArrays.resize(3);
    hvccArrays[0].arrayCompleteness = 0;
    hvccArrays[0].nalUnitType = 32;
    for (nalIt = nalUnits[32].begin(); nalIt != nalUnits[32].end(); nalIt++){
      hvccArrays[0].nalUnits.push_back(*nalIt);
    }
    hvccArrays[1].arrayCompleteness = 0;
    hvccArrays[1].nalUnitType = 33;
    for (nalIt = nalUnits[33].begin(); nalIt != nalUnits[33].end(); nalIt++){
      hvccArrays[1].nalUnits.push_back(*nalIt);
    }
    hvccArrays[2].arrayCompleteness = 0;
    hvccArrays[2].nalUnitType = 34;
    for (nalIt = nalUnits[34].begin(); nalIt != nalUnits[34].end(); nalIt++){
      hvccArrays[2].nalUnits.push_back(*nalIt);
    }
    hvccBox.setArrays(hvccArrays);
    hvccBox.setLengthSizeMinus1(3);
    return std::string(hvccBox.payload(), hvccBox.payloadSize());
  }

  metaInfo initData::getMeta() {
    metaInfo res;
    if (!nalUnits.count(33)){
      return res;
    }
    spsUnit sps(*nalUnits[33].begin());
    sps.getMeta(res);
    return res;
  }

  void skipProfileTierLevel(Utils::bitstream & bs, unsigned int maxSubLayersMinus1){
    bs.skip(8);
    bs.skip(32);//general_profile_flags
    bs.skip(4);
    bs.skip(44);//reserverd_zero
    bs.skip(8);
    std::deque<bool> profilePresent;
    std::deque<bool> levelPresent;
    for (size_t i = 0; i < maxSubLayersMinus1; i++){
      profilePresent.push_back(bs.get(1));
      levelPresent.push_back(bs.get(1));
    }
    if (maxSubLayersMinus1){
      for (int i = maxSubLayersMinus1; i < 8; i++){
        bs.skip(2);
      }
    }
    for (int i = 0; i < maxSubLayersMinus1; i++){
      if (profilePresent[i]){
        bs.skip(8);
        bs.skip(32);//sub_layer_profile_flags
        bs.skip(4);
        bs.skip(44);//reserved_zero
      }
      if (levelPresent[i]){
        bs.skip(8);
      }
    }
  }

  std::string printProfileTierLevel(Utils::bitstream & bs, unsigned int maxSubLayersMinus1, size_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "general_profile_space: " << bs.get(2) << std::endl;
    r << std::string(indent, ' ') << "general_tier_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "general_profile_idc: " << bs.get(5) << std::endl;
    r << std::string(indent, ' ') << "general_profile_compatibility_flags: 0x" << std::hex << bs.get(32) << std::dec << std::endl;
    r << std::string(indent, ' ') << "general_progressive_source_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "general_interlaced_source_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "general_non_packed_constraint_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "general_frame_only_constraint_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "general_reserved_zero_44bits: " << bs.get(44) << std::endl;
    r << std::string(indent, ' ') << "general_level_idc: " << bs.get(8) << std::endl;
    std::deque<bool> profilePresent;
    std::deque<bool> levelPresent;
    for (size_t i = 0; i < maxSubLayersMinus1; i++){
      bool profile = bs.get(1);
      bool level = bs.get(1);
      profilePresent.push_back(profile);
      levelPresent.push_back(level);
      r << std::string(indent + 1, ' ') << "sub_layer_profile_present_flag[" << i << "]: " << (profile ? 1 : 0) << std::endl;
      r << std::string(indent + 1, ' ') << "sub_layer_level_present_flag[" << i << "]: " << (level ? 1 : 0) << std::endl;
    }

    if (maxSubLayersMinus1){
      for (int i = maxSubLayersMinus1; i < 8; i++){
        r << std::string(indent + 1, ' ') << "reserver_zero_2_bits[" << i << "]: " << bs.get(2) << std::endl;
      }
    }
    for (int i = 0; i < maxSubLayersMinus1; i++){
      r << std::string(indent + 1, ' ') << "sub_layer[" << i << "]:" << std::endl;
      if (profilePresent[i]){
        r << std::string(indent + 2, ' ') << "sub_layer_profile_space: " << bs.get(2) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_tier_flag: " << bs.get(1) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_profile_idc: " << bs.get(5) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_profile_compatibility_flags: " << std::hex << bs.get(32) << std::dec << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_progressive_source_flag: " << bs.get(1) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_interlaced_source_flag: " << bs.get(1) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_non_packed_constraint_flag: " << bs.get(1) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_frame_only_constraint_flag: " << bs.get(1) << std::endl;
        r << std::string(indent + 2, ' ') << "sub_layer_reserved_zero_44bits: " << bs.get(44) << std::endl;
      }
      if (levelPresent[i]){
        r << std::string(indent + 2, ' ') << "sub_layer_level_idc: " << bs.get(8) << std::endl;
      }
    }
    return r.str();
  }

  void updateProfileTierLevel(Utils::bitstream & bs, MP4::HVCC & hvccBox, unsigned int maxSubLayersMinus1){
    hvccBox.setGeneralProfileSpace(bs.get(2));

    unsigned int tierFlag = bs.get(1);
    hvccBox.setGeneralProfileIdc(std::max((unsigned long long)hvccBox.getGeneralProfileIdc(), bs.get(5)));
    hvccBox.setGeneralProfileCompatibilityFlags(hvccBox.getGeneralProfileCompatibilityFlags() & bs.get(32));
    hvccBox.setGeneralConstraintIndicatorFlags(hvccBox.getGeneralConstraintIndicatorFlags() & bs.get(48));
    unsigned int levelIdc = bs.get(8);

    if (tierFlag && !hvccBox.getGeneralTierFlag()) {
      hvccBox.setGeneralLevelIdc(levelIdc);
    }else {
      hvccBox.setGeneralLevelIdc(std::max((unsigned int)hvccBox.getGeneralLevelIdc(),levelIdc));
    }
    hvccBox.setGeneralTierFlag(tierFlag || hvccBox.getGeneralTierFlag());


    //Remainder is for synchronsation of the parser
    std::deque<bool> profilePresent;
    std::deque<bool> levelPresent;

    for (int i = 0; i < maxSubLayersMinus1; i++){
      profilePresent.push_back(bs.get(1));
      levelPresent.push_back(bs.get(1));
    }

    if (maxSubLayersMinus1){
      for (int i = maxSubLayersMinus1; i < 8; i++){
        bs.skip(2);
      }
    }

    for (int i = 0; i < maxSubLayersMinus1; i++){
      if (profilePresent[i]){
        bs.skip(32);
        bs.skip(32);
        bs.skip(24);
      }
      if (levelPresent[i]){
        bs.skip(8);
      }
    }
  }

  vpsUnit::vpsUnit(const std::string & _data){
    data = nalu::removeEmulationPrevention(_data);
  }

  void vpsUnit::updateHVCC(MP4::HVCC & hvccBox) {
    Utils::bitstream bs;
    bs.append(data);
    bs.skip(16);//Nal Header
    bs.skip(12);

    unsigned int maxSubLayers = bs.get(3) + 1;

    hvccBox.setNumberOfTemporalLayers(std::max((unsigned int)hvccBox.getNumberOfTemporalLayers(), maxSubLayers));

    bs.skip(17);

    updateProfileTierLevel(bs, hvccBox, maxSubLayers - 1);
  }

  std::string vpsUnit::toPrettyString(size_t indent){
    Utils::bitstream bs;
    bs.append(data);
    bs.skip(16);//Nal Header
    std::stringstream r;
    r << std::string(indent, ' ') << "vps_video_parameter_set_id: " << bs.get(4) << std::endl;
    r << std::string(indent, ' ') << "vps_reserved_three_2bits: " << bs.get(2) << std::endl;
    r << std::string(indent, ' ') << "vps_max_layers_minus1: " << bs.get(6) << std::endl;
    unsigned int maxSubLayersMinus1 = bs.get(3);
    r << std::string(indent, ' ') << "vps_max_sub_layers_minus1: " << maxSubLayersMinus1 << std::endl;
    r << std::string(indent, ' ') << "vps_temporal_id_nesting_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "vps_reserved_0xffff_16bits: " << std::hex << bs.get(16) << std::dec << std::endl;
    r << std::string(indent, ' ') << "profile_tier_level(): " << std::endl << printProfileTierLevel(bs, maxSubLayersMinus1, indent + 1);
    bool sub_layer_ordering_info = bs.get(1);
    r << std::string(indent, ' ') << "vps_sub_layer_ordering_info_present_flag: " << sub_layer_ordering_info << std::endl;
    for (int i = (sub_layer_ordering_info ? 0 : maxSubLayersMinus1); i <= maxSubLayersMinus1; i++){
      r << std::string(indent, ' ') << "vps_max_dec_pic_buffering_minus1[" << i << "]: " << bs.getUExpGolomb() << std::endl;
      r << std::string(indent, ' ') << "vps_max_num_reorder_pics[" << i << "]: " << bs.getUExpGolomb() << std::endl;
      r << std::string(indent, ' ') << "vps_max_latency_increase_plus1[" << i << "]: " << bs.getUExpGolomb() << std::endl;
    }
    unsigned int vps_max_layer_id = bs.get(6);
    uint64_t vps_num_layer_sets_minus1 = bs.getUExpGolomb();
    r << std::string(indent, ' ') << "vps_max_layer_id: " << vps_max_layer_id << std::endl;
    r << std::string(indent, ' ') << "vps_num_layer_sets_minus1: " << vps_num_layer_sets_minus1 << std::endl;
    for (int i = 1; i <= vps_num_layer_sets_minus1; i++){
      for (int j = 0; j < vps_max_layer_id; j++){
        r << std::string(indent, ' ') << "layer_id_included_flag[" << i << "][" << j << "]: " << bs.get(1) << std::endl;
      }
    }
    bool vps_timing_info = bs.get(1);
    r << std::string(indent, ' ') << "vps_timing_info_present_flag: " << (vps_timing_info ? 1 : 0) << std::endl;

    return r.str();
  }

  spsUnit::spsUnit(const std::string & _data){
    data = nalu::removeEmulationPrevention(_data);
  }

  void skipShortTermRefPicSet(Utils::bitstream & bs, unsigned int idx, size_t count){
    static std::map<int,int> negativePics;
    static std::map<int,int> positivePics;
    if (idx == 0){
      negativePics.clear();
      positivePics.clear();
    }
    bool interPrediction = false;
    if (idx != 0){
      interPrediction = bs.get(1);
    }
    if (interPrediction){
      uint64_t deltaIdxMinus1 = 0;
      if (idx == count){
        deltaIdxMinus1 = bs.getUExpGolomb();
      }
      bs.skip(1);
      bs.getUExpGolomb();
      uint64_t refRpsIdx = idx - deltaIdxMinus1 - 1;
      uint64_t deltaPocs = negativePics[refRpsIdx] + positivePics[refRpsIdx];
      for (int j = 0; j < deltaPocs; j++){
        bool usedByCurrPicFlag = bs.get(1);
        if (!usedByCurrPicFlag){
          bs.skip(1);
        }
      }
    }else{
      negativePics[idx]  = bs.getUExpGolomb();
      positivePics[idx] = bs.getUExpGolomb();
      for (int i = 0; i < negativePics[idx]; i++){
        bs.getUExpGolomb();
        bs.skip(1);
      }
      for (int i = 0; i < positivePics[idx]; i++){
        bs.getUExpGolomb();
        bs.skip(1);
      }
    }
  }

  std::string printShortTermRefPicSet(Utils::bitstream & bs, unsigned int idx, size_t indent){
    std::stringstream r;
    bool interPrediction = false;
    if (idx != 0){
      interPrediction = bs.get(1);
      r << std::string(indent, ' ') << "inter_ref_pic_set_predicition_flag: " << (interPrediction ? 1 : 0) << std::endl;
    }
    if (interPrediction){
      WARN_MSG("interprediciton not yet handled");
    }else{
      uint64_t negativePics = bs.getUExpGolomb();
      uint64_t positivePics = bs.getUExpGolomb();
      r << std::string(indent, ' ') << "num_negative_pics: " << negativePics << std::endl;
      r << std::string(indent, ' ') << "num_positive_pics: " << positivePics << std::endl;
      for (int i = 0; i < negativePics; i++){
        r << std::string(indent + 1, ' ') << "delta_poc_s0_minus1[" << i << "]: " << bs.getUExpGolomb() << std::endl;
        r << std::string(indent + 1, ' ') << "used_by_curr_pic_s0_flag[" << i << "]: " << bs.get(1) << std::endl;
      }
      for (int i = 0; i < positivePics; i++){
        r << std::string(indent + 1, ' ') << "delta_poc_s1_minus1[" << i << "]: " << bs.getUExpGolomb() << std::endl;
        r << std::string(indent + 1, ' ') << "used_by_curr_pic_s1_flag[" << i << "]: " << bs.get(1) << std::endl;
      }
    }
    return r.str();
  }

  void parseVuiParameters(Utils::bitstream & bs, metaInfo & res){
    bool aspectRatio = bs.get(1);
    if (aspectRatio){
      uint16_t aspectRatioIdc = bs.get(8);
      if (aspectRatioIdc == 255){
        bs.skip(32);
      }
    }
    bool overscanInfo = bs.get(1);
    if (overscanInfo){
      bs.skip(1);
    }
    bool videoSignalTypePresent = bs.get(1);
    if (videoSignalTypePresent){
      bs.skip(4);
      bool colourDescription = bs.get(1);
      if (colourDescription){
        bs.skip(24);
      }
    }
    bool chromaLocPresent = bs.get(1);
    if (chromaLocPresent){
      bs.getUExpGolomb();
      bs.getUExpGolomb();
    }
    bs.skip(3);
    bool defaultDisplay = bs.get(1);
    if (defaultDisplay){
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
    }
    bool timingFlag = bs.get(1);
    if (timingFlag){
      uint32_t unitsInTick = bs.get(32);
      uint32_t timescale = bs.get(32);
      res.fps = (double)timescale / unitsInTick;
    }
  }

  std::string printVuiParameters(Utils::bitstream & bs, size_t indent){
    std::stringstream r;
    bool aspectRatio = bs.get(1);
    r << std::string(indent, ' ') << "aspect_ratio_info_present_flag: " << (aspectRatio ? 1 : 0) << std::endl;
    if (aspectRatio){
      uint16_t aspectRatioIdc = bs.get(8);
      r << std::string(indent, ' ') << "aspect_ratio_idc: " << aspectRatioIdc << std::endl;
      if (aspectRatioIdc == 255){
        r << std::string(indent, ' ') << "sar_width: " << bs.get(16) << std::endl; 
        r << std::string(indent, ' ') << "sar_height: " << bs.get(16) << std::endl; 
      }
    }
    return r.str();
  }

  void skipScalingList(Utils::bitstream & bs){
    for (int sizeId = 0; sizeId < 4; sizeId++){
      for (int matrixId = 0; matrixId < (sizeId == 3 ? 2 : 6); matrixId++){
        bool modeFlag = bs.get(1);
        if (!modeFlag){
          bs.getUExpGolomb();
        }else{
          size_t coefNum = std::min(64, (1 << (4 + (sizeId << 1))));
          if (sizeId > 1){
            bs.getExpGolomb();
          }
          for (int i = 0; i < coefNum; i++){
            bs.getExpGolomb();
          }
        }
      }
    }
  }

  void spsUnit::getMeta(metaInfo & res) {
    Utils::bitstream bs;
    bs.append(data);
    bs.skip(16);//Nal Header
    bs.skip(4);
    unsigned int maxSubLayersMinus1 = bs.get(3);
    bs.skip(1);
    skipProfileTierLevel(bs, maxSubLayersMinus1);
    bs.getUExpGolomb();
    uint64_t chromaFormatIdc = bs.getUExpGolomb();
    bool separateColorPlane = false;
    if (chromaFormatIdc == 3){
      separateColorPlane = bs.get(1);
    }
    res.width = bs.getUExpGolomb();
    res.height = bs.getUExpGolomb();
    bool conformanceWindow = bs.get(1);
    if (conformanceWindow){
      uint8_t subWidthC = ((chromaFormatIdc == 1 || chromaFormatIdc == 2) ? 2 : 1);
      uint8_t subHeightC = (chromaFormatIdc == 1 ? 2 : 1);
      uint64_t left = bs.getUExpGolomb();
      uint64_t right = bs.getUExpGolomb();
      uint64_t top = bs.getUExpGolomb();
      uint64_t bottom = bs.getUExpGolomb();
      res.width -= (subWidthC * right); 
      res.height -= (subHeightC * bottom);
    }
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    uint64_t log2MaxPicOrderCntLsbMinus4 = bs.getUExpGolomb();
    bool subLayerOrdering = bs.get(1);
    for (int i= (subLayerOrdering ? 0 : maxSubLayersMinus1); i <= maxSubLayersMinus1; i++){
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
    }
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bool scalingListEnabled = bs.get(1);
    if (scalingListEnabled){
      bool scalingListPresent = bs.get(1);
      if (scalingListPresent){
        skipScalingList(bs);
      }
    }
    bs.skip(2);
    bool pcmEnabled = bs.get(1);
    if (pcmEnabled){
      bs.skip(8);
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.skip(1);
    }
    uint64_t shortTermPicSets = bs.getUExpGolomb();
    for (int i= 0; i < shortTermPicSets; i++){
      skipShortTermRefPicSet(bs, i, shortTermPicSets);
    }
    bool longTermRefPics = bs.get(1);
    if (longTermRefPics){
      uint64_t numLongTermPics = bs.getUExpGolomb();
      for (int i = 0; i < numLongTermPics; i++){
        bs.skip(log2MaxPicOrderCntLsbMinus4 + 4);
        bs.skip(1);
      }
    }
    bs.skip(2);
    bool vuiParams = bs.get(1);
    if (vuiParams){
      parseVuiParameters(bs, res);
    }
  }

  std::string spsUnit::toPrettyString(size_t indent){
    Utils::bitstream bs;
    bs.append(data);
    bs.skip(16);//Nal Header
    std::stringstream r;
    r << std::string(indent, ' ') << "sps_video_parameter_set_id: " << bs.get(4) << std::endl;
    unsigned int maxSubLayersMinus1 = bs.get(3);
    r << std::string(indent, ' ') << "sps_max_sub_layers_minus1: " << maxSubLayersMinus1 << std::endl;
    r << std::string(indent, ' ') << "sps_temporal_id_nesting_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "profile_tier_level(): " << std::endl << printProfileTierLevel(bs, maxSubLayersMinus1, indent + 1);
    r << std::string(indent, ' ') << "sps_seq_parameter_set_id: " << bs.getUExpGolomb() << std::endl;
    uint64_t chromaFormatIdc = bs.getUExpGolomb();
    r << std::string(indent, ' ') << "chroma_format_idc: " << chromaFormatIdc << std::endl;
    if (chromaFormatIdc == 3){
      r << std::string(indent, ' ') << "separate_colour_plane_flag: " << bs.get(1) << std::endl;
    }
    r << std::string(indent, ' ') << "pic_width_in_luma_samples: " << bs.getUExpGolomb() << std::endl;
    r << std::string(indent, ' ') << "pic_height_in_luma_samples: " << bs.getUExpGolomb() << std::endl;
    bool conformance_window_flag = bs.get(1);
    r << std::string(indent, ' ') << "conformance_window_flag: " << conformance_window_flag << std::endl;
    if (conformance_window_flag){
      r << std::string(indent, ' ') << "conf_window_left_offset: " << bs.getUExpGolomb() << std::endl;
      r << std::string(indent, ' ') << "conf_window_right_offset: " << bs.getUExpGolomb() << std::endl;
      r << std::string(indent, ' ') << "conf_window_top_offset: " << bs.getUExpGolomb() << std::endl;
      r << std::string(indent, ' ') << "conf_window_bottom_offset: " << bs.getUExpGolomb() << std::endl;
    }
    r << std::string(indent, ' ') << "bit_depth_luma_minus8: " << bs.getUExpGolomb() << std::endl;
    r << std::string(indent, ' ') << "bit_depth_chroma_minus8: " << bs.getUExpGolomb() << std::endl;
    r << std::string(indent, ' ') << "log2_max_pic_order_cnt_lsb_minus4: " << bs.getUExpGolomb() << std::endl;
    bool subLayerOrdering = bs.get(1);
    r << std::string(indent, ' ') << "sps_sub_layer_ordering_info_present_flag: " << subLayerOrdering  << std::endl;
    for (int i= (subLayerOrdering ? 0 : maxSubLayersMinus1); i <= maxSubLayersMinus1; i++){
      r << std::string(indent + 1, ' ') << "sps_max_dec_pic_buffering_minus1[" << i << "]: " << bs.getUExpGolomb()  << std::endl;
      r << std::string(indent + 1, ' ') << "sps_max_num_reorder_pics[" << i << "]: " << bs.getUExpGolomb()  << std::endl;
      r << std::string(indent + 1, ' ') << "sps_max_latency_increase_plus1[" << i << "]: " << bs.getUExpGolomb()  << std::endl;
    }
    r << std::string(indent, ' ') << "log2_min_luma_coding_block_size_minus3: " << bs.getUExpGolomb()  << std::endl;
    r << std::string(indent, ' ') << "log2_diff_max_min_luma_coding_block_size: " << bs.getUExpGolomb()  << std::endl;
    r << std::string(indent, ' ') << "log2_min_transform_block_size_minus2: " << bs.getUExpGolomb()  << std::endl;
    r << std::string(indent, ' ') << "log2_diff_max_min_transform_block_size: " << bs.getUExpGolomb()  << std::endl;
    r << std::string(indent, ' ') << "max_transform_hierarchy_depth_inter: " << bs.getUExpGolomb()  << std::endl;
    r << std::string(indent, ' ') << "max_transform_hierarchy_depth_intra: " << bs.getUExpGolomb()  << std::endl;
    bool scalingListEnabled = bs.get(1);
    r << std::string(indent, ' ') << "scaling_list_enabled_flag: " << scalingListEnabled << std::endl;
    if (scalingListEnabled){
      WARN_MSG("Not implemented scaling list in HEVC sps");
    }
    r << std::string(indent, ' ') << "amp_enabled_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "sample_adaptive_offset_enabled_flag: " << bs.get(1) << std::endl;
    bool pcmEnabled = bs.get(1);
    r << std::string(indent, ' ') << "pcm_enabled_flag: " << pcmEnabled << std::endl;
    if (pcmEnabled){
      WARN_MSG("Not implemented pcm_enabled in HEVC sps");
    }
    uint64_t shortTermPicSets = bs.getUExpGolomb();
    r << std::string(indent, ' ') << "num_short_term_ref_pic_sets: " << shortTermPicSets << std::endl;
    for (int i= 0; i < shortTermPicSets; i++){
      r << std::string(indent, ' ') << "short_term_ref_pic_set(" << i << "):" << std::endl << printShortTermRefPicSet(bs, i, indent + 1);

    }
    bool longTermRefPics = bs.get(1);
    r << std::string(indent, ' ') << "long_term_ref_pics_present_flag: " << (longTermRefPics ? 1 : 0) << std::endl;
    if (longTermRefPics){
      WARN_MSG("Implement longTermRefPics");
    }
    r << std::string(indent, ' ') << "sps_temporal_mvp_enabled_flag: " << bs.get(1) << std::endl;
    r << std::string(indent, ' ') << "strong_intra_smoothing_enabled_flag: " << bs.get(1) << std::endl;
    
    bool vuiParams = bs.get(1);
    r << std::string(indent, ' ') << "vui_parameters_present_flag: " << (vuiParams ? 1 : 0) << std::endl;
    if (vuiParams){
      r << std::string(indent, ' ') << "vui_parameters:" << std::endl << printVuiParameters(bs, indent + 1);

    }
    return r.str();
  }

  void spsUnit::updateHVCC(MP4::HVCC & hvccBox) {
    Utils::bitstream bs;
    bs.append(data);
    bs.skip(16);//Nal Header
    bs.skip(4);

    unsigned int maxSubLayers = bs.get(3) + 1;

    hvccBox.setNumberOfTemporalLayers(std::max((unsigned int)hvccBox.getNumberOfTemporalLayers(), maxSubLayers));
    hvccBox.setTemporalIdNested(bs.get(1));
    updateProfileTierLevel(bs, hvccBox, maxSubLayers - 1);

    bs.getUExpGolomb();

    hvccBox.setChromaFormat(bs.getUExpGolomb());

    if (hvccBox.getChromaFormat() == 3){
      bs.skip(1);
    }

    bs.getUExpGolomb();
    bs.getUExpGolomb();
    
    if (bs.get(1)){
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
    }

    hvccBox.setBitDepthLumaMinus8(bs.getUExpGolomb());
    hvccBox.setBitDepthChromaMinus8(bs.getUExpGolomb());

    int log2MaxPicOrderCntLsb = bs.getUExpGolomb() + 4;

    for (int i = bs.get(1) ? 0 : (maxSubLayers - 1); i < maxSubLayers; i++){
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.getUExpGolomb();
    }

    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();
    bs.getUExpGolomb();

    if (bs.get(1) && bs.get(1)){
      for (int i = 0; i < 4; i++){
        for (int j = 0; j < (i == 3 ? 2 : 6); j++){
          if (!bs.get(1)){
            bs.getUExpGolomb();
          }else{
            int numCoeffs = std::min(64, 1 << (4 + (i << 1)));
            if (i > 1){
              bs.getExpGolomb();
            }

            for (int k = 0; k < numCoeffs; k++){
              bs.getExpGolomb();
            }
          }
        }
      }
    }

    bs.skip(2);

    if (bs.get(1)){
      bs.skip(8);
      bs.getUExpGolomb();
      bs.getUExpGolomb();
      bs.skip(1);
    }

    unsigned long long shortTermRefPicSets = bs.getUExpGolomb();
    for (int i = 0; i < shortTermRefPicSets; i++){
      //parse rps, return if ret < 0
    }

    if (bs.get(1)){
      if (log2MaxPicOrderCntLsb > 16){
        log2MaxPicOrderCntLsb = 16;
      }
      int numLongTermRefPicsSps = bs.getUExpGolomb();
      for (int i = 0; i < numLongTermRefPicsSps; i++){
        bs.skip(log2MaxPicOrderCntLsb + 1);
      }
    }

    bs.skip(2);

    if (bs.get(1)){
      //parse vui
      if (bs.get(1) && bs.get(8) == 255){ bs.skip(32); }

      if (bs.get(1)){ bs.skip(1); }

      if (bs.get(1)){
        bs.skip(4);
        if (bs.get(1)){ bs.skip(24); }
      }

      if (bs.get(1)){
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }

      bs.skip(3);
      
      if (bs.get(1)){
        bs.getUExpGolomb();
        bs.getUExpGolomb();
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }

      if (bs.get(1)){
        bs.skip(32);
        bs.skip(32);
        if (bs.get(1)){
          bs.getUExpGolomb();
        }
        if (bs.get(1)){
          int nalHrd = bs.get(1);
          int vclHrd = bs.get(1);
          int subPicPresent = 0;
          if (nalHrd || vclHrd){
            subPicPresent = bs.get(1);
            if (subPicPresent){
              bs.skip(19);
            }
            bs.skip(8);
            if (subPicPresent){
              bs.skip(4);
            }
            bs.skip(15);
          }

          //
          for (int i = 0; i < maxSubLayers; i++){
            int cpbCnt = 1;
            int lowDelay = 0;
            int fixedRateCvs = 0;
            int fixedRateGeneral = bs.get(1);

            if (fixedRateGeneral){
              fixedRateCvs = bs.get(1);
            }

            if (fixedRateCvs){
              bs.getUExpGolomb();
            }else{
              lowDelay = bs.get(1);
            }

            if (!lowDelay){
              cpbCnt = bs.getUExpGolomb() + 1;
            }

            if (nalHrd){
              for (int i = 0; i < cpbCnt; i++){
                bs.getUExpGolomb();
                bs.getUExpGolomb();
                if (subPicPresent){
                  bs.getUExpGolomb();
                  bs.getUExpGolomb();
                }
                bs.skip(1);
              }
            }

            if (vclHrd){
              for (int i = 0; i < cpbCnt; i++){
                bs.getUExpGolomb();
                bs.getUExpGolomb();
                if (subPicPresent){
                  bs.getUExpGolomb();
                  bs.getUExpGolomb();
                }
                bs.skip(1);
              }
            }

          }
        }
      }

      if (bs.get(1)){
        bs.skip(3);
        int spatialSegmentIdc = bs.getUExpGolomb();
        hvccBox.setMinSpatialSegmentationIdc(std::min((int)hvccBox.getMinSpatialSegmentationIdc(),spatialSegmentIdc));
        bs.getUExpGolomb();
        bs.getUExpGolomb();
        bs.getUExpGolomb();
        bs.getUExpGolomb();
      }
    }
  }
}
