#include "h265.h"
#include "bitfields.h"
#include "defines.h"

namespace h265 {
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

  spsUnit::spsUnit(const std::string & _data){
    data = nalu::removeEmulationPrevention(_data);
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
