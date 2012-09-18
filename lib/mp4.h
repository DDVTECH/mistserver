#pragma once
#include <string>
#include <iostream>
#include <stdint.h>
#include <deque>
#include <vector> ///\todo remove this include
#include <algorithm>
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4{

  class Box {
    public:
      Box( size_t size = 0);
      Box( const char* boxType, size_t size = 0 );
      Box( std::string & newData );
      ~Box();
      std::string getType();
      bool isType( char* boxType );
      bool read( std::string & newData );
      size_t boxedSize();
      size_t payloadSize();
      std::string & asBox();
      void regenerate();
      void clear();
      std::string toPrettyString( int indent = 0 );
    protected:
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
      void setString(std::string newData, size_t index );
      void setString(char* newData, size_t size, size_t index );
      std::string data;
      bool isUpdated;
  };//Box Class

  /// ABST Box class
  class ABST: public Box {
    public:
      ABST();
      void setVersion( char newVersion );
      void setFlags( long newFlags );
      void setBootstrapinfoVersion( long newVersion );
      void setProfile( char newProfile );
      void setLive( char newLive );
      void setUpdate( char newUpdate );
      void setTimeScale( long newTimeScale );
      void setCurrentMediaTime( long long int newTime );
      void setSmpteTimeCodeOffset( long long int newTime );
      void setMovieIdentifier( std::string newIdentifier );
      void addServerEntry( std::string newEntry );
      void delServerEntry( std::string delEntry );
      void addQualityEntry( std::string newEntry );
      void delQualityEntry( std::string delEntry );
      void setDrmData( std::string newDrm );
      void setMetaData( std::string newMetaData );
      void addSegmentRunTable( Box * newSegment );
      void addFragmentRunTable( Box * newFragment );
      std::string toPrettyString(int indent = 0);
    private:
      void regenerate();
      std::string movieIdentifier;
      std::deque<std::string> Servers;
      std::deque<std::string> Qualities;
      std::string drmData;
      std::string metaData;
      std::deque<Box *> segmentTables;
      std::deque<Box *> fragmentTables;
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
  };//MFHD Box
  
  class MOOF : public Box {
    public:
      MOOF();
      void addContent( Box* newContent );
      void regenerate( );
    private:
      std::deque<Box*> content;
  };//MOOF Box
};
