#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4{

  Box::Box(size_t size) {
    isUpdated = false;
    data.resize( size + 8 );
  }

  Box::Box( const char* BoxType, size_t size ) {
    isUpdated = false;
    data.resize( size + 8 );
    memcpy( (char*)data.c_str() + 4, BoxType, 4 );
  }

  Box::Box( std::string & newData ) {
    isUpdated = false;
    if( !read( newData ) ) {
      clear();
    }
  }

  Box::~Box() { }

  std::string Box::getType() {
    return data.substr(4,4);
  }

  bool Box::isType( char* boxType ) {
    return !memcmp( boxType, data.c_str() + 4, 4 );
  }

  bool Box::read(std::string & newData) {
    if( newData.size() > 4 ) {
      size_t size = ntohl( ((int*)newData.c_str())[0] );
      if( newData.size() > size + 8 ) {
        data = newData.substr( 0, size + 8 );
        newData.erase( 0, size + 8 );
        return true;
      }
    }
    return false;
  }

  size_t Box::boxedSize() {
    return data.size();
  }

  size_t Box::payloadSize() {
    return data.size() - 8;
  }

  std::string & Box::asBox() {
    if( isUpdated ) {
      regenerate( );
    }
    ((int*)data.c_str())[0] = htonl( data.size() );
    return data;
  }

  void Box::clear() {
    data.resize( 8 );
  }

  std::string Box::toPrettyString(int indent){
    return std::string(indent, ' ')+"Unimplemented pretty-printing for this box";
  }

  void Box::setInt8( char newData, size_t index ) {
    index += 8;
    if( index > data.size() ) {
      data.resize( index );
    }
    data[index] = newData;
  }
  
  char Box::getInt8( size_t index ) {
    index += 8;
    if( index > data.size() ) {
      data.resize( index );
    }
    return data[index];
  }

  void Box::setInt16( short newData, size_t index ) {
    index += 8;
    if( index + 1 > data.size() ) {
      data.resize( index + 1 );
    }
    newData = htons( newData );
    memcpy( (char*)data.c_str() + index, (char*)newData, 2 );
  }
  
  short Box::getInt16( size_t index ) {
    index += 8;
    if( index + 1 > data.size() ) {
      data.resize( index + 1 );
    }
    short result;
    memcpy( (char*)result, (char*)data.c_str() + index, 2 );
    return ntohs(result);
  }
  
  void Box::setInt24( long newData, size_t index ) {
    index += 8;
    if( index + 2 > data.size() ) {
      data.resize( index + 2 );
    }
    data[index] = (newData & 0x00FF0000) >> 16;
    data[index+1] = (newData & 0x0000FF00) >> 8;
    data[index+2] = (newData & 0x000000FF);
  }
  
  long Box::getInt24( size_t index ) {
    index += 8;
    if( index + 2 > data.size() ) {
      data.resize( index + 2 );
    }
    long result = data[index];
    result <<= 8;
    result += data[index+1];
    result <<= 8;
    result += data[index+2];
    return result;
  }

  void Box::setInt32( long newData, size_t index ) {
    index += 8;
    if( index + 3 > data.size() ) {
      data.resize( index + 3 );
    }
    newData = htonl( newData );
    memcpy( (char*)data.c_str() + index, (char*)newData, 4 );
  }
  
  long Box::getInt32( size_t index ) {
    index += 8;
    if( index + 3 > data.size() ) {
      data.resize( index + 3 );
    }
    long result;
    
    memcpy( (char*)result, (char*)data.c_str() + index, 4 );
    return ntohl(result);
  }

  void Box::setInt64( long long int newData, size_t index ) {
    index += 8;
    if( index + 7 > data.size() ) {
      data.resize( index + 7 );
    }
    data[index] = ( newData * 0xFF00000000000000 ) >> 56;
    data[index+1] = ( newData * 0x00FF000000000000 ) >> 48;
    data[index+2] = ( newData * 0x0000FF0000000000 ) >> 40;
    data[index+3] = ( newData * 0x000000FF00000000 ) >> 32;
    data[index+4] = ( newData * 0x00000000FF000000 ) >> 24;
    data[index+5] = ( newData * 0x0000000000FF0000 ) >> 16;
    data[index+6] = ( newData * 0x000000000000FF00 ) >> 8;
    data[index+7] = ( newData * 0x00000000000000FF );
  }
  
  long long int Box::getInt64( size_t index ) {
    index += 8;
    if( index + 7 > data.size() ) {
      data.resize( index + 7 );
    }
    long result = data[index];
    result <<= 8;
    result += data[index+1];
    result <<= 8;
    result += data[index+2];
    result <<= 8;
    result += data[index+3];
    result <<= 8;
    result += data[index+4];
    result <<= 8;
    result += data[index+5];
    result <<= 8;
    result += data[index+6];
    result <<= 8;
    result += data[index+7];
    return result;
  }


  void Box::setString(std::string newData, size_t index ) {
    setString( (char*)newData.c_str(), newData.size(), index );
  }

  void Box::setString(char* newData, size_t size, size_t index ) {
    index += 8;
    if( index + size > data.size() ) {
      data.resize( index + size );
    }
    memcpy( (char*)data.c_str() + index, newData, size );
  }

  void Box::regenerate() {
    std::cerr << "Regenerate() not implemented for this box type\n";
  }
  
  ABST::ABST( ) : Box("abst") {
    setVersion( 0 );
    setFlags( 0 );
    setBootstrapinfoVersion( 0 );
    setProfile( 0 );
    setLive( 1 );
    setUpdate( 0 );
    setTimeScale( 1000 );
    setCurrentMediaTime( 0 );
    setSmpteTimeCodeOffset( 0 );
    setMovieIdentifier( "" );
    setDrmData( "" );
    setMetaData( "" );  
  }
  
  void ABST::setVersion( char newVersion ) {
    setInt8( newVersion, 0 );
  }
  
  void ABST::setFlags( long newFlags ) {
    setInt24( newFlags, 1 );
  }

  void ABST::setBootstrapinfoVersion( long newVersion ) {
    setInt32( newVersion, 4 );
  }

  void ABST::setProfile( char newProfile ) {
    setInt8( ( getInt8(5) & 0x3F ) + ( ( newProfile & 0x02 ) << 6 ), 5 );
  }


  void ABST::setLive( char newLive ) {
    setInt8( ( getInt8(5) & 0xDF ) + ( ( newLive & 0x01 ) << 5 ), 5 );
  }


  void ABST::setUpdate( char newUpdate ) {
    setInt8( ( getInt8(5) & 0xEF ) + ( ( newUpdate & 0x01 ) << 4 ), 5 );
  }

  void ABST::setTimeScale( long newScale ) {
    setInt32( newScale, 6 );
  }
  
  void ABST::setCurrentMediaTime( long long int newTime ) {
    setInt64( newTime, 10 );
  }

  void ABST::setSmpteTimeCodeOffset( long long int newTime ) {
    setInt64( newTime, 18 );
  }

  void ABST::setMovieIdentifier( std::string newIdentifier ) {
    movieIdentifier = newIdentifier;
  }

  void ABST::addServerEntry( std::string newEntry ) {
    if( std::find( Servers.begin(), Servers.end(), newEntry ) == Servers.end() ) {
      Servers.push_back( newEntry );
      isUpdated = true;
    }
  }
  
  void ABST::delServerEntry( std::string delEntry ) {
    for( std::deque<std::string>::iterator it = Servers.begin(); it != Servers.end(); it++ ) {
      if( (*it) == delEntry ) {
        Servers.erase( it );
        isUpdated = true;
        break;
      }
    }
  }
  
  void ABST::addQualityEntry( std::string newEntry ) {
    if( std::find( Qualities.begin(), Qualities.end(), newEntry ) == Qualities.end() ) {
      Servers.push_back( newEntry );
      isUpdated = true;
    }
  }
  
  void ABST::delQualityEntry( std::string delEntry ) {
    for( std::deque<std::string>::iterator it = Qualities.begin(); it != Qualities.end(); it++ ) {
      if( (*it) == delEntry ) {
        Qualities.erase( it );
        isUpdated = true;
        break;
      }
    }
  }

  void ABST::setDrmData( std::string newDrm ) {
    drmData = newDrm;
    isUpdated = true;
  }

  void ABST::setMetaData( std::string newMetaData ) {
    metaData = newMetaData;
    isUpdated = true;
  }

  void ABST::addSegmentRunTable( Box * newSegment ) {
    segmentTables.push_back(newSegment);
    isUpdated = true;
  }

  void ABST::addFragmentRunTable( Box * newFragment ) {
    fragmentTables.push_back(newFragment);
    isUpdated = true;
  }
  
  void ABST::regenerate( ) {
    int myOffset = 26;
    //0-terminated movieIdentifier
    memcpy( (char*)data.c_str() + myOffset, movieIdentifier.c_str(), movieIdentifier.size() + 1);
    myOffset += movieIdentifier.size() + 1;
    //8-bit server amount
    setInt8( Servers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = Servers.begin(); it != Servers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    //8-bit quality amount
    setInt8( Qualities.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = Qualities.begin();it != Qualities.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    //0-terminated DrmData
    memcpy( (char*)data.c_str() + myOffset, drmData.c_str(), drmData.size() + 1);
    myOffset += drmData.size() + 1;
    //0-terminated MetaData
    memcpy( (char*)data.c_str() + myOffset, metaData.c_str(), metaData.size() + 1);
    myOffset += metaData.size() + 1;
    //8-bit segment run amount
    setInt8( segmentTables.size(), myOffset );
    myOffset ++;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = segmentTables.begin(); it != segmentTables.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() );
      myOffset += (*it)->boxedSize();
    }
    //8-bit fragment run amount
    setInt8( fragmentTables.size(), myOffset );
    myOffset ++;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = fragmentTables.begin(); it != fragmentTables.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() + 1);
      myOffset += (*it)->boxedSize();
    }
    isUpdated = false;
  }

  std::string ABST::toPrettyString( int indent ) {
    std::string r;
    r += std::string(indent, ' ')+"Bootstrap Info\n";
    if( getInt8(5) & 0x10 ) {
      r += std::string(indent+1, ' ')+"Update\n";
    } else {
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    if( getInt8(5) & 0x20 ) {
      r += std::string(indent+1, ' ' )+"Live\n";
    }else{
      r += std::string(indent+1, ' ' )+"Recorded\n";
    }
    r += std::string(indent+1, ' ')+"Profile "+JSON::Value((long long int)( getInt8(5) & 0xC0 ) ).asString()+"\n";
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((long long int)getInt64(10)).asString()+"\n";
    r += std::string(indent+1, ' ')+"CurrMediaTime "+JSON::Value((long long int)getInt32(6)).asString()+"\n";
    r += std::string(indent+1, ' ')+"Segment Run Tables "+JSON::Value((long long int)segmentTables.size()).asString()+"\n";
    for( uint32_t i = 0; i < segmentTables.size(); i++ ) {
      r += ((ASRT*)segmentTables[i])->toPrettyString(indent+2)+"\n";
    }
    r += std::string(indent+1, ' ')+"Fragment Run Tables "+JSON::Value((long long int)fragmentTables.size()).asString()+"\n";
    for( uint32_t i = 0; i < fragmentTables.size(); i++ ) {
      r += ((AFRT*)fragmentTables[i])->toPrettyString(indent+2)+"\n";
    }
    return r;
  }
  
  AFRT::AFRT() : Box("afrt"){
    setVersion( 0 );
    setUpdate( 0 );
    setTimeScale( 1000 );
  }
  
  void AFRT::setVersion( char newVersion ) {
    setInt8( newVersion ,0 );
  }
  
  void AFRT::setUpdate( long newUpdate ) {
    setInt24( newUpdate, 1 );
  }
  
  void AFRT::setTimeScale( long newScale ) {
    setInt32( newScale, 4 );
  }
  
  void AFRT::addQualityEntry( std::string newQuality ) {
    qualityModifiers.push_back( newQuality );
    isUpdated = true;
  }
  
  void AFRT::addFragmentRun( long firstFragment, long long int firstTimestamp, long duration, char discontinuity ) {
    fragmentRun newRun;
    newRun.firstFragment = firstFragment;
    newRun.firstTimestamp = firstTimestamp;
    newRun.duration = duration;
    newRun.discontinuity = discontinuity;
    fragmentRunTable.push_back( newRun );
    isUpdated = true;
  }
  
  void AFRT::regenerate( ) {
    int myOffset = 8;
    setInt8( qualityModifiers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = qualityModifiers.begin();it != qualityModifiers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    setInt32( fragmentRunTable.size(), myOffset );
    myOffset += 4;
    //table values for each entry
    for( std::deque<fragmentRun>::iterator it = fragmentRunTable.begin();it != fragmentRunTable.end(); it++ ) {
      setInt32( (*it).firstFragment, myOffset );
      myOffset += 4;
      setInt64( (*it).firstTimestamp, myOffset );
      myOffset += 8;
      setInt32( (*it).duration, myOffset );
      myOffset += 4;
      setInt8( (*it).discontinuity, myOffset );
      myOffset += 1;
    }
    isUpdated = false;
  }

  std::string AFRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Fragment Run Table\n";
    if (getInt24(1)){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((long long int)getInt32(4)).asString()+"\n";
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)qualityModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < qualityModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+qualityModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Fragments "+JSON::Value((long long int)fragmentRunTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < fragmentRunTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+"Duration "+JSON::Value((long long int)fragmentRunTable[i].duration).asString()+", starting at "+JSON::Value((long long int)fragmentRunTable[i].firstFragment).asString()+" @ "+JSON::Value((long long int)fragmentRunTable[i].firstTimestamp).asString();
    }
    return r;
  }
  
  ASRT::ASRT() : Box("asrt") {
    setVersion( 0 );
    setUpdate( 0 );
  }
  
  void ASRT::setVersion( char newVersion ) {
    setInt8( newVersion, 0 );
  }
  
  void ASRT::setUpdate( long newUpdate ) {
    setInt24( newUpdate, 1 );
  }
  
  void ASRT::addQualityEntry( std::string newQuality ) {
    qualityModifiers.push_back( newQuality );
    isUpdated = true;
  }
  
  void ASRT::addSegmentRun( long firstSegment, long fragmentsPerSegment ) {
    segmentRun newRun;
    newRun.firstSegment = firstSegment;
    newRun.fragmentsPerSegment = fragmentsPerSegment;
    segmentRunTable.push_back( newRun );
    isUpdated = true;
  }
  
  void ASRT::regenerate( ) {
    int myOffset = 4;
    setInt8( qualityModifiers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = qualityModifiers.begin();it != qualityModifiers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    setInt32( segmentRunTable.size(), myOffset );
    myOffset += 4;
    //table values for each entry
    for( std::deque<segmentRun>::iterator it = segmentRunTable.begin();it != segmentRunTable.end(); it++ ) {
      setInt32( (*it).firstSegment, myOffset );
      myOffset += 4;
      setInt32( (*it).fragmentsPerSegment, myOffset );
      myOffset += 4;
    }
    isUpdated = false;
  }

  std::string ASRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Segment Run Table\n";
    if (getInt24(1)){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)qualityModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < qualityModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+qualityModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Segments "+JSON::Value((long long int)segmentRunTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < segmentRunTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+JSON::Value((long long int)segmentRunTable[i].fragmentsPerSegment).asString()+" fragments per, starting at "+JSON::Value((long long int)segmentRunTable[i].firstSegment).asString();
    }
    return r;
  }
  
  MFHD::MFHD() : Box("mfhd") {
    setInt32(0,0);
  }
  
  void MFHD::setSequenceNumber( long newSequenceNumber ) {
    setInt32( newSequenceNumber, 4 );
  }
  
  MOOF::MOOF() : Box("moof") {}
  
  void MOOF::addContent( Box* newContent ) {
    content.push_back( newContent );
    isUpdated = true;
  }
  
  void MOOF::regenerate() {
    int myOffset = 0;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = content.begin(); it != content.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() + 1);
      myOffset += (*it)->boxedSize();
    }
    isUpdated = false;
  }
};
