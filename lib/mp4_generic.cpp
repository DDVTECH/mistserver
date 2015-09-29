#include "mp4_generic.h"
#include "defines.h"

namespace MP4 {
  MFHD::MFHD() {
    memcpy(data + 4, "mfhd", 4);
    setInt32(0, 0);
  }

  void MFHD::setSequenceNumber(uint32_t newSequenceNumber) {
    setInt32(newSequenceNumber, 4);
  }

  uint32_t MFHD::getSequenceNumber() {
    return getInt32(4);
  }

  std::string MFHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[mfhd] Movie Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "SequenceNumber " << getSequenceNumber() << std::endl;
    return r.str();
  }

  MOOF::MOOF() {
    memcpy(data + 4, "moof", 4);
  }

  TRAF::TRAF() {
    memcpy(data + 4, "traf", 4);
  }

  uint32_t TRAF::getContentCount() {
    int res = 0;
    unsigned int tempLoc = 0;
    while (tempLoc < boxedSize() - 8) {
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  void TRAF::setContent(Box & newContent, uint32_t no) {
    int tempLoc = 0;
    unsigned int contentCount = getContentCount();
    for (unsigned int i = 0; i < no; i++) {
      if (i < contentCount) {
        tempLoc += getBoxLen(tempLoc);
      } else {
        if (!reserve(tempLoc, 0, (no - contentCount) * 8)) {
          return;
        };
        memset(data + tempLoc, 0, (no - contentCount) * 8);
        tempLoc += (no - contentCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
  }

  Box & TRAF::getContent(uint32_t no) {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (no > getContentCount()) {
      return ret;
    }
    unsigned int i = 0;
    int tempLoc = 0;
    while (i < no) {
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string TRAF::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[traf] Track Fragment Box (" << boxedSize() << ")" << std::endl;
    int contentCount = getContentCount();
    for (int i = 0; i < contentCount; i++) {
      Box curBox = Box(getContent(i).asBox(), false);
      r << curBox.toPrettyString(indent + 1);
    }
    return r.str();
  }

  TRUN::TRUN() {
    memcpy(data + 4, "trun", 4);
  }

  void TRUN::setFlags(uint32_t newFlags) {
    setInt24(newFlags, 1);
  }

  uint32_t TRUN::getFlags() {
    return getInt24(1);
  }

  void TRUN::setDataOffset(uint32_t newOffset) {
    if (getFlags() & trundataOffset) {
      setInt32(newOffset, 8);
    }
  }

  uint32_t TRUN::getDataOffset() {
    if (getFlags() & trundataOffset) {
      return getInt32(8);
    } else {
      return 0;
    }
  }

  void TRUN::setFirstSampleFlags(uint32_t newSampleFlags) {
    if (!(getFlags() & trunfirstSampleFlags)) {
      return;
    }
    if (getFlags() & trundataOffset) {
      setInt32(newSampleFlags, 12);
    } else {
      setInt32(newSampleFlags, 8);
    }
  }

  uint32_t TRUN::getFirstSampleFlags() {
    if (!(getFlags() & trunfirstSampleFlags)) {
      return 0;
    }
    if (getFlags() & trundataOffset) {
      return getInt32(12);
    } else {
      return getInt32(8);
    }
  }

  uint32_t TRUN::getSampleInformationCount() {
    return getInt32(4);
  }

  void TRUN::setSampleInformation(trunSampleInformation newSample, uint32_t no) {
    uint32_t flags = getFlags();
    uint32_t sampInfoSize = 0;
    if (flags & trunsampleDuration) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleSize) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleFlags) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleOffsets) {
      sampInfoSize += 4;
    }
    uint32_t offset = 8;
    if (flags & trundataOffset) {
      offset += 4;
    }
    if (flags & trunfirstSampleFlags) {
      offset += 4;
    }
    uint32_t innerOffset = 0;
    if (flags & trunsampleDuration) {
      setInt32(newSample.sampleDuration, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleSize) {
      setInt32(newSample.sampleSize, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleFlags) {
      setInt32(newSample.sampleFlags, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleOffsets) {
      setInt32(newSample.sampleOffset, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (getSampleInformationCount() < no + 1) {
      setInt32(no + 1, 4);
    }
  }

  trunSampleInformation TRUN::getSampleInformation(uint32_t no) {
    trunSampleInformation ret;
    ret.sampleDuration = 0;
    ret.sampleSize = 0;
    ret.sampleFlags = 0;
    ret.sampleOffset = 0;
    if (getSampleInformationCount() < no + 1) {
      return ret;
    }
    uint32_t flags = getFlags();
    uint32_t sampInfoSize = 0;
    if (flags & trunsampleDuration) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleSize) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleFlags) {
      sampInfoSize += 4;
    }
    if (flags & trunsampleOffsets) {
      sampInfoSize += 4;
    }
    uint32_t offset = 8;
    if (flags & trundataOffset) {
      offset += 4;
    }
    if (flags & trunfirstSampleFlags) {
      offset += 4;
    }
    uint32_t innerOffset = 0;
    if (flags & trunsampleDuration) {
      ret.sampleDuration = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleSize) {
      ret.sampleSize = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleFlags) {
      ret.sampleFlags = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleOffsets) {
      ret.sampleOffset = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    return ret;
  }

  std::string TRUN::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[trun] Track Fragment Run (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    uint32_t flags = getFlags();
    r << std::string(indent + 1, ' ') << "Flags";
    if (flags & trundataOffset) {
      r << " dataOffset";
    }
    if (flags & trunfirstSampleFlags) {
      r << " firstSampleFlags";
    }
    if (flags & trunsampleDuration) {
      r << " sampleDuration";
    }
    if (flags & trunsampleSize) {
      r << " sampleSize";
    }
    if (flags & trunsampleFlags) {
      r << " sampleFlags";
    }
    if (flags & trunsampleOffsets) {
      r << " sampleOffsets";
    }
    r << std::endl;

    if (flags & trundataOffset) {
      r << std::string(indent + 1, ' ') << "Data Offset " << getDataOffset() << std::endl;
    }
    if (flags & trundataOffset) {
      r << std::string(indent + 1, ' ') << "Sample Flags" << prettySampleFlags(getFirstSampleFlags()) << std::endl;
    }

    r << std::string(indent + 1, ' ') << "SampleInformation (" << getSampleInformationCount() << "):" << std::endl;
    for (unsigned int i = 0; i < getSampleInformationCount(); ++i) {
      r << std::string(indent + 2, ' ') << "[" << i << "] ";
      trunSampleInformation samp = getSampleInformation(i);
      if (flags & trunsampleDuration) {
        r << "Duration=" << samp.sampleDuration << " ";
      }
      if (flags & trunsampleSize) {
        r << "Size=" << samp.sampleSize << " ";
      }
      if (flags & trunsampleFlags) {
        r << "Flags=" << prettySampleFlags(samp.sampleFlags) << " ";
      }
      if (flags & trunsampleOffsets) {
        r << "Offset=" << samp.sampleOffset << " ";
      }
      r << std::endl;
    }

    return r.str();
  }

  std::string prettySampleFlags(uint32_t flag) {
    std::stringstream r;
    if (flag & noIPicture) {
      r << " noIPicture";
    }
    if (flag & isIPicture) {
      r << " isIPicture";
    }
    if (flag & noDisposable) {
      r << " noDisposable";
    }
    if (flag & isDisposable) {
      r << " isDisposable";
    }
    if (flag & isRedundant) {
      r << " isRedundant";
    }
    if (flag & noRedundant) {
      r << " noRedundant";
    }
    if (flag & noKeySample) {
      r << " noKeySample";
    } else {
      r << " isKeySample";
    }
    return r.str();
  }

  TFHD::TFHD() {
    memcpy(data + 4, "tfhd", 4);
  }

  void TFHD::setFlags(uint32_t newFlags) {
    setInt24(newFlags, 1);
  }

  uint32_t TFHD::getFlags() {
    return getInt24(1);
  }

  void TFHD::setTrackID(uint32_t newID) {
    setInt32(newID, 4);
  }

  uint32_t TFHD::getTrackID() {
    return getInt32(4);
  }

  void TFHD::setBaseDataOffset(uint64_t newOffset) {
    if (getFlags() & tfhdBaseOffset) {
      setInt64(newOffset, 8);
    }
  }

  uint64_t TFHD::getBaseDataOffset() {
    if (getFlags() & tfhdBaseOffset) {
      return getInt64(8);
    } else {
      return 0;
    }
  }

  void TFHD::setSampleDescriptionIndex(uint32_t newIndex) {
    if (!(getFlags() & tfhdSampleDesc)) {
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    setInt32(newIndex, offset);
  }

  uint32_t TFHD::getSampleDescriptionIndex() {
    if (!(getFlags() & tfhdSampleDesc)) {
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleDuration(uint32_t newDuration) {
    if (!(getFlags() & tfhdSampleDura)) {
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    setInt32(newDuration, offset);
  }

  uint32_t TFHD::getDefaultSampleDuration() {
    if (!(getFlags() & tfhdSampleDura)) {
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleSize(uint32_t newSize) {
    if (!(getFlags() & tfhdSampleSize)) {
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura) {
      offset += 4;
    }
    setInt32(newSize, offset);
  }

  uint32_t TFHD::getDefaultSampleSize() {
    if (!(getFlags() & tfhdSampleSize)) {
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura) {
      offset += 4;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleFlags(uint32_t newFlags) {
    if (!(getFlags() & tfhdSampleFlag)) {
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleSize) {
      offset += 4;
    }
    setInt32(newFlags, offset);
  }

  uint32_t TFHD::getDefaultSampleFlags() {
    if (!(getFlags() & tfhdSampleFlag)) {
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset) {
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura) {
      offset += 4;
    }
    if (getFlags() & tfhdSampleSize) {
      offset += 4;
    }
    return getInt32(offset);
  }

  std::string TFHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfhd] Track Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    uint32_t flags = getFlags();
    r << std::string(indent + 1, ' ') << "Flags";
    if (flags & tfhdBaseOffset) {
      r << " BaseOffset";
    }
    if (flags & tfhdSampleDesc) {
      r << " SampleDesc";
    }
    if (flags & tfhdSampleDura) {
      r << " SampleDura";
    }
    if (flags & tfhdSampleSize) {
      r << " SampleSize";
    }
    if (flags & tfhdSampleFlag) {
      r << " SampleFlag";
    }
    if (flags & tfhdNoDuration) {
      r << " NoDuration";
    }
    r << std::endl;

    r << std::string(indent + 1, ' ') << "TrackID " << getTrackID() << std::endl;

    if (flags & tfhdBaseOffset) {
      r << std::string(indent + 1, ' ') << "Base Offset " << getBaseDataOffset() << std::endl;
    }
    if (flags & tfhdSampleDesc) {
      r << std::string(indent + 1, ' ') << "Sample Description Index " << getSampleDescriptionIndex() << std::endl;
    }
    if (flags & tfhdSampleDura) {
      r << std::string(indent + 1, ' ') << "Default Sample Duration " << getDefaultSampleDuration() << std::endl;
    }
    if (flags & tfhdSampleSize) {
      r << std::string(indent + 1, ' ') << "Default Same Size " << getDefaultSampleSize() << std::endl;
    }
    if (flags & tfhdSampleFlag) {
      r << std::string(indent + 1, ' ') << "Default Sample Flags " << prettySampleFlags(getDefaultSampleFlags()) << std::endl;
    }

    return r.str();
  }


  AVCC::AVCC() {
    memcpy(data + 4, "avcC", 4);
    setInt8(0xFF, 4); //reserved + 4-bytes NAL length
  }

  void AVCC::setVersion(uint32_t newVersion) {
    setInt8(newVersion, 0);
  }

  uint32_t AVCC::getVersion() {
    return getInt8(0);
  }

  void AVCC::setProfile(uint32_t newProfile) {
    setInt8(newProfile, 1);
  }

  uint32_t AVCC::getProfile() {
    return getInt8(1);
  }

  void AVCC::setCompatibleProfiles(uint32_t newCompatibleProfiles) {
    setInt8(newCompatibleProfiles, 2);
  }

  uint32_t AVCC::getCompatibleProfiles() {
    return getInt8(2);
  }

  void AVCC::setLevel(uint32_t newLevel) {
    setInt8(newLevel, 3);
  }

  uint32_t AVCC::getLevel() {
    return getInt8(3);
  }

  void AVCC::setSPSNumber(uint32_t newSPSNumber) {
    setInt8(newSPSNumber, 5);
  }

  uint32_t AVCC::getSPSNumber() {
    return getInt8(5);
  }

  void AVCC::setSPS(std::string newSPS) {
    setInt16(newSPS.size(), 6);
    for (unsigned int i = 0; i < newSPS.size(); i++) {
      setInt8(newSPS[i], 8 + i);
    } //not null-terminated
  }

  uint32_t AVCC::getSPSLen() {
    return getInt16(6);
  }

  char * AVCC::getSPS() {
    return payload() + 8;
  }

  void AVCC::setPPSNumber(uint32_t newPPSNumber) {
    int offset = 8 + getSPSLen();
    setInt8(newPPSNumber, offset);
  }

  uint32_t AVCC::getPPSNumber() {
    int offset = 8 + getSPSLen();
    return getInt8(offset);
  }

  void AVCC::setPPS(std::string newPPS) {
    int offset = 8 + getSPSLen() + 1;
    setInt16(newPPS.size(), offset);
    for (unsigned int i = 0; i < newPPS.size(); i++) {
      setInt8(newPPS[i], offset + 2 + i);
    } //not null-terminated
  }

  uint32_t AVCC::getPPSLen() {
    int offset = 8 + getSPSLen() + 1;
    return getInt16(offset);
  }

  char * AVCC::getPPS() {
    int offset = 8 + getSPSLen() + 3;
    return payload() + offset;
  }

  std::string AVCC::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[avcC] H.264 Init Data (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Profile: " << getProfile() << std::endl;
    r << std::string(indent + 1, ' ') << "Compatible Profiles: " << getCompatibleProfiles() << std::endl;
    r << std::string(indent + 1, ' ') << "Level: " << getLevel() << std::endl;
    r << std::string(indent + 1, ' ') << "SPS Number: " << getSPSNumber() << std::endl;
    r << std::string(indent + 2, ' ') << getSPSLen() << " of SPS data" << std::endl;
    r << std::string(indent + 1, ' ') << "PPS Number: " << getPPSNumber() << std::endl;
    r << std::string(indent + 2, ' ') << getPPSLen() << " of PPS data" << std::endl;
    return r.str();
  }

  std::string AVCC::asAnnexB() {
    std::stringstream r;
    r << (char)0x00 << (char)0x00 << (char)0x00 << (char)0x01;
    r.write(getSPS(), getSPSLen());
    r << (char)0x00 << (char)0x00 << (char)0x00 << (char)0x01;
    r.write(getPPS(), getPPSLen());
    return r.str();
  }

  void AVCC::setPayload(std::string newPayload) {
    if (!reserve(0, payloadSize(), newPayload.size())) {
      DEBUG_MSG(DLVL_ERROR, "Cannot allocate enough memory for payload");
      return;
    }
    memcpy((char *)payload(), (char *)newPayload.c_str(), newPayload.size());
  }

  HVCC::HVCC() {
    memcpy(data + 4, "hvcC", 4);
  }

  void HVCC::setConfigurationVersion(char newVersion) {
    setInt8(newVersion, 0);
  }
  char HVCC::getConfigurationVersion() {
    return getInt8(0);
  }

  void HVCC::setGeneralProfileSpace(char newGeneral) {
    setInt8(((newGeneral << 6) & 0xC0) | (getInt8(1) & 0x3F), 1);
  }
  char HVCC::getGeneralProfileSpace(){
    return ((getInt8(1) >> 6) & 0x03);
  }

  void HVCC::setGeneralTierFlag(char newGeneral){
    setInt8(((newGeneral << 5) & 0x20) | (getInt8(1) & 0xDF), 1);
  }
  char HVCC::getGeneralTierFlag(){
    return ((getInt8(1) >> 5) & 0x01);
  }
  
  void HVCC::setGeneralProfileIdc(char newGeneral){
    setInt8((newGeneral & 0x1F) | (getInt8(1) & 0xE0), 1);
  }
  char HVCC::getGeneralProfileIdc(){
    return getInt8(1) & 0x1F;
  }

  void HVCC::setGeneralProfileCompatibilityFlags(unsigned long newGeneral){
    setInt32(newGeneral, 2);
  }
  unsigned long HVCC::getGeneralProfileCompatibilityFlags(){
    return getInt32(2);
  }
  
  void HVCC::setGeneralConstraintIndicatorFlags(unsigned long long newGeneral){
    setInt32((newGeneral >> 16) & 0x0000FFFF,6);
    setInt16(newGeneral & 0xFFFFFF, 10);
  }
  unsigned long long HVCC::getGeneralConstraintIndicatorFlags(){
    unsigned long long result = getInt32(6);
    result <<= 16;
    return result | getInt16(10);
  }
  
  void HVCC::setGeneralLevelIdc(char newGeneral){
    setInt8(newGeneral, 12);
  }
  char HVCC::getGeneralLevelIdc(){
    return getInt8(12);
  }

  void HVCC::setMinSpatialSegmentationIdc(short newIdc){
    setInt16(newIdc | 0xF000, 13);
  }
  short HVCC::getMinSpatialSegmentationIdc(){
    return getInt16(13) & 0x0FFF;
  }
  
  void HVCC::setParallelismType(char newType){
    setInt8(newType | 0xFC, 15);
  }
  char HVCC::getParallelismType(){
    return getInt8(15) & 0x03;
  }
  
  void HVCC::setChromaFormat(char newFormat){
    setInt8(newFormat | 0xFC, 16);
  }
  char HVCC::getChromaFormat(){
    return getInt8(16) & 0x03;
  }
  
  void HVCC::setBitDepthLumaMinus8(char newBitDepthLumaMinus8){
    setInt8(newBitDepthLumaMinus8 | 0xF8, 17);
  }
  char HVCC::getBitDepthLumaMinus8(){
    return getInt8(17) & 0x07;
  }

  void HVCC::setBitDepthChromaMinus8(char newBitDepthChromaMinus8){
    setInt8(newBitDepthChromaMinus8 | 0xF8, 18);
  }
  char HVCC::getBitDepthChromaMinus8(){
    return getInt8(18) & 0x07;
  }
  
  void setAverageFramerate(short newFramerate);
  short HVCC::getAverageFramerate(){
    return getInt16(19);
  }
  void setConstantFramerate(char newFramerate);
  char HVCC::getConstantFramerate(){
    return (getInt8(21) >> 6) & 0x03;
  }
  void setNumberOfTemporalLayers(char newNumber);
  char HVCC::getNumberOfTemporalLayers(){
    return (getInt8(21) >> 3) & 0x07;
  }
  void setTemporalIdNested(char newNested);
  char HVCC::getTemporalIdNested(){
    return (getInt8(21) >> 2) & 0x01;
  }
  void setLengthSizeMinus1(char newLengthSizeMinus1);
  char HVCC::getLengthSizeMinus1(){
    return getInt8(21) & 0x03;
  }

  std::deque<HVCCArrayEntry> HVCC::getArrays(){
    std::deque<HVCCArrayEntry> r;
    char arrayCount = getInt8(22);
    int offset = 23;
    for(int i = 0; i < arrayCount; i++){
      HVCCArrayEntry entry;
      entry.arrayCompleteness = ((getInt8(offset) >> 7) & 0x01);
      entry.nalUnitType = (getInt8(offset) & 0x3F);
      offset++;
      short naluCount = getInt16(offset);
      offset += 2;
      for (int j = 0; j < naluCount; j++){
        short naluSize = getInt16(offset);
        offset += 2;
        std::string nalu;
        for (int k = 0; k < naluSize; k++){
          nalu += (char)getInt8(offset++);
        }
        entry.nalUnits.push_back(nalu);
      }
      r.push_back(entry);
    }
    return r;
  }

  std::string HVCC::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[hvcC] H.265 Init Data (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Configuration Version: " << (int)getConfigurationVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "General Profile Space: " << (int)getGeneralProfileSpace() << std::endl;
    r << std::string(indent + 1, ' ') << "General Tier Flag: " << (int)getGeneralTierFlag() << std::endl;
    r << std::string(indent + 1, ' ') << "General Profile IDC: " << (int)getGeneralProfileIdc() << std::endl;
    r << std::string(indent + 1, ' ') << "General Profile Compatibility Flags: 0x" << std::hex << std::setw(8) << std::setfill('0') << getGeneralProfileCompatibilityFlags() << std::dec << std::endl;
    r << std::string(indent + 1, ' ') << "General Constraint Indicator Flags: 0x" << std::hex << std::setw(12) << std::setfill('0') << getGeneralConstraintIndicatorFlags() << std::dec << std::endl;
    r << std::string(indent + 1, ' ') << "General Level IDC: " << (int)getGeneralLevelIdc() << std::endl;
    r << std::string(indent + 1, ' ') << "Minimum Spatial Segmentation IDC: " << (int)getMinSpatialSegmentationIdc() << std::endl;
    r << std::string(indent + 1, ' ') << "Parallelism Type: " << (int)getParallelismType() << std::endl;
    r << std::string(indent + 1, ' ') << "Chroma Format: " << (int)getChromaFormat() << std::endl;
    r << std::string(indent + 1, ' ') << "Bit Depth Luma - 8: " << (int)getBitDepthLumaMinus8() << std::endl;
    r << std::string(indent + 1, ' ') << "Average Framerate: " << (int)getAverageFramerate() << std::endl;
    r << std::string(indent + 1, ' ') << "Constant Framerate: " << (int)getConstantFramerate() << std::endl;
    r << std::string(indent + 1, ' ') << "Number of Temporal Layers: " << (int)getNumberOfTemporalLayers() << std::endl;
    r << std::string(indent + 1, ' ') << "Temporal ID Nested: " << (int)getTemporalIdNested() << std::endl;
    r << std::string(indent + 1, ' ') << "Length Size - 1: " << (int)getLengthSizeMinus1() << std::endl;
    r << std::string(indent + 1, ' ') << "Arrays:" << std::endl;
    std::deque<HVCCArrayEntry> arrays = getArrays();
    for (unsigned int i = 0; i < arrays.size(); i++){
      r << std::string(indent + 2, ' ') << "Array with type " << (int)arrays[i].nalUnitType << std::endl;
      for (unsigned int j = 0; j < arrays[i].nalUnits.size(); j++){
        r << std::string(indent + 3, ' ') << "Nal unit of " << arrays[i].nalUnits[j].size() << " bytes" << std::endl;
      }
    }
    return r.str();
  }

  void HVCC::setPayload(std::string newPayload) {
    if (!reserve(0, payloadSize(), newPayload.size())) {
      DEBUG_MSG(DLVL_ERROR, "Cannot allocate enough memory for payload");
      return;
    }
    memcpy((char *)payload(), (char *)newPayload.c_str(), newPayload.size());
  }

  std::string HVCC::asAnnexB() {
    std::deque<HVCCArrayEntry> arrays = getArrays();
    std::stringstream r;
    for (unsigned int i = 0; i < arrays.size(); i++){
      for (unsigned int j = 0; j < arrays[i].nalUnits.size(); j++){
        r << (char)0x00 << (char)0x00 << (char)0x00 << (char)0x01 << arrays[i].nalUnits[j];
      }
    }
    return r.str();
  }

  Descriptor::Descriptor(){
    data = (char*)malloc(2);
    data[0] = 0;
    data[1] = 0;
    size = 2;
    master = true;
  }
  
  Descriptor::Descriptor(const char* p, const long unsigned s, const bool m){
    master = m;
    if (m){
      Descriptor();
      Descriptor tmp = Descriptor(p, s, false);
      resize(tmp.getDataSize());
      memcpy(data, p, s);
    }else{
      data = (char*)p;
      size = s;
    }
  }
  
  char Descriptor::getTag(){
    return data[0];
  }
  
  void Descriptor::setTag(char t){
    data[0] = t;
  }
  
  unsigned long Descriptor::getDataSize(){
    unsigned int i = 1;
    unsigned long s = 0;
    for (i = 1; i< size-1; i++){
      s <<= 7;
      s |= data[i] & 0x7f; 
      if ((data[i] & 0x80) != 0x80){
        break;
      }
    }
    return s;
  }
  
  unsigned long Descriptor::getFullSize(){
    unsigned long tmp = getDataSize();
    unsigned long r = tmp+2;
    if (tmp > 0x7F){
      ++r;
    }
    if (tmp > 0x3FFF){
      ++r;
    }
    if (tmp > 0x1FFFFF){
      ++r;
    }
    return r;
  }

  void Descriptor::resize(unsigned long t){
    if (!master){return;}
    unsigned long realLen = t+2;
    if (t > 0x7F){
      ++realLen;
    }
    if (t > 0x3FFF){
      ++realLen;
    }
    if (t > 0x1FFFFF){
      ++realLen;
    }
    if (size < realLen){
      char* tmpData = (char*)realloc(data,realLen);
      if (tmpData){
        size = realLen;
        data = tmpData;
        unsigned long offset = realLen-t;
        char continueBit = 0;
        while (realLen){
          data[--offset] = (0x7f & realLen) | continueBit;
          continueBit = 0x80;
          realLen >>= 7;
        }
      }else{
        return;
      }
    }
  }
  
  char* Descriptor::getData(){
    unsigned int i = 1;
    for (i = 1; i< size-1; i++){
      if ((data[i] & 0x80) != 0x80){
        break;
      }
    }
    return data + i + 1;
  }
  
  std::string Descriptor::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[" << (int)data[0] << "] Unimplemented descriptor (" << getDataSize() << ")" << std::endl;
    return r.str();
  }
  
  ESDescriptor::ESDescriptor (const char* p, const unsigned long l, const bool m) : Descriptor(p,l,m){
  }

  DCDescriptor ESDescriptor::getDecoderConfig(){
    char * p = getData();
    char * max_p = p + getDataSize();
    bool dep = (p[2] & 0x80);
    bool url = (p[2] & 0x40);
    bool ocr = (p[2] & 0x20);
    p += 3;
    if (dep){p += 2;}
    if (url){p += (1+p[0]);}
    if (ocr){p += 2;}
    return DCDescriptor(p, max_p-p);
  }

  std::string ESDescriptor::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[" << (int)data[0] << "] ES Descriptor (" << getDataSize() << ")" << std::endl;
    char * p = getData();
    char * max_p = p + getDataSize();
    r << std::string(indent+1, ' ') << "ES ID: " << (unsigned int)(((unsigned int)p[0] << 8) | (unsigned int)p[1]) << std::endl;
    bool dep = (p[2] & 0x80);//check pagina 28 ISO 1449-1 2001
    bool url = (p[2] & 0x40);
    bool ocr = (p[2] & 0x20);
    //6 & 86
    r << std::string(indent+1, ' ') << "Priority: " << int(p[2] & 0x1f) << std::endl;
    p += 3;
    if (dep){
      r << std::string(indent+1, ' ') << "Depends on ES ID: " << (unsigned int)(((unsigned int)p[0] << 8) | (unsigned int)p[1]) << std::endl;
      p += 2;
    }
    if (url){
      r << std::string(indent+1, ' ') << "URL: " << std::string(p+1, (unsigned int)p[0]) << std::endl;
      p += (1+p[0]);
    }
    if (ocr){
      r << std::string(indent+1, ' ') << "Timebase derived from ES ID: " << (unsigned int)(((unsigned int)p[0] << 8) | (unsigned int)p[1]) << std::endl;
      p += 2;
    }
    while (p < max_p){
      switch (p[0]){
        case 0x4: {
          DCDescriptor d(p, max_p - p);
          r << d.toPrettyString(indent+1);
          p += d.getFullSize();
        }
        default: {
          Descriptor d(p, max_p - p);
          r << d.toPrettyString(indent+1);
          p += d.getFullSize();
        }
      }
    }
    return r.str();
  }

  DCDescriptor::DCDescriptor (const char* p, const unsigned long l, const bool m) : Descriptor(p,l,m){
  }

  DSDescriptor DCDescriptor::getSpecific(){
    char * p = getData();
    char * max_p = p + getDataSize();
    p += 13;
    if (p[0] == 0x05){
      return DSDescriptor(p, max_p-p);
    }else{
      FAIL_MSG("Expected DSDescriptor (5), but found %d!", (int)p[0]);
      return DSDescriptor(0,0);
    }
  }

  bool DCDescriptor::isAAC(){
    return (getData()[0] == 0x40);
  }

  std::string DCDescriptor::getCodec(){
    switch(getData()[0]){
      case 0x40:
        return "AAC";
        break;
      case 0x69:
      case 0x6B:
        return "MP3";
        break;
      default:
        return "UNKNOWN";
    }
  }

  std::string DCDescriptor::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[" << (int)data[0] << "] DecoderConfig Descriptor (" << getDataSize() << ")" << std::endl;
    char * p = getData();
    char * max_p = p + getDataSize();
    int objType = p[0];
    r << std::string(indent+1, ' ') << "Object type: ";
    switch (objType){
      case 0x40: r << "AAC (0x40)"; break;
      case 0x69: r << "MP3 (0x69)"; break;
      default: r << "Unknown (" << objType << ")"; break;
    }
    r << std::endl;
    int streamType = (p[1] >> 2) & 0x3F;
    r << std::string(indent+1, ' ') << "Stream type: ";
    switch (streamType){
      case 0x4: r << "Video (4)"; break;
      case 0x5: r << "Audio (5)"; break;
      default: r << "Unknown (" << streamType << ")"; break;
    }
    r << std::endl;
    if (p[1] & 0x2){
      r << std::string(indent+1, ' ') << "Upstream" << std::endl;
    }
    r << std::string(indent+1, ' ') << "Buffer size: " << (int)(((int)p[2] << 16) | ((int)p[3] << 8) | ((int)p[4])) << std::endl;
    r << std::string(indent+1, ' ') << "Max bps: " << (unsigned int)(((unsigned int)p[5] << 24) | ((int)p[6] << 16) | ((int)p[7] << 8) | (int)p[8]) << std::endl;
    r << std::string(indent+1, ' ') << "Avg bps: " << (unsigned int)(((unsigned int)p[9] << 24) | ((int)p[10] << 16) | ((int)p[11] << 8) | (int)p[12]) << std::endl;
    p += 13;
    while (p < max_p){
      switch (p[0]){
        case 0x5: {
          DSDescriptor d(p, max_p - p);
          r << d.toPrettyString(indent+1);
          p += d.getFullSize();
        }
        default: {
          Descriptor d(p, max_p - p);
          r << d.toPrettyString(indent+1);
          p += d.getFullSize();
        }
      }
    }
    return r.str();
  }

  DSDescriptor::DSDescriptor (const char* p, const unsigned long l, const bool m) : Descriptor(p,l,m){
  }

  std::string DSDescriptor::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[" << (int)data[0] << "] Decoder Specific Info (" << getDataSize() << ")" << std::endl;
    char * p = getData();
    char * max_p = p + getDataSize();
    r << std::string(indent+1, ' ') << "Data: ";
    while (p < max_p) {
      r << std::hex << std::setw(2) << std::setfill('0') << (int)p[0] << std::dec;
      ++p;
    }
    r << std::endl;
    return r.str();
  }

  std::string DSDescriptor::toString(){
    if (!data){
      return "";
    }
    return std::string(getData(), getDataSize());
  }

  ESDS::ESDS() {
    memcpy(data + 4, "esds", 4);
  }

  ESDS::ESDS(std::string init) {
    ///\todo Do this better, in a non-hardcoded way.
    memcpy(data + 4, "esds", 4);
    reserve(payloadOffset, 0, init.size() ? init.size()+28 : 26);
    unsigned int i = 12;
    data[i++] = 3;//ES_DescrTag
    data[i++] = init.size() ? init.size()+23 : 21;//size
    data[i++] = 0;//es_id
    data[i++] = 2;//es_id
    data[i++] = 0;//priority
    data[i++] = 4;//DecoderConfigDescrTag
    data[i++] = init.size() ? init.size()+15 : 13;//size
    if (init.size()){
      data[i++] = 0x40;//objType AAC
    }else{
      data[i++] = 0x69;//objType MP3
    }
    data[i++] = 0x15;//streamType audio (5<<2 + 1)
    data[i++] = 0;//buffer size
    data[i++] = 0;//buffer size
    data[i++] = 0;//buffer size
    data[i++] = 0;//maxbps
    data[i++] = 0;//maxbps
    data[i++] = 0;//maxbps
    data[i++] = 0;//maxbps
    data[i++] = 0;//avgbps
    data[i++] = 0;//avgbps
    data[i++] = 0;//avgbps
    data[i++] = 0;//avgbps
    if (init.size()){
      data[i++] = 0x5;//DecSpecificInfoTag
      data[i++] = init.size();
      memcpy(data+i, init.data(), init.size());
      i += init.size();
    }
    data[i++] = 6;//SLConfigDescriptor
    data[i++] = 1;//size
    data[i++] = 2;//predefined, reserved for use in MP4 files
  }
 
  bool ESDS::isAAC(){
    return getESDescriptor().getDecoderConfig().isAAC();
  }
  
  std::string ESDS::getCodec(){
    return getESDescriptor().getDecoderConfig().getCodec();
  }
  
  std::string ESDS::getInitData(){
    return getESDescriptor().getDecoderConfig().getSpecific().toString();
  }
  
  ESDescriptor ESDS::getESDescriptor(){
    return ESDescriptor(data+12,boxedSize()-12);
  }

  std::string ESDS::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[esds] ES Descriptor Box (" << boxedSize() << ")" << std::endl;
    r << getESDescriptor().toPrettyString(indent+1);
    return r.str();
  }

  DAC3::DAC3(unsigned int rate, unsigned int channels){
    memcpy(data + 4, "dac3", 4);
    setInt24(0,0);

    setBitStreamIdentification(8);///\todo This is right now a default value. check the docs, this is a weird property
    setBitStreamMode(0);//main, mixed audio
    setAudioConfigMode(2);

    switch (rate) {
      case 48000:
        setSampleRateCode(0);
        break;
      case 44100:
        setSampleRateCode(1);
        break;
      case 32000:
        setSampleRateCode(2);
        break;
      default:
        setSampleRateCode(3);
        break;
    }

    if (channels > 4) {
      setLowFrequencyEffectsChannelOn(1);
    } else {
      setLowFrequencyEffectsChannelOn(0);
    }
    setFrameSizeCode(20);//should be OK, but test this.
  }

  char DAC3::getSampleRateCode(){
    return getInt8(0) >> 6;
  }
  
  void DAC3::setSampleRateCode(char newVal){
    setInt8(((newVal << 6) & 0xC0) | (getInt8(0) & 0x3F), 0);
  }

  char DAC3::getBitStreamIdentification(){
    return (getInt8(0) >> 1) & 0x1F;
  }

  void DAC3::setBitStreamIdentification(char newVal){
    setInt8(((newVal << 1) & 0x3E) | (getInt8(0) & 0xC1), 0);
  }

  char DAC3::getBitStreamMode(){
    return (getInt16(0) >> 6) & 0x7;
  }

  void DAC3::setBitStreamMode(char newVal){
    setInt16(((newVal & 0x7) << 6) | (getInt16(0) & 0xFE3F), 0);
  }

  char DAC3::getAudioConfigMode(){
    return (getInt8(1) >> 3) & 0x7;
  }

  void DAC3::setAudioConfigMode(char newVal){
    setInt8(((newVal & 0x7) << 3) | (getInt8(1) & 0x38),1);
  }

  bool DAC3::getLowFrequencyEffectsChannelOn(){
    return (getInt8(1) >> 2) & 0x1;
  }

  void DAC3::setLowFrequencyEffectsChannelOn(bool newVal){
    setInt8(((unsigned int)(newVal & 0x1) << 2) | (getInt8(1) & 0x4),1);
  }

  char DAC3::getFrameSizeCode(){
    return ((getInt8(1) & 0x3) << 4) | ((getInt8(2) & 0xF0) >> 4);
  }

  void DAC3::setFrameSizeCode(char newVal){
    setInt16(((newVal & 0x3F) << 4) | (getInt16(1) & 0x03F0),1);
  }

  std::string DAC3::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[dac3] D-AC3 box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "FSCOD: " << (int)getSampleRateCode() << std::endl;
    r << std::string(indent + 1, ' ') << "BSID: " << (int)getBitStreamIdentification() << std::endl;
    r << std::string(indent + 1, ' ') << "BSMOD: " << (int)getBitStreamMode() << std::endl;
    r << std::string(indent + 1, ' ') << "ACMOD: " << (int)getAudioConfigMode() << std::endl;
    r << std::string(indent + 1, ' ') << "LFEON: " << (int)getLowFrequencyEffectsChannelOn() << std::endl;
    r << std::string(indent + 1, ' ') << "FrameSizeCode: " << (int)getFrameSizeCode() << std::endl;
    return r.str();
  }

  FTYP::FTYP(bool fillDefaults){
    memcpy(data + 4, "ftyp", 4);
    if (fillDefaults){
      setMajorBrand("isom");
      setMinorVersion("\000\000\002\000");
      setCompatibleBrands("isom",0);
      setCompatibleBrands("iso2",1);
      setCompatibleBrands("avc1",2);
      setCompatibleBrands("mp41",3);
      setCompatibleBrands("Mist",4);
    }
  }
  
  void FTYP::setMajorBrand(const char * newMajorBrand){
    if (payloadOffset + 3 >= boxedSize()){
      if ( !reserve(payloadOffset, 0, 4)){
        return;
      }
    }
    memcpy(data + payloadOffset, newMajorBrand, 4);
  }

  std::string FTYP::getMajorBrand() {
    return std::string(data + payloadOffset, 4);
  }

  void FTYP::setMinorVersion(const char * newMinorVersion) {
    if (payloadOffset + 7 >= boxedSize()) {
      if (!reserve(payloadOffset + 4, 0, 4)) {
        return;
      }
    }
    memset(data + payloadOffset + 4, 0, 4);
    memcpy(data + payloadOffset + 4, newMinorVersion, 4);
  }
  
  std::string FTYP::getMinorVersion(){
    static char zero[4] = {0,0,0,0};
    if (memcmp(zero, data+payloadOffset+4, 4) == 0){
      return "";
    }
    return std::string(data+payloadOffset+4, 4);
  }

  size_t FTYP::getCompatibleBrandsCount() {
    return (payloadSize() - 8) / 4;
  }

  void FTYP::setCompatibleBrands(const char * newCompatibleBrand, size_t index) {
    if (payloadOffset + 8 + index * 4 + 3 >= boxedSize()) {
      if (!reserve(payloadOffset + 8 + index * 4, 0, 4)) {
        return;
      }
    }
    memcpy(data + payloadOffset + 8 + index * 4, newCompatibleBrand, 4);
  }

  std::string FTYP::getCompatibleBrands(size_t index) {
    if (index >= getCompatibleBrandsCount()) {
      return "";
    }
    return std::string(data + payloadOffset + 8 + (index * 4), 4);
  }

  std::string FTYP::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[ftyp] File Type (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "MajorBrand: " << getMajorBrand() << std::endl;
    r << std::string(indent + 1, ' ') << "MinorVersion: " << getMinorVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "CompatibleBrands (" << getCompatibleBrandsCount() << "):" << std::endl;
    for (unsigned int i = 0; i < getCompatibleBrandsCount(); i++) {
      r << std::string(indent + 2, ' ') << "[" << i << "] CompatibleBrand: " << getCompatibleBrands(i) << std::endl;
    }
    return r.str();
  }

  STYP::STYP(bool fillDefaults) : FTYP(fillDefaults) {
    memcpy(data + 4, "styp", 4);
  }
  
  std::string STYP::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[styp] Fragment Type (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "MajorBrand: " << getMajorBrand() << std::endl;
    r << std::string(indent + 1, ' ') << "MinorVersion: " << getMinorVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "CompatibleBrands (" << getCompatibleBrandsCount() << "):" << std::endl;
    for (unsigned int i = 0; i < getCompatibleBrandsCount(); i++){
      r << std::string(indent + 2, ' ') << "[" << i << "] CompatibleBrand: " << getCompatibleBrands(i) << std::endl;
    }
    return r.str();
  }

  MOOV::MOOV() {
    memcpy(data + 4, "moov", 4);
  }

  MVEX::MVEX() {
    memcpy(data + 4, "mvex", 4);
  }

  TREX::TREX(unsigned int trackId){
    memcpy(data + 4, "trex", 4);
    setTrackID(trackId);
    setDefaultSampleDescriptionIndex(1);
    setDefaultSampleDuration(0);
    setDefaultSampleSize(0);
    setDefaultSampleFlags(0);
  }
  
  void TREX::setTrackID(uint32_t newTrackID){
    setInt32(newTrackID, 4);
  }
  
  uint32_t TREX::getTrackID(){
    return getInt32(4);
  }
  
  void TREX::setDefaultSampleDescriptionIndex(uint32_t newDefaultSampleDescriptionIndex){
    setInt32(newDefaultSampleDescriptionIndex,8);
  }
  
  uint32_t TREX::getDefaultSampleDescriptionIndex(){
    return getInt32(8);
  }
  
  void TREX::setDefaultSampleDuration(uint32_t newDefaultSampleDuration){
    setInt32(newDefaultSampleDuration,12);
  }
  
  uint32_t TREX::getDefaultSampleDuration(){
    return getInt32(12);
  }
  
  void TREX::setDefaultSampleSize(uint32_t newDefaultSampleSize){
    setInt32(newDefaultSampleSize,16);
  }
  
  uint32_t TREX::getDefaultSampleSize(){
    return getInt32(16);
  }
  
  void TREX::setDefaultSampleFlags(uint32_t newDefaultSampleFlags){
    setInt32(newDefaultSampleFlags,20);
  }
  
  uint32_t TREX::getDefaultSampleFlags(){
    return getInt32(20);
  }

  std::string TREX::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[trex] Track Extends (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleDescriptionIndex : " << getDefaultSampleDescriptionIndex() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleDuration : " << getDefaultSampleDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleSize : " << getDefaultSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleFlags : " << getDefaultSampleFlags() << std::endl;
    return r.str();
  }

  TRAK::TRAK() {
    memcpy(data + 4, "trak", 4);
  }

  MDIA::MDIA() {
    memcpy(data + 4, "mdia", 4);
  }

  MINF::MINF() {
    memcpy(data + 4, "minf", 4);
  }

  DINF::DINF() {
    memcpy(data + 4, "dinf", 4);
  }

  MFRA::MFRA() {
    memcpy(data + 4, "mfra", 4);
  }

  MFRO::MFRO() {
    memcpy(data + 4, "mfro", 4);
  }

  void MFRO::setSize(uint32_t newSize) {
    setInt32(newSize, 0);
  }

  uint32_t MFRO::getSize() {
    return getInt32(0);
  }

  std::string MFRO::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[mfro] Movie Fragment Random Access Offset (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Size: " << getSize() << std::endl;
    return r.str();
  }

  HDLR::HDLR(std::string & type, std::string name) {
    memcpy(data + 4, "hdlr", 4);
    //reserve an entire box, except for the string part at the end
    if (!reserve(0, 8, 32)) {
      return;//on fail, cancel all the things
    }
    memset(data + payloadOffset, 0, 24); //set all bytes (32 - 8) to zeroes

    if (type == "video") {
      setHandlerType("vide");
    }
    if (type == "audio") {
      setHandlerType("soun");
    }
    setName(name);
  }

  void HDLR::setHandlerType(const char * newHandlerType) {
    memcpy(data + payloadOffset + 8, newHandlerType, 4);
  }

  std::string HDLR::getHandlerType() {
    return std::string(data + payloadOffset + 8, 4);
  }

  void HDLR::setName(std::string newName) {
    setString(newName, 24);
  }

  std::string HDLR::getName() {
    return getString(24);
  }

  std::string HDLR::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[hdlr] Handler Reference (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Handler Type: " << getHandlerType() << std::endl;
    r << std::string(indent + 1, ' ') << "Name: " << getName() << std::endl;
    return r.str();
  }

  //Note: next 4 headers inherit from fullBox, start at byte 4.
  VMHD::VMHD() {
    memcpy(data + 4, "vmhd", 4);
    setGraphicsMode(0);
    setOpColor(0, 0);
    setOpColor(0, 1);
    setOpColor(0, 2);
  }

  void VMHD::setGraphicsMode(uint16_t newGraphicsMode) {
    setInt16(newGraphicsMode, 4);
  }

  uint16_t VMHD::getGraphicsMode() {
    return getInt16(4);
  }

  uint32_t VMHD::getOpColorCount() {
    return 3;
  }

  void VMHD::setOpColor(uint16_t newOpColor, size_t index) {
    if (index < 3) {
      setInt16(newOpColor, 6 + (2 * index));
    }
  }

  uint16_t VMHD::getOpColor(size_t index) {
    if (index < 3) {
      return getInt16(6 + (index * 2));
    } else {
      return 0;
    }
  }

  std::string VMHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[vmhd] Video Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "GraphicsMode: " << getGraphicsMode() << std::endl;
    for (unsigned int i = 0; i < getOpColorCount(); i++) {
      r << std::string(indent + 1, ' ') << "OpColor[" << i << "]: " << getOpColor(i) << std::endl;
    }
    return r.str();
  }

  SMHD::SMHD() {
    memcpy(data + 4, "smhd", 4);
    setBalance(0);
    setInt16(0, 6);
  }

  void SMHD::setBalance(int16_t newBalance) {
    setInt16(newBalance, 4);
  }

  int16_t SMHD::getBalance() {
    return getInt16(4);
  }

  std::string SMHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[smhd] Sound Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Balance: " << getBalance() << std::endl;
    return r.str();
  }

  HMHD::HMHD() {
    memcpy(data + 4, "hmhd", 4);
  }

  void HMHD::setMaxPDUSize(uint16_t newMaxPDUSize) {
    setInt16(newMaxPDUSize, 4);
  }

  uint16_t HMHD::getMaxPDUSize() {
    return getInt16(4);
  }

  void HMHD::setAvgPDUSize(uint16_t newAvgPDUSize) {
    setInt16(newAvgPDUSize, 6);
  }

  uint16_t HMHD::getAvgPDUSize() {
    return getInt16(6);
  }

  void HMHD::setMaxBitRate(uint32_t newMaxBitRate) {
    setInt32(newMaxBitRate, 8);
  }

  uint32_t HMHD::getMaxBitRate() {
    return getInt32(8);
  }

  void HMHD::setAvgBitRate(uint32_t newAvgBitRate) {
    setInt32(newAvgBitRate, 12);
  }

  uint32_t HMHD::getAvgBitRate() {
    return getInt32(12);
  }

  std::string HMHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[hmhd] Hint Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "maxPDUSize: " << getMaxPDUSize() << std::endl;
    r << std::string(indent + 1, ' ') << "avgPDUSize: " << getAvgPDUSize() << std::endl;
    r << std::string(indent + 1, ' ') << "maxBitRate: " << getMaxBitRate() << std::endl;
    r << std::string(indent + 1, ' ') << "avgBitRate: " << getAvgBitRate() << std::endl;
    return r.str();
  }

  NMHD::NMHD() {
    memcpy(data + 4, "nmhd", 4);
  }

  std::string NMHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[nmhd] Null Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    return r.str();
  }

  MEHD::MEHD() {
    memcpy(data + 4, "mehd", 4);
  }

  void MEHD::setFragmentDuration(uint64_t newFragmentDuration) {
    if (getVersion() == 0) {
      setInt32(newFragmentDuration, 4);
    } else {
      setInt64(newFragmentDuration, 4);
    }
  }

  uint64_t MEHD::getFragmentDuration() {
    if (getVersion() == 0) {
      return getInt32(4);
    } else {
      return getInt64(4);
    }
  }

  std::string MEHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[mehd] Movie Extends Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "FragmentDuration: " << getFragmentDuration() << std::endl;
    return r.str();
  }

  STBL::STBL() {
    memcpy(data + 4, "stbl", 4);
  }

  URL::URL() {
    memcpy(data + 4, "url ", 4);
  }

  void URL::setLocation(std::string newLocation) {
    setString(newLocation, 4);
  }

  std::string URL::getLocation() {
    return std::string(getString(4), getStringLen(4));
  }

  std::string URL::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[url ] URL Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Location: " << getLocation() << std::endl;
    return r.str();
  }

  URN::URN() {
    memcpy(data + 4, "urn ", 4);
  }

  void URN::setName(std::string newName) {
    setString(newName, 4);
  }

  std::string URN::getName() {
    return std::string(getString(4), getStringLen(4));
  }

  void URN::setLocation(std::string newLocation) {
    setString(newLocation, 4 + getStringLen(4) + 1);
  }

  std::string URN::getLocation() {
    int loc = 4 + getStringLen(4) + 1;
    return std::string(getString(loc), getStringLen(loc));
  }

  std::string URN::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[urn ] URN Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Name: " << getName() << std::endl;
    r << std::string(indent + 1, ' ') << "Location: " << getLocation() << std::endl;
    return r.str();
  }

  DREF::DREF() {
    memcpy(data + 4, "dref", 4);
    setVersion(0);
    setFlags(0);
    setInt32(0, 4);
    URL urlBox;
    urlBox.setFlags(1);
    setDataEntry(urlBox, 0);
  }

  uint32_t DREF::getEntryCount() {
    return getInt32(4);
  }

  void DREF::setDataEntry(fullBox & newDataEntry, size_t index) {
    unsigned int i;
    uint32_t offset = 8; //start of boxes
    for (i = 0; i < getEntryCount() && i < index; i++) {
      offset += getBoxLen(offset);
    }
    if (index + 1 > getEntryCount()) {
      int amount = index + 1 - getEntryCount();
      if (!reserve(payloadOffset + offset, 0, amount * 8)) {
        return;
      }
      for (int j = 0; j < amount; ++j) {
        memcpy(data + payloadOffset + offset + j * 8, "\000\000\000\010erro", 8);
      }
      setInt32(index + 1, 4);
      offset += (index - i) * 8;
    }
    setBox(newDataEntry, offset);
  }

  Box & DREF::getDataEntry(size_t index) {
    uint32_t offset = 8;
    if (index > getEntryCount()) {
      static Box res;
      return (Box &)res;
    }

    for (unsigned int i = 0; i < index; i++) {
      offset += getBoxLen(offset);
    }
    return (Box &)getBox(offset);
  }

  std::string DREF::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[dref] Data Reference Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (unsigned int i = 0; i < getEntryCount(); i++) {
      r << getDataEntry(i).toPrettyString(indent + 1);
    }
    return r.str();
  }

  MVHD::MVHD(long long unsigned int duration) {
    memcpy(data + 4, "mvhd", 4);

    //reserve an entire version 0 box
    if (!reserve(0, 9, 108)) {
      return;//on fail, cancel all the things
    }
    memset(data + payloadOffset, 0, 100); //set all bytes (108 - 8) to zeroes

    setTimeScale(1000);//we always use milliseconds
    setDuration(duration);//in ms
    setRate(0x00010000);//playback rate 1.0X
    setVolume(0x0100);//volume 1.0X
    setMatrix(0x00010000, 0);
    setMatrix(0x00010000, 4);
    setMatrix(0x40000000, 8);
    setTrackID(0xFFFFFFFF);//empty track numbers is unknown

  }

  void MVHD::setCreationTime(uint64_t newCreationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newCreationTime, 4);
    } else {
      setInt64(newCreationTime, 4);
    }
  }

  uint64_t MVHD::getCreationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(4);
    } else {
      return getInt64(4);
    }
  }

  void MVHD::setModificationTime(uint64_t newModificationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newModificationTime, 8);
    } else {
      setInt64(newModificationTime, 12);
    }
  }

  uint64_t MVHD::getModificationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(8);
    } else {
      return getInt64(12);
    }
  }

  void MVHD::setTimeScale(uint32_t newTimeScale) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newTimeScale, 12);
    } else {
      setInt32(newTimeScale, 20);
    }
  }

  uint32_t MVHD::getTimeScale() {
    if (getVersion() == 0) {
      return getInt32(12);
    } else {
      return getInt32(20);
    }
  }

  void MVHD::setDuration(uint64_t newDuration) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newDuration, 16);
    } else {
      setInt64(newDuration, 24);
    }
  }

  uint64_t MVHD::getDuration() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(16);
    } else {
      return getInt64(24);
    }
  }

  void MVHD::setRate(uint32_t newRate) {
    if (getVersion() == 0) {
      setInt32(newRate, 20);
    } else {
      setInt32(newRate, 32);
    }
  }

  uint32_t MVHD::getRate() {
    if (getVersion() == 0) {
      return getInt32(20);
    } else {
      return getInt32(32);
    }
  }

  void MVHD::setVolume(uint16_t newVolume) {
    if (getVersion() == 0) {
      setInt16(newVolume, 24);
    } else {
      setInt16(newVolume, 36);
    }
  }

  uint16_t MVHD::getVolume() {
    if (getVersion() == 0) {
      return getInt16(24);
    } else {
      return getInt16(36);
    }
  }
  //10 bytes reserved in between
  uint32_t MVHD::getMatrixCount() {
    return 9;
  }

  void MVHD::setMatrix(int32_t newMatrix, size_t index) {
    int offset = 0;
    if (getVersion() == 0) {
      offset = 24 + 2 + 10;
    } else {
      offset = 36 + 2 + 10;
    }
    setInt32(newMatrix, offset + index * 4);
  }

  int32_t MVHD::getMatrix(size_t index) {
    int offset = 0;
    if (getVersion() == 0){
      offset = 36;
    }else{
      offset = 48;
    }
    return getInt32(offset + index * 4);
  }

  //24 bytes of pre-defined in between
  void MVHD::setTrackID(uint32_t newTrackID){
    if (getVersion() == 0){
      setInt32(newTrackID, 96);
    }else{
      setInt32(newTrackID, 108);
    }
  }
  
  uint32_t MVHD::getTrackID(){
    if (getVersion() == 0){
      return getInt32(96);
    }else{
      return getInt32(108);
    }
  }

  std::string MVHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[mvhd] Movie Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale: " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Rate: " << getRate() << std::endl;
    r << std::string(indent + 1, ' ') << "Volume: " << getVolume() << std::endl;
    r << std::string(indent + 1, ' ') << "Matrix: ";
    for (unsigned int i = 0; i < getMatrixCount(); i++) {
      r << getMatrix(i);
      if (i != getMatrixCount() - 1) {
        r << ", ";
      }
    }
    r << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    return r.str();
  }

  TFRA::TFRA() {
    memcpy(data + 4, "dref", 4);
  }

  //note, fullbox starts at byte 4
  void TFRA::setTrackID(uint32_t newTrackID) {
    setInt32(newTrackID, 4);
  }

  uint32_t TFRA::getTrackID() {
    return getInt32(4);
  }

  void TFRA::setLengthSizeOfTrafNum(char newVal) {
    char part = getInt8(11);
    setInt8(((newVal & 0x03) << 4) + (part & 0xCF), 11);
  }

  char TFRA::getLengthSizeOfTrafNum() {
    return (getInt8(11) >> 4) & 0x03;
  }

  void TFRA::setLengthSizeOfTrunNum(char newVal) {
    char part = getInt8(11);
    setInt8(((newVal & 0x03) << 2) + (part & 0xF3), 11);
  }

  char TFRA::getLengthSizeOfTrunNum() {
    return (getInt8(11) >> 2) & 0x03;
  }

  void TFRA::setLengthSizeOfSampleNum(char newVal) {
    char part = getInt8(11);
    setInt8(((newVal & 0x03)) + (part & 0xFC), 11);
  }

  char TFRA::getLengthSizeOfSampleNum() {
    return (getInt8(11)) & 0x03;
  }

  void TFRA::setNumberOfEntry(uint32_t newNumberOfEntry) {
    setInt32(newNumberOfEntry, 12);
  }

  uint32_t TFRA::getNumberOfEntry() {
    return getInt32(12);
  }

  uint32_t TFRA::getTFRAEntrySize() {
    int EntrySize = (getVersion() == 1 ? 16 : 8);
    EntrySize += getLengthSizeOfTrafNum() + 1;
    EntrySize += getLengthSizeOfTrunNum() + 1;
    EntrySize += getLengthSizeOfSampleNum() + 1;
    return EntrySize;
  }

  void TFRA::setTFRAEntry(TFRAEntry newTFRAEntry, uint32_t no) {
    if (no + 1 > getNumberOfEntry()) { //if a new entry is issued
      uint32_t offset = 16 + getTFRAEntrySize() * getNumberOfEntry();//start of filler in bytes
      uint32_t fillsize = (no + 1 - getNumberOfEntry()) * getTFRAEntrySize(); //filler in bytes
      if (!reserve(offset, 0, fillsize)) {//filling space
        return;
      }
      setNumberOfEntry(no + 1);
    }
    uint32_t loc = 16 + no * getTFRAEntrySize();
    if (getVersion() == 1) {
      setInt64(newTFRAEntry.time, loc);
      setInt64(newTFRAEntry.moofOffset, loc + 8);
      loc += 16;
    } else {
      setInt32(newTFRAEntry.time, loc);
      setInt32(newTFRAEntry.moofOffset, loc + 4);
      loc += 8;
    }
    switch (getLengthSizeOfTrafNum()) {
      case 0:
        setInt8(newTFRAEntry.trafNumber, loc);
        break;
      case 1:
        setInt16(newTFRAEntry.trafNumber, loc);
        break;
      case 2:
        setInt24(newTFRAEntry.trafNumber, loc);
        break;
      case 3:
        setInt32(newTFRAEntry.trafNumber, loc);
        break;
    }
    loc += getLengthSizeOfTrafNum() + 1;
    switch (getLengthSizeOfTrunNum()) {
      case 0:
        setInt8(newTFRAEntry.trunNumber, loc);
        break;
      case 1:
        setInt16(newTFRAEntry.trunNumber, loc);
        break;
      case 2:
        setInt24(newTFRAEntry.trunNumber, loc);
        break;
      case 3:
        setInt32(newTFRAEntry.trunNumber, loc);
        break;
    }
    loc += getLengthSizeOfTrunNum() + 1;
    switch (getLengthSizeOfSampleNum()) {
      case 0:
        setInt8(newTFRAEntry.sampleNumber, loc);
        break;
      case 1:
        setInt16(newTFRAEntry.sampleNumber, loc);
        break;
      case 2:
        setInt24(newTFRAEntry.sampleNumber, loc);
        break;
      case 3:
        setInt32(newTFRAEntry.sampleNumber, loc);
        break;
    }
  }

  TFRAEntry & TFRA::getTFRAEntry(uint32_t no) {
    static TFRAEntry retval;
    if (no >= getNumberOfEntry()) {
      static TFRAEntry inval;
      return inval;
    }
    uint32_t loc = 16 + no * getTFRAEntrySize();
    if (getVersion() == 1) {
      retval.time = getInt64(loc);
      retval.moofOffset = getInt64(loc + 8);
      loc += 16;
    } else {
      retval.time = getInt32(loc);
      retval.moofOffset = getInt32(loc + 4);
      loc += 8;
    }
    switch (getLengthSizeOfTrafNum()) {
      case 0:
        retval.trafNumber = getInt8(loc);
        break;
      case 1:
        retval.trafNumber = getInt16(loc);
        break;
      case 2:
        retval.trafNumber = getInt24(loc);
        break;
      case 3:
        retval.trafNumber = getInt32(loc);
        break;
    }
    loc += getLengthSizeOfTrafNum() + 1;
    switch (getLengthSizeOfTrunNum()) {
      case 0:
        retval.trunNumber = getInt8(loc);
        break;
      case 1:
        retval.trunNumber = getInt16(loc);
        break;
      case 2:
        retval.trunNumber = getInt24(loc);
        break;
      case 3:
        retval.trunNumber = getInt32(loc);
        break;
    }
    loc += getLengthSizeOfTrunNum() + 1;
    switch (getLengthSizeOfSampleNum()) {
      case 0:
        retval.sampleNumber = getInt8(loc);
        break;
      case 1:
        retval.sampleNumber = getInt16(loc);
        break;
      case 2:
        retval.sampleNumber = getInt24(loc);
        break;
      case 3:
        retval.sampleNumber = getInt32(loc);
        break;
    }
    return retval;
  }

  std::string TFRA::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfra] Track Fragment Random Access Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfTrafNum: " << (int)getLengthSizeOfTrafNum() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfTrunNum: " << (int)getLengthSizeOfTrunNum() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfSampleNum: " << (int)getLengthSizeOfSampleNum() << std::endl;
    r << std::string(indent + 1, ' ') << "NumberOfEntry: " << getNumberOfEntry() << std::endl;
    for (unsigned int i = 0; i < getNumberOfEntry(); i++) {
      static TFRAEntry temp;
      temp = getTFRAEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i << "]:" << std::endl;
      r << std::string(indent + 2, ' ') << "Time: " << temp.time << std::endl;
      r << std::string(indent + 2, ' ') << "MoofOffset: " << temp.moofOffset << std::endl;
      r << std::string(indent + 2, ' ') << "TrafNumber: " << temp.trafNumber << std::endl;
      r << std::string(indent + 2, ' ') << "TrunNumber: " << temp.trunNumber << std::endl;
      r << std::string(indent + 2, ' ') << "SampleNumber: " << temp.sampleNumber << std::endl;
    }
    return r.str();
  }


  TKHD::TKHD(uint32_t trackId, uint64_t duration, uint32_t width, uint32_t height) {
    initialize();
    setTrackID(trackId);
    setDuration(duration);
    setWidth(width);
    setHeight(height);
  }

  TKHD::TKHD(DTSC::Track & track, bool fragmented) {
    initialize(); 
    setTrackID(track.trackID);
    setDuration(-1);
    if (!fragmented) {
      setDuration(track.lastms - track.firstms);
    }
    if (track.type == "video") {
      setWidth(track.width);
      setHeight(track.height);
    }
  }

  void TKHD::initialize(){
    memcpy(data + 4, "tkhd", 4);

    //reserve an entire version 0 box
    if (!reserve(0, 9, 92)) {
      return;//on fail, cancel all the things
    }
    memset(data + payloadOffset, 0, 84); //set all bytes (92 - 8) to zeroes
    setFlags(3);//ENABLED | IN_MOVIE 
    setMatrix(0x00010000, 0);
    setMatrix(0x00010000, 4);
    setMatrix(0x40000000, 8);
    setVolume(0x0100);
  }

  void TKHD::setCreationTime(uint64_t newCreationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newCreationTime, 4);
    } else {
      setInt64(newCreationTime, 4);
    }
  }

  uint64_t TKHD::getCreationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(4);
    } else {
      return getInt64(4);
    }
  }

  void TKHD::setModificationTime(uint64_t newModificationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newModificationTime, 8);
    } else {
      setInt64(newModificationTime, 12);
    }
  }

  uint64_t TKHD::getModificationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(8);
    } else {
      return getInt64(12);
    }
  }

  void TKHD::setTrackID(uint32_t newTrackID) {
    if (getVersion() == 0) {
      setInt32(newTrackID, 12);
    } else {
      setInt32(newTrackID, 20);
    }
  }

  uint32_t TKHD::getTrackID() {
    if (getVersion() == 0) {
      return getInt32(12);
    } else {
      return getInt32(20);
    }
  }
  //note 4 bytes reserved in between
  void TKHD::setDuration(uint64_t newDuration) {
    if (getVersion() == 0) {
      setInt32(newDuration, 20);
    } else {
      setInt64(newDuration, 28);
    }
  }

  uint64_t TKHD::getDuration() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(20);
    } else {
      return getInt64(28);
    }
  }
  //8 bytes reserved in between
  void TKHD::setLayer(uint16_t newLayer) {
    if (getVersion() == 0) {
      setInt16(newLayer, 32);
    } else {
      setInt16(newLayer, 44);
    }
  }

  uint16_t TKHD::getLayer() {
    if (getVersion() == 0) {
      return getInt16(32);
    } else {
      return getInt16(44);
    }
  }

  void TKHD::setAlternateGroup(uint16_t newAlternateGroup) {
    if (getVersion() == 0) {
      setInt16(newAlternateGroup, 34);
    } else {
      setInt16(newAlternateGroup, 46);
    }
  }

  uint16_t TKHD::getAlternateGroup() {
    if (getVersion() == 0) {
      return getInt16(34);
    } else {
      return getInt16(46);
    }
  }

  void TKHD::setVolume(uint16_t newVolume) {
    if (getVersion() == 0) {
      setInt16(newVolume, 36);
    } else {
      setInt16(newVolume, 48);
    }
  }

  uint16_t TKHD::getVolume() {
    if (getVersion() == 0) {
      return getInt16(36);
    } else {
      return getInt16(48);
    }
  }
  //2 bytes reserved in between
  uint32_t TKHD::getMatrixCount() {
    return 9;
  }

  void TKHD::setMatrix(int32_t newMatrix, size_t index) {
    int offset = 0;
    if (getVersion() == 0) {
      offset = 40;
    } else {
      offset = 52;
    }
    setInt32(newMatrix, offset + index * 4);
  }

  int32_t TKHD::getMatrix(size_t index) {
    int offset = 0;
    if (getVersion() == 0) {
      offset = 40;
    } else {
      offset = 52;
    }
    return getInt32(offset + index * 4);
  }

  void TKHD::setWidth(double newWidth) {
    if (getVersion() == 0) {
      setInt32(newWidth * 65536.0, 76);
    } else {
      setInt32(newWidth * 65536.0, 88);
    }
  }

  double TKHD::getWidth() {
    if (getVersion() == 0) {
      return getInt32(76) / 65536.0;
    } else {
      return getInt32(88) / 65536.0;
    }
  }

  void TKHD::setHeight(double newHeight) {
    if (getVersion() == 0) {
      setInt32(newHeight * 65536.0, 80);
    } else {
      setInt32(newHeight * 65536.0, 92);
    }
  }

  double TKHD::getHeight() {
    if (getVersion() == 0) {
      return getInt32(80) / 65536.0;
    } else {
      return getInt32(92) / 65536.0;
    }
  }

  std::string TKHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[tkhd] Track Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Layer: " << getLayer() << std::endl;
    r << std::string(indent + 1, ' ') << "AlternateGroup: " << getAlternateGroup() << std::endl;
    r << std::string(indent + 1, ' ') << "Volume: " << getVolume() << std::endl;
    r << std::string(indent + 1, ' ') << "Matrix: ";
    for (unsigned int i = 0; i < getMatrixCount(); i++) {
      r << getMatrix(i);
      if (i != getMatrixCount() - 1) {
        r << ", ";
      }
    }
    r << std::endl;
    r << std::string(indent + 1, ' ') << "Width: " << getWidth() << std::endl;
    r << std::string(indent + 1, ' ') << "Height: " << getHeight() << std::endl;
    return r.str();
  }

  MDHD::MDHD(uint64_t duration) {
    memcpy(data + 4, "mdhd", 4);
    //reserve an entire version 0 box
    if (!reserve(0, 9, 32)) {
      return;//on fail, cancel all the things
    }
    memset(data + payloadOffset, 0, 24); //set all bytes (32 - 8) to zeroes

    setTimeScale(1000);
    setDuration(duration);
  }

  void MDHD::setCreationTime(uint64_t newCreationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newCreationTime, 4);
    } else {
      setInt64(newCreationTime, 4);
    }
  }

  uint64_t MDHD::getCreationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(4);
    } else {
      return getInt64(4);
    }
  }

  void MDHD::setModificationTime(uint64_t newModificationTime) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newModificationTime, 8);
    } else {
      setInt64(newModificationTime, 12);
    }
  }

  uint64_t MDHD::getModificationTime() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(8);
    } else {
      return getInt64(12);
    }
  }

  void MDHD::setTimeScale(uint32_t newTimeScale) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newTimeScale, 12);
    } else {
      setInt32(newTimeScale, 20);
    }
  }

  uint32_t MDHD::getTimeScale() {
    if (getVersion() == 0) {
      return getInt32(12);
    } else {
      return getInt32(20);
    }
  }

  void MDHD::setDuration(uint64_t newDuration) {
    if (getVersion() == 0) {
      setInt32((uint32_t) newDuration, 16);
    } else {
      setInt64(newDuration, 24);
    }
  }

  uint64_t MDHD::getDuration() {
    if (getVersion() == 0) {
      return (uint64_t)getInt32(16);
    } else {
      return getInt64(24);
    }
  }

  void MDHD::setLanguage(uint16_t newLanguage) {
    if (getVersion() == 0) {
      setInt16(newLanguage & 0x7F, 20);
    } else {
      setInt16(newLanguage & 0x7F, 32);
    }
  }

  uint16_t MDHD::getLanguage() {
    if (getVersion() == 0) {
      return getInt16(20) & 0x7F;
    } else {
      return getInt16(32) & 0x7F;
    }
  }

  std::string MDHD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[mdhd] Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale: " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Language: 0x" << std::hex << getLanguage() << std::dec << std::endl;
    return r.str();
  }

  STTS::STTS(char v, uint32_t f) {
    memcpy(data + 4, "stts", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void STTS::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t STTS::getEntryCount() {
    return getInt32(4);
  }

  void STTS::setSTTSEntry(STTSEntry newSTTSEntry, uint32_t no) {
    if (no + 1 > getEntryCount()) {
      setEntryCount(no + 1);
      for (unsigned int i = getEntryCount(); i < no; i++) {
        setInt64(0, 8 + (i * 8));//filling up undefined entries of 64 bits
      }
    }
    setInt32(newSTTSEntry.sampleCount, 8 + no * 8);
    setInt32(newSTTSEntry.sampleDelta, 8 + (no * 8) + 4);
  }

  STTSEntry STTS::getSTTSEntry(uint32_t no) {
    static STTSEntry retval;
    if (no >= getEntryCount()) {
      static STTSEntry inval;
      return inval;
    }
    retval.sampleCount = getInt32(8 + (no * 8));
    retval.sampleDelta = getInt32(8 + (no * 8) + 4);
    return retval;
  }

  std::string STTS::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stts] Sample Table Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (unsigned int i = 0; i < getEntryCount(); i++) {
      static STTSEntry temp;
      temp = getSTTSEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i << "]:" << std::endl;
      r << std::string(indent + 2, ' ') << "SampleCount: " << temp.sampleCount << std::endl;
      r << std::string(indent + 2, ' ') << "SampleDelta: " << temp.sampleDelta << std::endl;
    }
    return r.str();

  }

  CTTS::CTTS() {
    memcpy(data + 4, "ctts", 4);
  }

  void CTTS::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t CTTS::getEntryCount() {
    return getInt32(4);
  }

  void CTTS::setCTTSEntry(CTTSEntry newCTTSEntry, uint32_t no) {
    if (no + 1 > getEntryCount()) {
      for (unsigned int i = getEntryCount(); i < no; i++) {
        setInt64(0, 8 + (i * 8));//filling up undefined entries of 64 bits
      }
      setEntryCount(no + 1);
    }
    setInt32(newCTTSEntry.sampleCount, 8 + no * 8);
    setInt32(newCTTSEntry.sampleOffset, 8 + (no * 8) + 4);
  }

  CTTSEntry CTTS::getCTTSEntry(uint32_t no) {
    static CTTSEntry retval;
    if (no >= getEntryCount()) {
      static CTTSEntry inval;
      return inval;
    }
    retval.sampleCount = getInt32(8 + (no * 8));
    retval.sampleOffset = getInt32(8 + (no * 8) + 4);
    return retval;
  }

  std::string CTTS::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[ctts] Composition Time To Sample Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (unsigned int i = 0; i < getEntryCount(); i++) {
      static CTTSEntry temp;
      temp = getCTTSEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i << "]:" << std::endl;
      r << std::string(indent + 2, ' ') << "SampleCount: " << temp.sampleCount << std::endl;
      r << std::string(indent + 2, ' ') << "SampleOffset: " << temp.sampleOffset << std::endl;
    }
    return r.str();

  }

  STSCEntry::STSCEntry(unsigned int _first, unsigned int _count, unsigned int _index){
    firstChunk = _first;
    samplesPerChunk = _count;
    sampleDescriptionIndex = _index;
  }

  STSC::STSC(char v, uint32_t f) {
    memcpy(data + 4, "stsc", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void STSC::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t STSC::getEntryCount() {
    return getInt32(4);
  }

  void STSC::setSTSCEntry(STSCEntry newSTSCEntry, uint32_t no) {
    if (no + 1 > getEntryCount()) {
      setEntryCount(no + 1);
      for (unsigned int i = getEntryCount(); i < no; i++) {
        setInt64(0, 8 + (i * 12));//filling up undefined entries of 64 bits
        setInt32(0, 8 + (i * 12) + 8);
      }
    }
    setInt32(newSTSCEntry.firstChunk, 8 + no * 12);
    setInt32(newSTSCEntry.samplesPerChunk, 8 + (no * 12) + 4);
    setInt32(newSTSCEntry.sampleDescriptionIndex, 8 + (no * 12) + 8);
  }

  STSCEntry STSC::getSTSCEntry(uint32_t no) {
    static STSCEntry retval;
    if (no >= getEntryCount()) {
      static STSCEntry inval;
      return inval;
    }
    retval.firstChunk = getInt32(8 + (no * 12));
    retval.samplesPerChunk = getInt32(8 + (no * 12) + 4);
    retval.sampleDescriptionIndex = getInt32(8 + (no * 12) + 8);
    return retval;
  }

  std::string STSC::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsc] Sample To Chunk Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (unsigned int i = 0; i < getEntryCount(); i++) {
      static STSCEntry temp;
      temp = getSTSCEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i << "]: Chunks " << temp.firstChunk << " onward contain " << temp.samplesPerChunk << " samples, description " << temp.sampleDescriptionIndex << std::endl;
    }
    return r.str();
  }

  STCO::STCO(char v, uint32_t f) {
    memcpy(data + 4, "stco", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void STCO::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t STCO::getEntryCount() {
    return getInt32(4);
  }

  void STCO::setChunkOffset(uint32_t newChunkOffset, uint32_t no) {
    setInt32(newChunkOffset, 8 + no * 4);
    uint32_t entryCount = getEntryCount();
    //if entrycount is lower than new entry count, update it and fill any skipped entries with zeroes.
    if (no + 1 > entryCount) {
      setEntryCount(no + 1);
      //fill undefined entries, if any (there's only undefined entries if we skipped an entry)
      if (no > entryCount){
        memset(data+payloadOffset+8+entryCount*4, 0, 4*(no-entryCount));
      }
    }
  }

  uint32_t STCO::getChunkOffset(uint32_t no) {
    if (no >= getEntryCount()) {
      return 0;
    }
    return getInt32(8 + no * 4);
  }

  std::string STCO::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stco] Chunk Offset Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    r << std::string(indent + 1, ' ') << "Offsets: ";
    for (unsigned long i = 0; i < getEntryCount(); i++) {
      r << getChunkOffset(i);
      if (i != getEntryCount() - 1){
        r << ", ";
      }
    }
    r << std::endl;
    return r.str();
  }

  CO64::CO64(char v, uint32_t f) {
    memcpy(data + 4, "co64", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void CO64::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t CO64::getEntryCount() {
    return getInt32(4);
  }

  void CO64::setChunkOffset(uint64_t newChunkOffset, uint32_t no) {
    setInt64(newChunkOffset, 8 + no * 8);
    uint32_t entryCount = getEntryCount();
    //if entrycount is lower than new entry count, update it and fill any skipped entries with zeroes.
    if (no + 1 > entryCount) {
      setEntryCount(no + 1);
      //fill undefined entries, if any (there's only undefined entries if we skipped an entry)
      if (no > entryCount){
        memset(data+payloadOffset+8+entryCount*8, 0, 8*(no-entryCount));
      }
    }
  }

  uint64_t CO64::getChunkOffset(uint32_t no) {
    if (no >= getEntryCount()) {
      return 0;
    }
    return getInt64(8 + no * 8);
  }

  std::string CO64::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[co64] 64-bits Chunk Offset Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    r << std::string(indent + 1, ' ') << "Offsets: ";
    for (unsigned long i = 0; i < getEntryCount(); i++) {
      r << getChunkOffset(i);
      if (i != getEntryCount() - 1){
        r << ", ";
      }
    }
    r << std::endl;
    return r.str();
  }

  STSZ::STSZ(char v, uint32_t f) {
    memcpy(data + 4, "stsz", 4);
    setVersion(v);
    setFlags(f);
    setSampleCount(0);
  }

  void STSZ::setSampleSize(uint32_t newSampleSize) {
    setInt32(newSampleSize, 4);
  }

  uint32_t STSZ::getSampleSize() {
    return getInt32(4);
  }

  void STSZ::setSampleCount(uint32_t newSampleCount) {
    setInt32(newSampleCount, 8);
  }

  uint32_t STSZ::getSampleCount() {
    return getInt32(8);
  }

  void STSZ::setEntrySize(uint32_t newEntrySize, uint32_t no) {
    if (no + 1 > getSampleCount()) {
      setSampleCount(no + 1);
      for (unsigned int i = getSampleCount(); i < no; i++) {
        setInt32(0, 12 + i * 4);//filling undefined entries
      }
    }
    setInt32(newEntrySize, 12 + no * 4);
  }

  uint32_t STSZ::getEntrySize(uint32_t no) {
    if (no >= getSampleCount()) {
      return 0;
    }
    long unsigned int retVal = getInt32(12 + no * 4);
    if (retVal == 0){
      return getSampleSize();
    }else{
      return retVal;
    }
  }

  std::string STSZ::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsz] Sample Size Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Global Sample Size: " << getSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "Sample Count: " << getSampleCount() << std::endl;

    r << std::string(indent + 1, ' ') << "Sample sizes: ";
    for (unsigned long i = 0; i < getSampleCount(); i++) {
      r << getEntrySize(i);
      if (i != getSampleCount() - 1){
        r << ", ";
      }
    }
    r << std::endl;
    return r.str();
  }

  SampleEntry::SampleEntry() {
    memcpy(data + 4, "erro", 4);
  }

  void SampleEntry::setDataReferenceIndex(uint16_t newDataReferenceIndex) {
    setInt16(newDataReferenceIndex, 6);
  }

  uint16_t SampleEntry::getDataReferenceIndex() {
    return getInt16(6);
  }

  std::string SampleEntry::toPrettySampleString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent + 1, ' ') << "DataReferenceIndex: " << getDataReferenceIndex() << std::endl;
    return r.str();
  }

  CLAP::CLAP() {
    memcpy(data + 4, "clap", 4);
    setHorizOffN(0);
    setHorizOffD(0);
    setVertOffN(0);
    setVertOffD(0);
  }

  void CLAP::setCleanApertureWidthN(uint32_t newVal) {
    setInt32(newVal, 0);
  }

  uint32_t CLAP::getCleanApertureWidthN() {
    return getInt32(0);
  }

  void CLAP::setCleanApertureWidthD(uint32_t newVal) {
    setInt32(newVal, 4);
  }

  uint32_t CLAP::getCleanApertureWidthD() {
    return getInt32(4);
  }

  void CLAP::setCleanApertureHeightN(uint32_t newVal) {
    setInt32(newVal, 8);
  }

  uint32_t CLAP::getCleanApertureHeightN() {
    return getInt32(8);
  }

  void CLAP::setCleanApertureHeightD(uint32_t newVal) {
    setInt32(newVal, 12);
  }

  uint32_t CLAP::getCleanApertureHeightD() {
    return getInt32(12);
  }

  void CLAP::setHorizOffN(uint32_t newVal) {
    setInt32(newVal, 16);
  }

  uint32_t CLAP::getHorizOffN() {
    return getInt32(16);
  }

  void CLAP::setHorizOffD(uint32_t newVal) {
    setInt32(newVal, 20);
  }

  uint32_t CLAP::getHorizOffD() {
    return getInt32(20);
  }

  void CLAP::setVertOffN(uint32_t newVal) {
    setInt32(newVal, 24);
  }

  uint32_t CLAP::getVertOffN() {
    return getInt32(24);
  }

  void CLAP::setVertOffD(uint32_t newVal) {
    setInt32(newVal, 28);
  }

  uint32_t CLAP::getVertOffD() {
    return getInt32(32);
  }

  std::string CLAP::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[clap] Clean Aperture Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureWidthN: " << getCleanApertureWidthN() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureWidthD: " << getCleanApertureWidthD() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureHeightN: " << getCleanApertureHeightN() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureHeightD: " << getCleanApertureHeightD() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizOffN: " << getHorizOffN() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizOffD: " << getHorizOffD() << std::endl;
    r << std::string(indent + 1, ' ') << "VertOffN: " << getVertOffN() << std::endl;
    r << std::string(indent + 1, ' ') << "VertOffD: " << getVertOffD() << std::endl;
    return r.str();
  }

  PASP::PASP() {
    memcpy(data + 4, "pasp", 4);
    setHSpacing(1);
    setVSpacing(1);
  }

  void PASP::setHSpacing(uint32_t newVal) {
    setInt32(newVal, 0);
  }

  uint32_t PASP::getHSpacing() {
    return getInt32(0);
  }

  void PASP::setVSpacing(uint32_t newVal) {
    setInt32(newVal, 4);
  }

  uint32_t PASP::getVSpacing() {
    return getInt32(4);
  }

  std::string PASP::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[pasp] Pixel Aspect Ratio Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "HSpacing: " << getHSpacing() << std::endl;
    r << std::string(indent + 1, ' ') << "VSpacing: " << getVSpacing() << std::endl;
    return r.str();
  }

  VisualSampleEntry::VisualSampleEntry() {
    initialize();
  }

  VisualSampleEntry::VisualSampleEntry(DTSC::Track & track){
    initialize();
    setDataReferenceIndex(1);
    setWidth(track.width);
    setHeight(track.height);
    if (track.codec == "H264") {
      setCodec("avc1");
      MP4::AVCC avccBox;
      avccBox.setPayload(track.init);
      setCLAP(avccBox);
    }
    /*LTS-START*/
    if (track.codec == "HEVC") {
      setCodec("hev1");
      MP4::HVCC hvccBox;
      hvccBox.setPayload(track.init);
      setCLAP(hvccBox);
    }
    /*LTS-END*/
  }

  void VisualSampleEntry::initialize(){
    memcpy(data + 4, "erro", 4);
    setHorizResolution(0x00480000);
    setVertResolution(0x00480000);
    setFrameCount(1);
    setCompressorName("");
    setDepth(0x0018);
  }

  void VisualSampleEntry::setCodec(const char * newCodec) {
    memcpy(data + 4, newCodec, 4);
  }

  void VisualSampleEntry::setWidth(uint16_t newWidth) {
    setInt16(newWidth, 24);
  }

  uint16_t VisualSampleEntry::getWidth() {
    return getInt16(24);
  }

  void VisualSampleEntry::setHeight(uint16_t newHeight) {
    setInt16(newHeight, 26);
  }

  uint16_t VisualSampleEntry::getHeight() {
    return getInt16(26);
  }

  void VisualSampleEntry::setHorizResolution(uint32_t newHorizResolution) {
    setInt32(newHorizResolution, 28);
  }

  uint32_t VisualSampleEntry::getHorizResolution() {
    return getInt32(28);
  }

  void VisualSampleEntry::setVertResolution(uint32_t newVertResolution) {
    setInt32(newVertResolution, 32);
  }

  uint32_t VisualSampleEntry::getVertResolution() {
    return getInt32(32);
  }

  void VisualSampleEntry::setFrameCount(uint16_t newFrameCount) {
    setInt16(newFrameCount, 40);
  }

  uint16_t VisualSampleEntry::getFrameCount() {
    return getInt16(40);
  }

  void VisualSampleEntry::setCompressorName(std::string newCompressorName) {
    if (newCompressorName.size() > 32){
      newCompressorName.resize(32, ' ');
    }
    setString(newCompressorName, 42);
  }

  std::string VisualSampleEntry::getCompressorName() {
    return getString(42);
  }

  void VisualSampleEntry::setDepth(uint16_t newDepth) {
    setInt16(newDepth, 74);
  }

  uint16_t VisualSampleEntry::getDepth() {
    return getInt16(74);
  }

  void VisualSampleEntry::setCLAP(Box & clap) {
    setBox(clap, 78);
  }

  Box & VisualSampleEntry::getCLAP() {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (payloadSize() < 84) { //if the EntryBox is not big enough to hold a CLAP/PASP
      return ret;
    }
    ret = getBox(78);
    return ret;
  }

  void VisualSampleEntry::setPASP(Box & pasp) {
    int writePlace = 78;
    Box tmpBox = getBox(78);
    if (tmpBox.getType () != "erro"){
      writePlace = 78 + getBoxLen(78);
    }
    setBox(pasp, writePlace);
  }

  Box & VisualSampleEntry::getPASP() {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (payloadSize() < 84) { //if the EntryBox is not big enough to hold a CLAP/PASP
      return ret;
    }
    if (payloadSize() < 78 + getBoxLen(78) + 8) {
      return ret;
    } else {
      return getBox(78 + getBoxLen(78));
    }

  }

  std::string VisualSampleEntry::toPrettyVisualString(uint32_t indent, std::string name) {
    std::stringstream r;
    r << std::string(indent, ' ') << name << " (" << boxedSize() << ")" << std::endl;
    r << toPrettySampleString(indent);
    r << std::string(indent + 1, ' ') << "Width: " << getWidth() << std::endl;
    r << std::string(indent + 1, ' ') << "Height: " << getHeight() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizResolution: " << getHorizResolution() << std::endl;
    r << std::string(indent + 1, ' ') << "VertResolution: " << getVertResolution() << std::endl;
    r << std::string(indent + 1, ' ') << "FrameCount: " << getFrameCount() << std::endl;
    r << std::string(indent + 1, ' ') << "CompressorName: " << getCompressorName() << std::endl;
    r << std::string(indent + 1, ' ') << "Depth: " << getDepth() << std::endl;
    if (!getCLAP().isType("erro")) {
      r << getCLAP().toPrettyString(indent + 1);
    }
    if (!getPASP().isType("erro")) {
      r << getPASP().toPrettyString(indent + 1);
    }
    return r.str();
  }

  AudioSampleEntry::AudioSampleEntry() {
    initialize();
  }

  AudioSampleEntry::AudioSampleEntry(DTSC::Track & track) {
    initialize();
    if (track.codec == "AAC" || track.codec == "MP3") {
      setCodec("mp4a");
    }
    if (track.codec == "AC3") {
      setCodec("ac-3");
    }
    setDataReferenceIndex(1);
    setSampleRate(track.rate);
    setChannelCount(track.channels);
    setSampleSize(track.size);
    if (track.codec == "AC3") {
      MP4::DAC3 dac3Box(track.rate, track.channels);
      setCodecBox(dac3Box);
    } else { //other codecs use the ESDS box
      MP4::ESDS esdsBox(track.init);
      setCodecBox(esdsBox);
    }
  }

  void AudioSampleEntry::initialize() {
    memcpy(data + 4, "erro", 4);
    setChannelCount(2);
    setSampleSize(16);
    setSampleRate(44100);
  }

  uint16_t AudioSampleEntry::toAACInit() {
    uint16_t result = 0;
    result |= (2 & 0x1F) << 11;
    result |= (getSampleRate() & 0x0F) << 7;
    result |= (getChannelCount() & 0x0F) << 3;
    return result;
  }

  void AudioSampleEntry::setCodec(const char * newCodec) {
    memcpy(data + 4, newCodec, 4);
  }

  void AudioSampleEntry::setChannelCount(uint16_t newChannelCount) {
    setInt16(newChannelCount, 16);
  }

  uint16_t AudioSampleEntry::getChannelCount() {
    return getInt16(16);
  }

  void AudioSampleEntry::setSampleSize(uint16_t newSampleSize) {
    setInt16(newSampleSize, 18);
  }

  uint16_t AudioSampleEntry::getSampleSize() {
    return getInt16(18);
  }

  void AudioSampleEntry::setPreDefined(uint16_t newPreDefined) {
    setInt16(newPreDefined, 20);
  }

  uint16_t AudioSampleEntry::getPreDefined() {
    return getInt16(20);
  }

  void AudioSampleEntry::setSampleRate(uint32_t newSampleRate) {
    setInt32(newSampleRate << 16, 24);
  }

  uint32_t AudioSampleEntry::getSampleRate() {
    return getInt32(24) >> 16;
  }

  void AudioSampleEntry::setCodecBox(Box & newBox) {
    setBox(newBox, 28);
  }

  Box & AudioSampleEntry::getCodecBox() {
    return getBox(28);
  }

  /*LTS-START*/
  Box & AudioSampleEntry::getSINFBox() {
    static Box ret = Box(getBox(28 + getBoxLen(28)).asBox(), false);
    return ret;
  }
  /*LTS-END*/

  std::string AudioSampleEntry::toPrettyAudioString(uint32_t indent, std::string name) {
    std::stringstream r;
    r << std::string(indent, ' ') << name << " (" << boxedSize() << ")" << std::endl;
    r << toPrettySampleString(indent);
    r << std::string(indent + 1, ' ') << "ChannelCount: " << getChannelCount() << std::endl;
    r << std::string(indent + 1, ' ') << "SampleSize: " << getSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "PreDefined: " << getPreDefined() << std::endl;
    r << std::string(indent + 1, ' ') << "SampleRate: " << getSampleRate() << std::endl;
    r << getCodecBox().toPrettyString(indent + 1) << std::endl;
    /*LTS-START*/
    if (isType("enca")) {
      r << getSINFBox().toPrettyString(indent + 1);
    }
    /*LTS-END*/
    return r.str();
  }

  MP4A::MP4A() {
    memcpy(data + 4, "mp4a", 4);
  }

  std::string MP4A::toPrettyString(uint32_t indent) {
    return toPrettyAudioString(indent, "[mp4a] MPEG-4 Audio");
  }

  AAC::AAC() {
    memcpy(data + 4, "aac ", 4);
  }

  std::string AAC::toPrettyString(uint32_t indent) {
    return toPrettyAudioString(indent, "[aac ] Advanced Audio Codec");
  }

  HEV1::HEV1() {
    memcpy(data + 4, "hev1", 4);
  }

  std::string HEV1::toPrettyString(uint32_t indent) {
    return toPrettyVisualString(indent, "[hev1] High Efficiency Video Codec 1");
  }

  AVC1::AVC1() {
    memcpy(data + 4, "avc1", 4);
  }

  std::string AVC1::toPrettyString(uint32_t indent) {
    return toPrettyVisualString(indent, "[avc1] Advanced Video Codec 1");
  }

  H264::H264() {
    memcpy(data + 4, "h264", 4);
  }

  std::string H264::toPrettyString(uint32_t indent) {
    return toPrettyVisualString(indent, "[h264] H.264/MPEG-4 AVC");
  }

  FIEL::FIEL(){
    memcpy(data + 4, "fiel", 4);
  }

  void FIEL::setTotal(char newTotal){
    setInt8(newTotal, 0);
  }
  
  char FIEL::getTotal(){
    return getInt8(0);
  }

  void FIEL::setOrder(char newOrder){
    setInt8(newOrder, 1);
  }
  
  char FIEL::getOrder(){
    return getInt8(1);
  }

  std::string FIEL::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[fiel] Video Field Order Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Total: " << (int)getTotal() << std::endl;
    r << std::string(indent + 1, ' ') << "Order: " << (int)getOrder() << std::endl;
    return r.str();
  }

  STSD::STSD(char v, uint32_t f) {
    memcpy(data + 4, "stsd", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void STSD::setEntryCount(uint32_t newEntryCount) {
    setInt32(newEntryCount, 4);
  }

  uint32_t STSD::getEntryCount() {
    return getInt32(4);
  }

  void STSD::setEntry(Box & newContent, uint32_t no) {
    int tempLoc = 8;
    unsigned int entryCount = getEntryCount();
    for (unsigned int i = 0; i < no; i++) {
      if (i < entryCount) {
        tempLoc += getBoxLen(tempLoc);
      } else {
        if (!reserve(tempLoc, 0, (no - entryCount) * 8)) {
          return;
        }
        memset(data + tempLoc, 0, (no - entryCount) * 8);
        tempLoc += (no - entryCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
    if (getEntryCount() < no + 1) {
      setEntryCount(no + 1);
    }
  }

  Box & STSD::getEntry(uint32_t no) {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (no > getEntryCount()) {
      return ret;
    }
    unsigned int i = 0;
    int tempLoc = 8;
    while (i < no) {
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string STSD::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsd] Sample Description Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntrySize: " << getEntryCount() << std::endl;
    for (unsigned int i = 0; i < getEntryCount(); i++) {
      Box curBox = Box(getEntry(i).asBox(), false);
      r << curBox.toPrettyString(indent + 1);
    }
    return r.str();
  }

  GMHD::GMHD() {
    memcpy(data + 4, "gmhd", 4);
  }

  TREF::TREF() {
    memcpy(data + 4, "tref", 4);
  }

  EDTS::EDTS() {
    memcpy(data + 4, "edts", 4);
  }

  UDTA::UDTA() {
    memcpy(data + 4, "udta", 4);
  }

  STSS::STSS(char v, uint32_t f) {
    memcpy(data + 4, "stss", 4);
    setVersion(v);
    setFlags(f);
    setEntryCount(0);
  }

  void STSS::setEntryCount(uint32_t newVal) {
    setInt32(newVal, 4);
  }

  uint32_t STSS::getEntryCount() {
    return getInt32(4);
  }

  void STSS::setSampleNumber(uint32_t newVal, uint32_t index) {
    if (index >= getEntryCount()) {
      setEntryCount(index + 1);
    }
    setInt32(newVal, 8 + (index * 4));
  }

  uint32_t STSS::getSampleNumber(uint32_t index) {
    if (index >= getEntryCount()) {
      return 0;
    }
    return getInt32(8 + (index * 4));
  }

  std::string STSS::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[stss] Sync Sample Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;

    r << std::string(indent + 1, ' ') << "Keyframe sample indexes: ";
    for (unsigned long i = 0; i < getEntryCount(); i++) {
      r << getSampleNumber(i);
      if (i != getEntryCount() - 1){
        r << ", ";
      }
    }
    r << std::endl;
    return r.str();
  }

  META::META() {
    memcpy(data + 4, "meta", 4);
  }

  std::string META::toPrettyString(uint32_t indent) {
    return toPrettyCFBString(indent, "[meta] Meta Box");
  }

  ELST::ELST() {
    memcpy(data + 4, "elst", 4);
  }

  void ELST::setCount(uint32_t newVal){
      setInt32(newVal, 4);
  }
  
  uint32_t ELST::getCount(){
    return getInt32(4);
  }

  void ELST::setSegmentDuration(uint64_t newVal) {
    if (getVersion() == 1) {
      setInt64(newVal, 8);
    } else {
      setInt32(newVal, 8);
    }
  }

  uint64_t ELST::getSegmentDuration() {
    if (getVersion() == 1) {
      return getInt64(8);
    } else {
      return getInt32(8);
    }
  }

  void ELST::setMediaTime(uint64_t newVal) {
    if (getVersion() == 1) {
      setInt64(newVal, 16);
    } else {
      setInt32(newVal, 12);
    }
  }

  uint64_t ELST::getMediaTime() {
    if (getVersion() == 1) {
      return getInt64(16);
    } else {
      return getInt32(12);
    }
  }

  void ELST::setMediaRateInteger(uint16_t newVal) {
    if (getVersion() == 1) {
      setInt16(newVal, 24);
    } else {
      setInt16(newVal, 16);
    }
  }

  uint16_t ELST::getMediaRateInteger() {
    if (getVersion() == 1) {
      return getInt16(24);
    } else {
      return getInt16(16);
    }
  }

  void ELST::setMediaRateFraction(uint16_t newVal) {
    if (getVersion() == 1) {
      setInt16(newVal, 26);
    } else {
      setInt16(newVal, 18);
    }
  }

  uint16_t ELST::getMediaRateFraction() {
    if (getVersion() == 1) {
      return getInt16(26);
    } else {
      return getInt16(18);
    }
  }

  std::string ELST::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[elst] Edit List Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Count: " << getCount() << std::endl;
    r << std::string(indent + 1, ' ') << "SegmentDuration: " << getSegmentDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "MediaTime: " << getMediaTime() << std::endl;
    r << std::string(indent + 1, ' ') << "MediaRateInteger: " << getMediaRateInteger() << std::endl;
    r << std::string(indent + 1, ' ') << "MediaRateFraction: " << getMediaRateFraction() << std::endl;
    return r.str();
  }
}
