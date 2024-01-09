#pragma once
#include "mp4.h"

namespace h265{
  struct metaInfo;
}

namespace MP4{
  class MFHD : public Box{
  public:
    MFHD(uint32_t sequenceNumber = 0);
    void setSequenceNumber(uint32_t newSequenceNumber);
    uint32_t getSequenceNumber();
    std::string toPrettyString(uint32_t indent = 0);
  };
  // MFHD Box

  class MOOF : public containerBox{
  public:
    MOOF();
  };
  // MOOF Box

  class TRAF : public containerBox{
  public:
    TRAF();
  };
  // TRAF Box

  class BTRT: public Box {
    public:
      BTRT();
      uint32_t getDecodingBufferSize();
      void setDecodingBufferSize(uint32_t val);
      uint32_t getMaxBitrate();
      void setMaxBitrate(uint32_t val);
      uint32_t getAverageBitrate();
      void setAverageBitrate(uint32_t val);
      std::string toPrettyString(uint32_t indent = 0);
  };

  struct trunSampleInformation {
    uint32_t sampleDuration;
    uint32_t sampleSize;
    uint32_t sampleFlags;
    uint32_t sampleOffset;
  };
  enum trunflags{
    trundataOffset = 0x00000001,
    trunfirstSampleFlags = 0x00000004,
    trunsampleDuration = 0x00000100,
    trunsampleSize = 0x00000200,
    trunsampleFlags = 0x00000400,
    trunsampleOffsets = 0x00000800
  };
  enum sampleflags{
    noIPicture = 0x01000000,
    isIPicture = 0x02000000,
    noDisposable = 0x00400000,
    isDisposable = 0x00800000,
    isRedundant = 0x00100000,
    noRedundant = 0x00200000,
    noKeySample = 0x00010000,
    isKeySample = 0x00000000,
    MUST_BE_PRESENT = 0x1
  };
  std::string prettySampleFlags(uint32_t flag);
  class TRUN : public Box{
  public:
    TRUN();
    void setFlags(uint32_t newFlags);
    uint32_t getFlags();
    void setDataOffset(uint32_t newOffset);
    uint32_t getDataOffset();
    void setFirstSampleFlags(uint32_t newSampleFlags);
    uint32_t getFirstSampleFlags();
    uint32_t getSampleInformationCount();
    void setSampleInformation(trunSampleInformation newSample, uint32_t no);
    trunSampleInformation getSampleInformation(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  enum tfhdflags{
    tfhdBaseOffset = 0x000001,
    tfhdSampleDesc = 0x000002,
    tfhdSampleDura = 0x000008,
    tfhdSampleSize = 0x000010,
    tfhdSampleFlag = 0x000020,
    tfhdNoDuration = 0x010000,
    tfhdBaseIsMoof = 0x020000,
  };
  class TFHD : public Box{
  public:
    TFHD();
    void setFlags(uint32_t newFlags);
    uint32_t getFlags();
    void setTrackID(uint32_t newID);
    uint32_t getTrackID();
    void setBaseDataOffset(uint64_t newOffset);
    uint64_t getBaseDataOffset();
    void setSampleDescriptionIndex(uint32_t newIndex);
    uint32_t getSampleDescriptionIndex();
    void setDefaultSampleDuration(uint32_t newDuration);
    uint32_t getDefaultSampleDuration();
    void setDefaultSampleSize(uint32_t newSize);
    uint32_t getDefaultSampleSize();
    void setDefaultSampleFlags(uint32_t newFlags);
    uint32_t getDefaultSampleFlags();
    bool getDefaultBaseIsMoof();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class AVCC : public Box{
  public:
    AVCC();
    void setVersion(uint32_t newVersion);
    uint32_t getVersion();
    void setProfile(uint32_t newProfile);
    uint32_t getProfile();
    void setCompatibleProfiles(uint32_t newCompatibleProfiles);
    uint32_t getCompatibleProfiles();
    void setLevel(uint32_t newLevel);
    uint32_t getLevel();

    void setSPSCount(uint32_t _count);
    uint32_t getSPSCount();
    void setSPS(std::string newSPS, size_t index = 0);
    void setSPS(const char *data, size_t len, size_t index = 0);
    uint32_t getSPSLen(size_t index = 0);
    char *getSPS(size_t index = 0);
    std::string hexSPS(size_t index = 0);

    size_t PPSCountOffset();
    void setPPSCount(uint32_t _count);
    uint32_t getPPSCount();
    void setPPS(std::string newPPS, size_t index = 0);
    void setPPS(const char *data, size_t len, size_t index = 0);
    uint32_t getPPSLen(size_t index = 0);
    char *getPPS(size_t index = 0);
    void multiplyPPS(size_t newAmount);
    std::string hexPPS(size_t index = 0);
    std::string asAnnexB();
    void setPayload(std::string newPayload);
    void setPayload(const char *data, size_t len);

    bool sanitize();

    std::string toPrettyString(uint32_t indent = 0);
  };

  struct HVCCArrayEntry{
    char arrayCompleteness;
    char nalUnitType;
    std::deque<std::string> nalUnits;
  };

  class HVCC : public Box{
  public:
    HVCC();
    void setConfigurationVersion(char newVersion);
    char getConfigurationVersion();
    void setGeneralProfileSpace(char newGeneral);
    char getGeneralProfileSpace();
    void setGeneralTierFlag(char newGeneral);
    char getGeneralTierFlag();
    void setGeneralProfileIdc(char newGeneral);
    char getGeneralProfileIdc();
    void setGeneralProfileCompatibilityFlags(unsigned long newGeneral);
    unsigned long getGeneralProfileCompatibilityFlags();
    void setGeneralConstraintIndicatorFlags(unsigned long long newGeneral);
    unsigned long long getGeneralConstraintIndicatorFlags();
    void setGeneralLevelIdc(char newGeneral);
    char getGeneralLevelIdc();
    void setMinSpatialSegmentationIdc(short newIdc);
    short getMinSpatialSegmentationIdc();
    void setParallelismType(char newType);
    char getParallelismType();
    void setChromaFormat(char newFormat);
    char getChromaFormat();
    void setBitDepthLumaMinus8(char newBitDepthLumaMinus8);
    char getBitDepthLumaMinus8();
    void setBitDepthChromaMinus8(char newBitDepthChromaMinus8);
    char getBitDepthChromaMinus8();
    void setAverageFramerate(short newFramerate);
    short getAverageFramerate();
    void setConstantFramerate(char newFramerate);
    char getConstantFramerate();
    void setNumberOfTemporalLayers(char newNumber);
    char getNumberOfTemporalLayers();
    void setTemporalIdNested(char newNested);
    char getTemporalIdNested();
    void setLengthSizeMinus1(char newLengthSizeMinus1);
    char getLengthSizeMinus1();
    ///\todo Add setter for the array entries
    void setArrays(std::deque<HVCCArrayEntry> &arrays);
    std::deque<HVCCArrayEntry> getArrays();
    std::string asAnnexB();
    void setPayload(std::string newPayload);
    std::string toPrettyString(uint32_t indent = 0);
    h265::metaInfo getMetaInfo();
  };

  class AV1C : public Box{
  public:
    AV1C();
    void setPayload(std::string newPayload);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class Descriptor{
  public:
    Descriptor();
    Descriptor(const char *pointer, const unsigned long length, const bool master = false);
    char getTag();                                   ///< Get type of descriptor
    void setTag(char t);                             ///< Set type of descriptor
    unsigned long getDataSize();                     ///< Get internal size of descriptor
    unsigned long getFullSize();                     ///< Get external size of descriptor
    void resize(unsigned long t);                    ///< Resize descriptor
    char *getData();                                 ///< Returns pointer to start of internal data
    std::string toPrettyString(uint32_t indent = 0); ///< put it into a pretty string
  protected:
    unsigned long size; ///< Length of data
    char *data;         ///< Pointer to data in memory
    bool master;
  };

  /// Implements ISO 14496-1 DecSpecificInfoTag
  class DSDescriptor : public Descriptor{
  public:
    DSDescriptor(const char *pointer, const unsigned long length, const bool master = false);
    std::string toPrettyString(uint32_t indent = 0); ///< put it into a pretty string
    std::string toString(); ///< Return decoder specific info as standard string in binary format.
  };

  /// Implements ISO 14496-1 DecoderConfigDescrTag
  class DCDescriptor : public Descriptor{
  public:
    DCDescriptor(const char *pointer, const unsigned long length, const bool master = false);
    bool isAAC(); ///< Returns true if this track is AAC.
    std::string getCodec();
    DSDescriptor getSpecific();
    std::string toPrettyString(uint32_t indent = 0); ///< put it into a pretty string
  };

  /// Implements ISO 14496-1 ES_DescrTag
  class ESDescriptor : public Descriptor{
  public:
    ESDescriptor(const char *pointer, const unsigned long length, const bool master = false);
    DCDescriptor getDecoderConfig();
    std::string toPrettyString(uint32_t indent = 0); ///< put it into a pretty string
  };

  /// Implements ISO 14496-1 SLConfigDescrTag
  class SLCDescriptor : public Descriptor{};

  class ESDS : public fullBox{
  public:
    ESDS();
    ESDS(const DTSC::Meta & M, size_t idx);
    ESDescriptor getESDescriptor();
    bool isAAC();
    std::string getCodec();
    std::string getInitData();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class DAC3 : public Box{
  public:
    DAC3(unsigned int rate, unsigned int channels);
    char getSampleRateCode();                          // 2bits
    void setSampleRateCode(char newVal);               // 2bits
    char getBitStreamIdentification();                 // 5bits
    void setBitStreamIdentification(char newVal);      // 5bits
    char getBitStreamMode();                           // 3 bits
    void setBitStreamMode(char newVal);                // 3 bits
    char getAudioConfigMode();                         // 3 bits
    void setAudioConfigMode(char newVal);              // 3 bits
    bool getLowFrequencyEffectsChannelOn();            // 1bit
    void setLowFrequencyEffectsChannelOn(bool newVal); // 1bit
    char getFrameSizeCode();                           // 6bits
    void setFrameSizeCode(char newVal);                // 6bits
    std::string toPrettyString(uint32_t indent = 0);
  };

  class FTYP : public Box{
  public:
    FTYP(bool fillDefaults = true);
    void setMajorBrand(const char *newMajorBrand);
    std::string getMajorBrand();
    void setMinorVersion(const char *newMinorVersion);
    std::string getMinorVersionHex();
    std::string getMinorVersion();
    size_t getCompatibleBrandsCount();
    void setCompatibleBrands(const char *newCompatibleBrand, size_t index);
    std::string getCompatibleBrands(size_t index);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STYP : public FTYP{
  public:
    STYP(bool fillDefaults = true);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class MOOV : public containerBox{
  public:
    MOOV();
  };

  class MVEX : public containerBox{
  public:
    MVEX();
  };

  class TREX : public fullBox{
  public:
    TREX(unsigned int trackId = 0);
    void setTrackID(uint32_t newTrackID);
    uint32_t getTrackID();
    void setDefaultSampleDescriptionIndex(uint32_t newDefaultSampleDescriptionIndex);
    uint32_t getDefaultSampleDescriptionIndex();
    void setDefaultSampleDuration(uint32_t newDefaultSampleDuration);
    uint32_t getDefaultSampleDuration();
    void setDefaultSampleSize(uint32_t newDefaultSampleSize);
    uint32_t getDefaultSampleSize();
    void setDefaultSampleFlags(uint32_t newDefaultSampleFlags);
    uint32_t getDefaultSampleFlags();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class MFRA : public containerBox{
  public:
    MFRA();
  };

  class TRAK : public containerBox{
  public:
    TRAK();
  };

  class MDIA : public containerBox{
  public:
    MDIA();
  };

  class MINF : public containerBox{
  public:
    MINF();
  };

  class DINF : public containerBox{
  public:
    DINF();
  };

  class WAVE : public containerBox{
  public:
    WAVE();
  };

  class MFRO : public Box{
  public:
    MFRO();
    void setSize(uint32_t newSize);
    uint32_t getSize();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class HDLR : public Box{
  public:
    HDLR(const std::string &type = "", const std::string &name = "");
    void setHandlerType(const char *newHandlerType);
    std::string getHandlerType();
    void setName(std::string newName);
    std::string getName();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class VMHD : public fullBox{
  public:
    VMHD(uint32_t version = 0, uint32_t flags = 0);
    void setGraphicsMode(uint16_t newGraphicsMode);
    uint16_t getGraphicsMode();
    uint32_t getOpColorCount();
    void setOpColor(uint16_t newOpColor, size_t index);
    uint16_t getOpColor(size_t index);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SMHD : public fullBox{
  public:
    SMHD();
    void setBalance(int16_t newBalance);
    int16_t getBalance();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STHD : public fullBox{
  public:
    STHD();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class HMHD : public fullBox{
  public:
    HMHD();
    void setMaxPDUSize(uint16_t newMaxPDUSize);
    uint16_t getMaxPDUSize();
    void setAvgPDUSize(uint16_t newAvgPDUSize);
    uint16_t getAvgPDUSize();
    void setMaxBitRate(uint32_t newMaxBitRate);
    uint32_t getMaxBitRate();
    void setAvgBitRate(uint32_t newAvgBitRate);
    uint32_t getAvgBitRate();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class NMHD : public fullBox{
  public:
    NMHD();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class MEHD : public fullBox{
  public:
    MEHD();
    void setFragmentDuration(uint64_t newFragmentDuration);
    uint64_t getFragmentDuration();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STBL : public containerBox{
  public:
    STBL();
  };

  class URL : public fullBox{
  public:
    URL();
    void setLocation(std::string newLocation);
    std::string getLocation();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class URN : public fullBox{
  public:
    URN();
    void setName(std::string newName);
    std::string getName();
    void setLocation(std::string newLocation);
    std::string getLocation();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class DREF : public fullBox{
  public:
    DREF();
    uint32_t getEntryCount();
    void setDataEntry(fullBox &newDataEntry, size_t index);
    Box &getDataEntry(size_t index);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class MVHD : public fullBox{
  public:
    MVHD(long long unsigned int duration);
    void setCreationTime(uint64_t newCreationTime);
    uint64_t getCreationTime();
    void setModificationTime(uint64_t newModificationTime);
    uint64_t getModificationTime();
    void setTimeScale(uint32_t newTimeScale);
    uint32_t getTimeScale();
    void setDuration(uint64_t newDuration);
    uint64_t getDuration();
    void setRate(uint32_t newRate);
    uint32_t getRate();
    void setVolume(uint16_t newVolume);
    uint16_t getVolume();
    uint32_t getMatrixCount();
    void setMatrix(double newMatrix, size_t index);
    double getMatrix(size_t index);
    uint16_t getRotation();
    void setTrackID(uint32_t newTrackID);
    uint32_t getTrackID();
    std::string toPrettyString(uint32_t indent = 0);
  };

  struct TFRAEntry{
    uint64_t time;
    uint64_t moofOffset;
    uint32_t trafNumber;
    uint32_t trunNumber;
    uint32_t sampleNumber;
  };

  class TFRA : public fullBox{
  public:
    TFRA();
    void setTrackID(uint32_t newTrackID);
    uint32_t getTrackID();
    void setLengthSizeOfTrafNum(char newVal);
    char getLengthSizeOfTrafNum();
    void setLengthSizeOfTrunNum(char newVal);
    char getLengthSizeOfTrunNum();
    void setLengthSizeOfSampleNum(char newVal);
    char getLengthSizeOfSampleNum();
    void setNumberOfEntry(uint32_t newNumberOfEntry);
    uint32_t getNumberOfEntry();
    void setTFRAEntry(TFRAEntry newTFRAEntry, uint32_t no);
    TFRAEntry &getTFRAEntry(uint32_t no);
    uint32_t getTFRAEntrySize();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class TKHD : public fullBox{
  public:
    TKHD(const Box &rs) : fullBox(rs){}
    TKHD(uint32_t trackId = 0, uint64_t duration = 0, uint32_t width = 0, uint32_t height = 0);
    TKHD(const DTSC::Meta &M, size_t idx);

    void setCreationTime(uint64_t newCreationTime);
    uint64_t getCreationTime();
    void setModificationTime(uint64_t newModificationTime);
    uint64_t getModificationTime();
    void setTrackID(uint32_t newTrackID);
    uint32_t getTrackID();
    void setDuration(uint64_t newDuration);
    uint64_t getDuration();

    void setLayer(uint16_t newLayer);
    uint16_t getLayer();
    void setAlternateGroup(uint16_t newAlternateGroup);
    uint16_t getAlternateGroup();

    void setVolume(uint16_t newVolume);
    uint16_t getVolume();
    uint32_t getMatrixCount();
    void setMatrix(int32_t newMatrix, size_t index);
    int32_t getMatrix(size_t index);
    uint16_t getRotation();

    void setWidth(double newWidth);
    double getWidth();
    void setHeight(double newHeight);
    double getHeight();
    std::string toPrettyString(uint32_t indent = 0);

  protected:
    void initialize();
  };

  class MDHD : public fullBox{
  public:
    MDHD(uint64_t duration = 0, const std::string &language = "");
    void setCreationTime(uint64_t newCreationTime);
    uint64_t getCreationTime();
    void setModificationTime(uint64_t newModificationTime);
    uint64_t getModificationTime();
    void setTimeScale(uint32_t newTimeScale);
    uint32_t getTimeScale();
    void setDuration(uint64_t newDuration);
    uint64_t getDuration();
    void setLanguage(uint16_t newLanguage);
    uint16_t getLanguageInt();
    void setLanguage(const std::string &newLanguage);
    std::string getLanguage();
    std::string toPrettyString(uint32_t indent = 0);
  };

  struct STTSEntry{
    uint32_t sampleCount;
    uint32_t sampleDelta;
  };

  class STTS : public fullBox{
  public:
    STTS(char v = 1, uint32_t f = 0);
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setSTTSEntry(STTSEntry newSTTSEntry, uint32_t no);
    STTSEntry getSTTSEntry(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  struct CTTSEntry{
    uint32_t sampleCount;
    int32_t sampleOffset;
  };

  class CTTS : public fullBox{
  public:
    CTTS();
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setCTTSEntry(CTTSEntry newCTTSEntry, uint32_t no);
    CTTSEntry getCTTSEntry(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STSCEntry{
  public:
    STSCEntry(unsigned int _first = 0, unsigned int _count = 0, unsigned int _index = 0);
    uint32_t firstChunk;
    uint32_t samplesPerChunk;
    uint32_t sampleDescriptionIndex;
  };

  class STSC : public fullBox{
  public:
    STSC(char v = 1, uint32_t f = 0);
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setSTSCEntry(STSCEntry newSTSCEntry, uint32_t no);
    STSCEntry getSTSCEntry(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STCO : public fullBox{
  public:
    STCO(const Box &rs) : fullBox(rs){}
    STCO(char v = 1, uint32_t f = 0);
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setChunkOffset(uint32_t newChunkOffset, uint32_t no);
    uint32_t getChunkOffset(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class CO64 : public fullBox{
  public:
    CO64(char v = 1, uint32_t f = 0);
    CO64(const Box &rs) : fullBox(rs){}
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setChunkOffset(uint64_t newChunkOffset, uint32_t no);
    uint64_t getChunkOffset(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STSZ : public fullBox{
  public:
    STSZ(char v = 1, uint32_t f = 0);
    void setSampleSize(uint32_t newSampleSize);
    uint32_t getSampleSize();
    void setSampleCount(uint32_t newSampleCount);
    uint32_t getSampleCount();
    void setEntrySize(uint32_t newEntrySize, uint32_t no);
    uint32_t getEntrySize(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class SampleEntry : public Box{
  public:
    SampleEntry();
    void setDataReferenceIndex(uint16_t newDataReferenceIndex);
    uint16_t getDataReferenceIndex();
    std::string toPrettySampleString(uint32_t index);
  };

  class CLAP : public Box{// CleanApertureBox
  public:
    CLAP();
    void setCleanApertureWidthN(uint32_t newVal);
    uint32_t getCleanApertureWidthN();
    void setCleanApertureWidthD(uint32_t newVal);
    uint32_t getCleanApertureWidthD();
    void setCleanApertureHeightN(uint32_t newVal);
    uint32_t getCleanApertureHeightN();
    void setCleanApertureHeightD(uint32_t newVal);
    uint32_t getCleanApertureHeightD();
    void setHorizOffN(uint32_t newVal);
    uint32_t getHorizOffN();
    void setHorizOffD(uint32_t newVal);
    uint32_t getHorizOffD();
    void setVertOffN(uint32_t newVal);
    uint32_t getVertOffN();
    void setVertOffD(uint32_t newVal);
    uint32_t getVertOffD();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class PASP : public Box{// PixelAspectRatioBox
  public:
    PASP(uint32_t hSpacing = 1, uint32_t vSpacing = 1);
    void setHSpacing(uint32_t newVal);
    uint32_t getHSpacing();
    void setVSpacing(uint32_t newVal);
    uint32_t getVSpacing();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class VisualSampleEntry : public SampleEntry{
    ///\todo set default values
  public:
    VisualSampleEntry();
    VisualSampleEntry(const DTSC::Meta &M, size_t idx);
    void initialize();
    void setCodec(const char *newCodec);
    std::string getCodec();
    void setWidth(uint16_t newWidth);
    uint16_t getWidth();
    void setHeight(uint16_t newHeight);
    uint16_t getHeight();
    void setHorizResolution(double newHorizResolution);
    double getHorizResolution();
    void setVertResolution(double newVertResolution);
    double getVertResolution();
    void setFrameCount(uint16_t newFrameCount);
    uint16_t getFrameCount();
    void setCompressorName(std::string newCompressorName);
    std::string getCompressorName();
    void setDepth(uint16_t newDepth);
    uint16_t getDepth();
    Box &getCLAP();
    void setCLAP(Box &clap);
    Box &getPASP();
    void setPASP(Box &pasp);

    size_t getBoxEntryCount();
    Box &getBoxEntry(size_t index);
    void setBoxEntry(size_t index, Box &box);

    std::string toPrettyVisualString(uint32_t index = 0, std::string = "");
  };

  class AudioSampleEntry : public SampleEntry{
  public:
    ///\todo set default values
    AudioSampleEntry();
    AudioSampleEntry(const DTSC::Meta &M, size_t idx);
    uint16_t getVersion() const;
    void initialize();
    void setCodec(const char *newCodec);
    void setChannelCount(uint16_t newChannelCount);
    uint16_t getChannelCount();
    void setSampleSize(uint16_t newSampleSize);
    uint16_t getSampleSize();
    void setPreDefined(uint16_t newPreDefined);
    uint16_t getPreDefined();
    void setSampleRate(uint32_t newSampleRate);
    uint16_t toAACInit();
    uint32_t getSampleRate();
    size_t getBoxOffset() const;
    void setCodecBox(Box &newBox);
    Box &getCodecBox();
    Box &getSINFBox(); /*LTS*/

    size_t getBoxEntryCount();
    Box &getBoxEntry(size_t index);
    void setBoxEntry(size_t index, Box &box);

    std::string toPrettyAudioString(uint32_t indent = 0, std::string name = "");
  };

  struct BoxRecord{
    int16_t top;
    int16_t left;
    int16_t bottom;
    int16_t right;
  };

  struct StyleRecord{
    uint16_t startChar;
    uint16_t endChar;
    uint16_t font_id;
    uint8_t face_style_flags;
    uint8_t font_size;
    uint8_t text_color_rgba[4];
  };

  class FontRecord{
  public:
    FontRecord();
    ~FontRecord();
    uint16_t font_id;
    uint8_t font_name_length;
    char *font;
    // uint8_t font[font_name_length];

    void setFont(const char *f);
  };

  class FontTableBox : public Box{
  public:
    FontTableBox();
    void setEntryCount(uint16_t);
    uint16_t getEntryCount();

    void setFontRecord(FontRecord f);
    FontRecord font_entry[1];
    void setFontId(uint16_t id);
    // FontRecord font_entry[entry_count];
  };

  class TextSampleEntry : public SampleEntry{
  public:
    TextSampleEntry();
    TextSampleEntry(const DTSC::Meta &m, size_t idx);
    void initialize();
    void setHzJustification(int8_t n);
    void setVtJustification(int8_t n);
    uint32_t getDisplayFlags();
    void setDisplayFlags(uint32_t flags);
    int8_t getHzJustification();
    int8_t getVtJustification();
    uint8_t getBackGroundColorRGBA(uint8_t n = 0);
    void setBackGroundColorRGBA(uint8_t pos, uint8_t value);

    void setCodec(const char *newCodec);
    void setCodecBox(Box &newBox);
    Box &getCodecBox();

    BoxRecord getBoxRecord();
    void setBoxRecord(BoxRecord b);

    StyleRecord getStyleRecord();
    void setStyleRecord(StyleRecord s);

    FontTableBox getFontTableBox();
    void setFontTableBox(FontTableBox f);

    std::string toPrettyTextString(uint32_t indent = 0, std::string name = "");
  };

  class TX3G : public TextSampleEntry{
  public:
    TX3G();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class MP4A : public AudioSampleEntry{
  public:
    MP4A();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class AAC : public AudioSampleEntry{
  public:
    AAC();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class HEV1 : public VisualSampleEntry{
  public:
    HEV1();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class AVC1 : public VisualSampleEntry{
  public:
    AVC1();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class H264 : public VisualSampleEntry{
  public:
    H264();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class AV01 : public VisualSampleEntry{
  public:
    AV01();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class FIEL : public Box{
  public:
    FIEL();
    void setTotal(char newTotal);
    char getTotal();
    void setOrder(char newOrder);
    char getOrder();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class STSD : public fullBox{
  public:
    STSD(char v = 1, uint32_t f = 0);
    void setEntryCount(uint32_t newEntryCount);
    uint32_t getEntryCount();
    void setEntry(Box &newContent, uint32_t no);
    Box &getEntry(uint32_t no);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class GMHD : public containerBox{
  public:
    GMHD();
  };

  class TREF : public containerBox{
  public:
    TREF();
  };

  class EDTS : public containerBox{
  public:
    EDTS();
  };

  class UDTA : public containerBox{
  public:
    UDTA();
  };

  class STSS : public fullBox{
  public:
    STSS(char v = 1, uint32_t f = 0);
    void setEntryCount(uint32_t newVal);
    uint32_t getEntryCount();
    void setSampleNumber(uint32_t newVal, uint32_t index);
    uint32_t getSampleNumber(uint32_t index);
    std::string toPrettyString(uint32_t indent = 0);
  };

  class META : public containerFullBox{
  public:
    META();
    std::string toPrettyString(uint32_t indent = 0);
  };

  class ELST : public fullBox{
  public:
    ELST();
    void setCount(uint32_t newVal);
    uint32_t getCount();
    void setSegmentDuration(uint32_t cnt, uint64_t newVal);
    uint64_t getSegmentDuration(uint32_t cnt);
    void setMediaTime(uint32_t cnt, uint64_t newVal);
    uint64_t getMediaTime(uint32_t cnt);
    void setMediaRateInteger(uint32_t cnt, uint16_t newVal);
    uint16_t getMediaRateInteger(uint32_t cnt);
    void setMediaRateFraction(uint32_t cnt, uint16_t newVal);
    uint16_t getMediaRateFraction(uint32_t cnt);
    std::string toPrettyString(uint32_t indent = 0);
  };
}// namespace MP4
