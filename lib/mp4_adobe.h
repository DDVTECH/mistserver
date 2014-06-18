#pragma once
#include "mp4.h"
#include <stdint.h>

namespace MP4 {
  //class Box;

  struct afrt_runtable {
    uint32_t firstFragment;
    uint64_t firstTimestamp;
    uint32_t duration;
    uint32_t discontinuity;
  };
  //fragmentRun

  /// AFRT Box class
  class AFRT: public Box {
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
      std::string toPrettyString(uint32_t indent = 0);
  };
  //AFRT Box

  struct asrt_runtable {
    uint32_t firstSegment;
    uint32_t fragmentsPerSegment;
  };

  /// ASRT Box class
  class ASRT: public Box {
    public:
      ASRT();
      void setVersion(char newVersion);
      uint32_t getVersion();
      void setUpdate(uint32_t newUpdate);
      uint32_t getUpdate();
      uint32_t getQualityEntryCount();
      void setQualityEntry(std::string & newQuality, uint32_t no);
      const char * getQualityEntry(uint32_t no);
      uint32_t getSegmentRunEntryCount();
      void setSegmentRun(uint32_t firstSegment, uint32_t fragmentsPerSegment, uint32_t no);
      asrt_runtable getSegmentRun(uint32_t no);
      std::string toPrettyString(uint32_t indent = 0);
  };
  //ASRT Box

  /// ABST Box class
  class ABST: public Box {
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

  struct afraentry {
    uint64_t time;
    uint64_t offset;
  };
  struct globalafraentry {
    uint64_t time;
    uint32_t segment;
    uint32_t fragment;
    uint64_t afraoffset;
    uint64_t offsetfromafra;
  };
  class AFRA: public Box {
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

}

