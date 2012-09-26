#pragma once
#include <string>
#include <iostream>
#include <cstdio>
#include <stdint.h>
#include <sstream>
#include <deque>
#include <algorithm>
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4{

  class Box {
    public:
      Box(char * datapointer = 0, bool manage = true);
      ~Box();
      std::string getType();
      bool isType( char* boxType );
      bool read(std::string & newData);
      long long int boxedSize();
      long long int payloadSize();
      char * asBox();
      char * payload();
      void clear();
      std::string toPrettyString( int indent = 0 );
    protected:
      //integer functions
      void setInt8( char newData, size_t index );
      char getInt8( size_t index );
      void setInt16( short newData, size_t index );
      short getInt16( size_t index );
      void setInt24( long newData, size_t index );
      long getInt24( size_t index );
      void setInt32( long newData, size_t index );
      long getInt32( size_t index );
      void setInt64( long long int newData, size_t index );
      long long int getInt64( size_t index );
      //string functions
      void setString(std::string newData, size_t index );
      void setString(char* newData, size_t size, size_t index );
      char * getString(size_t index);
      size_t getStringLen(size_t index);
      //box functions
      Box getBox(size_t index);
      //data functions
      bool reserve(size_t position, size_t current, size_t wanted);
      //internal variables
      char * data; ///< Holds the data of this box
      int data_size; ///< Currently reserved size
      bool managed; ///< If false, will not attempt to resize/free the data pointer.
      int payloadOffset;///<The offset of the payload with regards to the data
  };//Box Class

  struct afrt_runtable {
    long firstFragment;
    long long int firstTimestamp;
    long duration;
    long discontinuity;
  };//fragmentRun

  /// AFRT Box class
  class AFRT : public Box {
    public:
      AFRT();
      void setVersion( char newVersion );
      long getVersion();
      void setUpdate( long newUpdate );
      long getUpdate();
      void setTimeScale( long newScale );
      long getTimeScale();
      long getQualityEntryCount();
      void setQualityEntry( std::string & newQuality, long no );
      const char * getQualityEntry( long no );
      long getFragmentRunCount();
      void setFragmentRun( afrt_runtable newRun, long no );
      afrt_runtable getFragmentRun( long no );
      std::string toPrettyString(int indent = 0);
  };//AFRT Box

  struct asrt_runtable{
    long firstSegment;
    long fragmentsPerSegment;
  };

  /// ASRT Box class
  class ASRT : public Box {
    public:
      ASRT();
      void setVersion( char newVersion );
      long getVersion();
      void setUpdate( long newUpdate );
      long getUpdate();
      long getQualityEntryCount();
      void setQualityEntry( std::string & newQuality, long no );
      const char* getQualityEntry( long no );
      long getSegmentRunEntryCount();
      void setSegmentRun( long firstSegment, long fragmentsPerSegment, long no );
      asrt_runtable getSegmentRun( long no );
      std::string toPrettyString(int indent = 0);
  };//ASRT Box
  
  /// ABST Box class
  class ABST: public Box {
    public:
      ABST();
      void setVersion(char newVersion);
      char getVersion();
      void setFlags(long newFlags);
      long getFlags();
      void setBootstrapinfoVersion(long newVersion);
      long getBootstrapinfoVersion();
      void setProfile(char newProfile);
      char getProfile();
      void setLive(bool newLive);
      bool getLive();
      void setUpdate(bool newUpdate);
      bool getUpdate();
      void setTimeScale(long newTimeScale);
      long getTimeScale();
      void setCurrentMediaTime(long long int newTime);
      long long int getCurrentMediaTime();
      void setSmpteTimeCodeOffset(long long int newTime);
      long long int getSmpteTimeCodeOffset();
      void setMovieIdentifier(std::string & newIdentifier);
      char * getMovieIdentifier();
      long getServerEntryCount();
      void setServerEntry(std::string & entry, long no);
      const char * getServerEntry(long no);
      long getQualityEntryCount();
      void setQualityEntry(std::string & entry, long no);
      const char * getQualityEntry(long no);
      void setDrmData(std::string newDrm);
      char * getDrmData();
      void setMetaData(std::string newMetaData);
      char * getMetaData();
      long getSegmentRunTableCount();
      void setSegmentRunTable(ASRT table, long no);
      ASRT & getSegmentRunTable(long no);
      long getFragmentRunTableCount();
      void setFragmentRunTable(AFRT table, long no);
      AFRT & getFragmentRunTable(long no);
      std::string toPrettyString(long indent = 0);
  };//ABST Box
  
  class MFHD : public Box {
    public:
      MFHD();
      void setSequenceNumber( long newSequenceNumber );
      long getSequenceNumber();
      std::string toPrettyString(int indent = 0);
  };//MFHD Box
  
  class MOOF : public Box {
    public:
      MOOF();
      long getContentCount();
      void setContent( Box newContent, long no );
      Box getContent( long no );
      std::string toPrettyString(int indent = 0);
  };//MOOF Box
  
  class TRAF : public Box {
    public:
      TRAF();
      long getContentCount();
      void setContent( Box newContent, long no );
      Box getContent( long no );
      std::string toPrettyString(int indent = 0);
  };//TRAF Box
  
  struct trunSampleInformation {
    long sampleDuration;
    long sampleSize;
    long sampleFlags;
    long sampleOffset;
  };
  enum trunflags {
    trundataOffset       = 0x000001,
    trunfirstSampleFlags = 0x000004,
    trunsampleDuration   = 0x000100,
    trunsampleSize       = 0x000200,
    trunsampleFlags      = 0x000400,
    trunsampleOffsets    = 0x000800
  };
  enum sampleflags {
    noIPicture = 0x1000000,
    isIPicture = 0x2000000,
    noDisposable = 0x400000,
    isDisposable = 0x800000,
    isRedundant = 0x100000,
    noRedundant = 0x200000,
    noKeySample = 0x10000,
    iskeySample = 0x0,
    MUST_BE_PRESENT = 0x1
  };
  std::string prettySampleFlags(long flag);
  class TRUN : public Box {
    public:
      TRUN();
      void setFlags(long newFlags);
      long getFlags();
      void setDataOffset(long newOffset);
      long getDataOffset();
      void setFirstSampleFlags(long newSampleFlags);
      long getFirstSampleFlags();
      long getSampleInformationCount();
      void setSampleInformation(trunSampleInformation newSample, long no);
      trunSampleInformation getSampleInformation(long no);
      std::string toPrettyString(long indent = 0);
  };

  enum tfhdflags {
    tfhdBaseOffset = 0x000001,
    tfhdSampleDesc = 0x000002,
    tfhdSampleDura = 0x000008,
    tfhdSampleSize = 0x000010,
    tfhdSampleFlag = 0x000020,
    tfhdNoDuration = 0x010000,
  };
  class TFHD : public Box {
    public:
      TFHD();
      void setFlags(long newFlags);
      long getFlags();
      void setTrackID(long newID);
      long getTrackID();
      void setBaseDataOffset(long long newOffset);
      long long getBaseDataOffset();
      void setSampleDescriptionIndex(long newIndex);
      long getSampleDescriptionIndex();
      void setDefaultSampleDuration(long newDuration);
      long getDefaultSampleDuration();
      void setDefaultSampleSize(long newSize);
      long getDefaultSampleSize();
      void setDefaultSampleFlags(long newFlags);
      long getDefaultSampleFlags();
      std::string toPrettyString(long indent = 0);
  };

  struct afraentry {
    long long time;
    long long offset;
  };
  struct globalafraentry {
    long long time;
    long segment;
    long fragment;
    long long afraoffset;
    long long offsetfromafra;
  };
  class AFRA : public Box {
  public:
    AFRA();
    void setVersion(long newVersion);
    long getVersion();
    void setFlags(long newFlags);
    long getFlags();
    void setLongIDs(bool newVal);
    bool getLongIDs();
    void setLongOffsets(bool newVal);
    bool getLongOffsets();
    void setGlobalEntries(bool newVal);
    bool getGlobalEntries();
    void setTimeScale(long newVal);
    long getTimeScale();
    long getEntryCount();
    void setEntry(afraentry newEntry, long no);
    afraentry getEntry(long no);
    long getGlobalEntryCount();
    void setGlobalEntry(globalafraentry newEntry, long no);
    globalafraentry getGlobalEntry(long no);
    std::string toPrettyString(long indent = 0);
  };
  
};
