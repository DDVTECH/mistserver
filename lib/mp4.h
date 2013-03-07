#pragma once
#include <string>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <stdint.h>
#include <sstream>
#include <deque>
#include <algorithm>
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4 {

  class Box{
    public:
      Box(char * datapointer = 0, bool manage = true);
      ~Box();
      std::string getType();
      bool isType(const char* boxType);
      bool read(std::string & newData);
      uint64_t boxedSize();
      uint64_t payloadSize();
      char * asBox();
      char * payload();
      void clear();
      std::string toPrettyString(int indent = 0);
    protected:
      //integer functions
      void setInt8(char newData, size_t index);
      char getInt8(size_t index);
      void setInt16(short newData, size_t index);
      short getInt16(size_t index);
      void setInt24(uint32_t newData, size_t index);
      uint32_t getInt24(size_t index);
      void setInt32(uint32_t newData, size_t index);
      uint32_t getInt32(size_t index);
      void setInt64(uint64_t newData, size_t index);
      uint64_t getInt64(size_t index);
      //string functions
      void setString(std::string newData, size_t index);
      void setString(char* newData, size_t size, size_t index);
      char * getString(size_t index);
      size_t getStringLen(size_t index);
      //box functions
      Box & getBox(size_t index);
      size_t getBoxLen(size_t index);
      void setBox(Box & newEntry, size_t index);
      //data functions
      bool reserve(size_t position, size_t current, size_t wanted);
      //internal variables
      char * data; ///< Holds the data of this box
      int data_size; ///< Currently reserved size
      bool managed; ///< If false, will not attempt to resize/free the data pointer.
      int payloadOffset; ///<The offset of the payload with regards to the data
  };
  //Box Class

  struct afrt_runtable{
      uint32_t firstFragment;
      uint64_t firstTimestamp;
      uint32_t duration;
      uint32_t discontinuity;
  };
  //fragmentRun

  /// AFRT Box class
  class AFRT: public Box{
    public:
      AFRT();
      void setVersion(char newVersion);
      uint32_t getVersion();
      void setUpdate(uint32_t newUpdate);
      uint32_t getUpdate();
      void setTimeScale(uint32_t newScale);
      uint32_t getTimeScale();
      uint32_t getQualityEntryCount();
      void setQualityEntry(std::string & newQuality, uint32_t no);
      const char * getQualityEntry(uint32_t no);
      uint32_t getFragmentRunCount();
      void setFragmentRun(afrt_runtable newRun, uint32_t no);
      afrt_runtable getFragmentRun(uint32_t no);
      std::string toPrettyString(int indent = 0);
  };
  //AFRT Box

  struct asrt_runtable{
      uint32_t firstSegment;
      uint32_t fragmentsPerSegment;
  };

  /// ASRT Box class
  class ASRT: public Box{
    public:
      ASRT();
      void setVersion(char newVersion);
      uint32_t getVersion();
      void setUpdate(uint32_t newUpdate);
      uint32_t getUpdate();
      uint32_t getQualityEntryCount();
      void setQualityEntry(std::string & newQuality, uint32_t no);
      const char* getQualityEntry(uint32_t no);
      uint32_t getSegmentRunEntryCount();
      void setSegmentRun(uint32_t firstSegment, uint32_t fragmentsPerSegment, uint32_t no);
      asrt_runtable getSegmentRun(uint32_t no);
      std::string toPrettyString(int indent = 0);
  };
  //ASRT Box

  /// ABST Box class
  class ABST: public Box{
    public:
      ABST();
      void setVersion(char newVersion);
      char getVersion();
      void setFlags(uint32_t newFlags);
      uint32_t getFlags();
      void setBootstrapinfoVersion(uint32_t newVersion);
      uint32_t getBootstrapinfoVersion();
      void setProfile(char newProfile);
      char getProfile();
      void setLive(bool newLive);
      bool getLive();
      void setUpdate(bool newUpdate);
      bool getUpdate();
      void setTimeScale(uint32_t newTimeScale);
      uint32_t getTimeScale();
      void setCurrentMediaTime(uint64_t newTime);
      uint64_t getCurrentMediaTime();
      void setSmpteTimeCodeOffset(uint64_t newTime);
      uint64_t getSmpteTimeCodeOffset();
      void setMovieIdentifier(std::string & newIdentifier);
      char * getMovieIdentifier();
      uint32_t getServerEntryCount();
      void setServerEntry(std::string & entry, uint32_t no);
      const char * getServerEntry(uint32_t no);
      uint32_t getQualityEntryCount();
      void setQualityEntry(std::string & entry, uint32_t no);
      const char * getQualityEntry(uint32_t no);
      void setDrmData(std::string newDrm);
      char * getDrmData();
      void setMetaData(std::string newMetaData);
      char * getMetaData();
      uint32_t getSegmentRunTableCount();
      void setSegmentRunTable(ASRT & table, uint32_t no);
      ASRT & getSegmentRunTable(uint32_t no);
      uint32_t getFragmentRunTableCount();
      void setFragmentRunTable(AFRT & table, uint32_t no);
      AFRT & getFragmentRunTable(uint32_t no);
      std::string toPrettyString(uint32_t indent = 0);
  };
  //ABST Box

  class MFHD: public Box{
    public:
      MFHD();
      void setSequenceNumber(uint32_t newSequenceNumber);
      uint32_t getSequenceNumber();
      std::string toPrettyString(int indent = 0);
  };
  //MFHD Box

  class MOOF: public Box{
    public:
      MOOF();
      uint32_t getContentCount();
      void setContent(Box & newContent, uint32_t no);
      Box & getContent(uint32_t no);
      std::string toPrettyString(int indent = 0);
  };
  //MOOF Box

  class TRAF: public Box{
    public:
      TRAF();
      uint32_t getContentCount();
      void setContent(Box & newContent, uint32_t no);
      Box & getContent(uint32_t no);
      std::string toPrettyString(int indent = 0);
  };
  //TRAF Box

  struct trunSampleInformation{
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
  class TRUN: public Box{
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
  };
  class TFHD: public Box{
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
      std::string toPrettyString(uint32_t indent = 0);
  };

  struct afraentry{
      uint64_t time;
      uint64_t offset;
  };
  struct globalafraentry{
      uint64_t time;
      uint32_t segment;
      uint32_t fragment;
      uint64_t afraoffset;
      uint64_t offsetfromafra;
  };
  class AFRA: public Box{
    public:
      AFRA();
      void setVersion(uint32_t newVersion);
      uint32_t getVersion();
      void setFlags(uint32_t newFlags);
      uint32_t getFlags();
      void setLongIDs(bool newVal);
      bool getLongIDs();
      void setLongOffsets(bool newVal);
      bool getLongOffsets();
      void setGlobalEntries(bool newVal);
      bool getGlobalEntries();
      void setTimeScale(uint32_t newVal);
      uint32_t getTimeScale();
      uint32_t getEntryCount();
      void setEntry(afraentry newEntry, uint32_t no);
      afraentry getEntry(uint32_t no);
      uint32_t getGlobalEntryCount();
      void setGlobalEntry(globalafraentry newEntry, uint32_t no);
      globalafraentry getGlobalEntry(uint32_t no);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class AVCC: public Box{
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
      void setSPSNumber(uint32_t newSPSNumber);
      uint32_t getSPSNumber();
      void setSPS(std::string newSPS);
      uint32_t getSPSLen();
      char* getSPS();
      void setPPSNumber(uint32_t newPPSNumber);
      uint32_t getPPSNumber();
      void setPPS(std::string newPPS);
      uint32_t getPPSLen();
      char* getPPS();
      std::string asAnnexB();
      void setPayload(std::string newPayload);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class SDTP: public Box{
    public:
      SDTP();
      void setVersion(uint32_t newVersion);
      uint32_t getVersion();
      void setValue(uint32_t newValue, size_t index);
      uint32_t getValue(size_t index);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID: public Box{
    public:
      UUID();
      std::string getUUID();
      void setUUID(const std::string & uuid_string);
      void setUUID(const char * raw_uuid);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID_TrackFragmentReference: public UUID{
    public:
      UUID_TrackFragmentReference();
      void setVersion(uint32_t newVersion);
      uint32_t getVersion();
      void setFlags(uint32_t newFlags);
      uint32_t getFlags();
      void setFragmentCount(uint32_t newCount);
      uint32_t getFragmentCount();
      void setTime(size_t num, uint64_t newTime);
      uint64_t getTime(size_t num);
      void setDuration(size_t num, uint64_t newDuration);
      uint64_t getDuration(size_t num);
      std::string toPrettyString(uint32_t indent = 0);
  };

}

