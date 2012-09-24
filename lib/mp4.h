#pragma once
#include <string>
#include <iostream>
#include <stdint.h>
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
      size_t boxedSize();
      size_t payloadSize();
      char * asBox();
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
      //data functions
      bool reserve(size_t position, size_t current, size_t wanted);
      //internal variables
      char * data; ///< Holds the data of this box
      int data_size; ///< Currently reserved size
      bool managed; ///< If false, will not attempt to resize/free the data pointer.
  };//Box Class

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
      void setLive(char newLive);
      char getLive();
      void setUpdate(char newUpdate);
      char getUpdate();
      void setTimeScale(long newTimeScale);
      long getTimeScale();
      void setCurrentMediaTime(long long int newTime);
      long long int getCurrentMediaTime();
      void setSmpteTimeCodeOffset(long long int newTime);
      long long int getSmpteTimeCodeOffset();
      void setMovieIdentifier(std::string & newIdentifier);
      char * getMovieIdentifier();
      void setServerEntry(std::string & entry, int no);
      char * getServerEntry(int no);
      int getServerEntryCount();
      void setQualityEntry(std::string & entry, int no);
      char * getQualityEntry(int no);
      int getQualityEntryCount();
      void setDrmData(std::string newDrm);
      char * getDrmData();
      void setMetaData(std::string newMetaData);
      char * getMetaData();
      void setSegmentRunTable(ASRT table, int no);
      ASRT & getSegmentRunTable(int no);
      int getSegmentRunTableCount();
      void setFragmentRunTables(AFRT table, int no);
      AFRT & getFragmentRunTable(int no);
      int getFragmentRunTableCount();
      std::string toPrettyString(int indent = 0);
  };//ABST Box

  struct fragmentRun {
    long firstFragment;
    long long int firstTimestamp;
    long duration;
    char discontinuity;
  };//fragmentRun


  /// AFRT Box class
  class AFRT : public Box {
    public:
      AFRT();
      void setVersion( char newVersion );
      void setUpdate( long newUpdate );
      void setTimeScale( long newScale );
      void addQualityEntry( std::string newQuality );
      void addFragmentRun( long firstFragment, long long int firstTimestamp, long duration, char discontinuity );
      void regenerate( );
      std::string toPrettyString(int indent = 0);
    private:
      std::deque<std::string> qualityModifiers;
      std::deque<fragmentRun> fragmentRunTable;
  };//AFRT Box

  struct segmentRun {
    uint32_t firstSegment;
    uint32_t fragmentsPerSegment;
  };//segmentRun

  /// ASRT Box class
  class ASRT : public Box {
    public:
      ASRT();
      void setVersion( char newVersion );
      void setUpdate( long newUpdate );
      void addQualityEntry( std::string newQuality );
      void addSegmentRun( long firstSegment, long fragmentsPerSegment );
      void regenerate();
      std::string toPrettyString(int indent = 0);
    private:
      std::deque<std::string> qualityModifiers;
      std::deque<segmentRun> segmentRunTable;
  };//ASRT Box
  
  class MFHD : public Box {
    public:
      MFHD();
      void setSequenceNumber( long newSequenceNumber );
      std::string toPrettyString(int indent = 0);
  };//MFHD Box
  
  class MOOF : public Box {
    public:
      MOOF();
      void addContent( Box* newContent );
      void regenerate( );
      std::string toPrettyString(int indent = 0);
    private:
      std::deque<Box*> content;
  };//MOOF Box
  
  struct trunSampleInformation {
    long sampleDuration;
    long sampleSize;
    long sampleFlags;
    long sampleCompositionTimeOffset;
  };
  
  class TRUN : public Box {
    public:
      TRUN();
      void setFlags( long newFlags );
      void setDataOffset( long newOffset );
      void setFirstSampleFlags( char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy, char sampleIsDifferenceSample );
      void addSampleInformation( long newDuration, long newSize, char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy,char sampleIsDifferenceSample, long newCompositionTimeOffset );
      void regenerate();
    private:
      long getSampleFlags( char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy, char sampleIsDifferenceSample );
      long dataOffset;
      long firstSampleFlags;
      std::deque<trunSampleInformation> allSamples;
  };
};
