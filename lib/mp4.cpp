#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

/// Contains all MP4 format related code.
namespace MP4{

  Box::Box(size_t size) {
    data.resize( size + 8 );
    memset( (char*)data.c_str(), 0, size + 8 );
  }

  Box::Box( char* BoxType, size_t size ) {
    data.resize( size + 8 );
    memset( (char*)data.c_str(), 0, size + 8 );
    memcpy( (char*)data.c_str() + 4, BoxType, 4 );
  }

  Box::Box( std::string & newData ) {
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

  void Box::setInt16( short newData, size_t index ) {
    index += 8;
    if( index + 1 > data.size() ) {
      data.resize( index + 1 );
    }
    newData = htons( newData );
    memcpy( (char*)data.c_str() + index, (char*)newData, 2 );
  }

  void Box::setInt32( long newData, size_t index ) {
    index += 8;
    if( index + 3 > data.size() ) {
      data.resize( index + 3 );
    }
    newData = htonl( newData );
    memcpy( (char*)data.c_str() + index, (char*)newData, 4 );
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


/*
  void ABST::SetBootstrapVersion( uint32_t Version ) {
    curBootstrapInfoVersion = Version;
  }

  void ABST::SetProfile( uint8_t Profile ) {
    curProfile = Profile;
  }

  void ABST::SetLive( bool Live ) {
    isLive = Live;
  }

  void ABST::SetUpdate( bool Update ) {
    isUpdate = Update;
  }

  void ABST::SetTimeScale( uint32_t Scale ) {
    curTimeScale = Scale;
  }

  void ABST::SetMediaTime( uint32_t Time ) {
    curMediatime = Time;
  }

  void ABST::SetSMPTE( uint32_t Smpte ) {
    curSMPTE = Smpte;
  }

  void ABST::SetMovieIdentifier( std::string Identifier ) {
    curMovieIdentifier = Identifier;
  }

  void ABST::SetDRM( std::string Drm ) {
    curDRM = Drm;
  }

  void ABST::SetMetaData( std::string MetaData ) {
    curMetaData = MetaData;
  }

  void ABST::AddServerEntry( std::string Url, uint32_t Offset ) {
    if(Offset >= Servers.size()) {
      Servers.resize(Offset+1);
    }
    Servers[Offset].ServerBaseUrl = Url;
  }

  void ABST::AddQualityEntry( std::string Quality, uint32_t Offset ) {
    if(Offset >= Qualities.size()) {
      Qualities.resize(Offset+1);
    }
    Qualities[Offset].QualityModifier = Quality;
  }

  void ABST::AddSegmentRunTable( Box * newSegment, uint32_t Offset ) {
    if( Offset >= SegmentRunTables.size() ) {
      SegmentRunTables.resize(Offset+1);
    }
    if( SegmentRunTables[Offset] ) {
      delete SegmentRunTables[Offset];
    }
    SegmentRunTables[Offset] = newSegment;
  }

  void ABST::AddFragmentRunTable( Box * newFragment, uint32_t Offset ) {
    if( Offset >= FragmentRunTables.size() ) {
      FragmentRunTables.resize(Offset+1);
    }
    if( FragmentRunTables[Offset] ) {
      delete FragmentRunTables[Offset];
    }
    FragmentRunTables[Offset] = newFragment;
  }

  void ABST::SetDefaults( ) {
    SetProfile( );
    SetLive( );
    SetUpdate( );
    SetTimeScale( );
    SetMediaTime( );
    SetSMPTE( );
    SetMovieIdentifier( );
    SetDRM( );
    SetMetaData( );
    SetVersion( );
  }

  void ABST::SetVersion( bool NewVersion) {
    Version = NewVersion;
  }

  void ABST::SetReserved( ) {
    SetInt32(0);
  }

  void ABST::WriteContent( ) {
    Box * current;
    std::string serializedServers = "";
    std::string serializedQualities = "";
    std::string serializedSegments = "";
    std::string serializedFragments = "";
    int SegmentAmount = 0;
    int FragmentAmount = 0;
    uint8_t * temp = new uint8_t[1];
    
    clear( );
    SetReserved( );
    
    for( uint32_t i = 0; i < Servers.size(); i++ ) {
      serializedServers.append(Servers[i].ServerBaseUrl.c_str());
      serializedServers += '\0';
    }
    for( uint32_t i = 0; i < Qualities.size(); i++ ) {
      serializedQualities.append(Qualities[i].QualityModifier.c_str());
      serializedQualities += '\0';
    }
    for( uint32_t i = 0; i < SegmentRunTables.size(); i++ ) {
      current=SegmentRunTables[i];
      if( current ) {
        SegmentAmount ++;
        serializedSegments.append((char*)current->GetBoxedData(),current->GetBoxedDataSize());
      }
    }
    for( uint32_t i = 0; i < FragmentRunTables.size(); i++ ) {
      current=FragmentRunTables[i];
      if( current ) {
        FragmentAmount ++;
        serializedFragments.append((char*)current->GetBoxedData(),current->GetBoxedDataSize());
      }
    }
    uint32_t OffsetServerEntryCount = 29 + curMovieIdentifier.size() + 1;
    uint32_t OffsetQualityEntryCount = OffsetServerEntryCount + 1 + serializedServers.size();
    uint32_t OffsetDrmData = OffsetQualityEntryCount + 1 + serializedQualities.size();
    uint32_t OffsetMetaData = OffsetDrmData + curDRM.size() + 1;
    uint32_t OffsetSegmentRuntableCount = OffsetMetaData + curMetaData.size() + 1;
    uint32_t OffsetFragmentRuntableCount = OffsetSegmentRuntableCount + 1 + serializedSegments.size();
    
    temp[0] = 0 + ( curProfile << 6 ) + ( (uint8_t)isLive << 7 ) + ( (uint8_t)isUpdate << 7 );
    
    SetPayload((uint32_t)serializedFragments.size(),(uint8_t*)serializedFragments.c_str(),OffsetFragmentRuntableCount+1);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(FragmentAmount),OffsetFragmentRuntableCount);
    SetPayload((uint32_t)serializedSegments.size(),(uint8_t*)serializedSegments.c_str(),OffsetSegmentRuntableCount+1);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(SegmentAmount),OffsetSegmentRuntableCount);
    SetPayload((uint32_t)curMetaData.size()+1,(uint8_t*)curMetaData.c_str(),OffsetMetaData);
    SetPayload((uint32_t)curDRM.size()+1,(uint8_t*)curDRM.c_str(),OffsetDrmData);
    SetPayload((uint32_t)serializedQualities.size(),(uint8_t*)serializedQualities.c_str(),OffsetQualityEntryCount+1);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(Qualities.size()),OffsetQualityEntryCount);
    SetPayload((uint32_t)serializedServers.size(),(uint8_t*)serializedServers.c_str(),OffsetServerEntryCount+1);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(Servers.size()),OffsetServerEntryCount);
    SetPayload((uint32_t)curMovieIdentifier.size()+1,(uint8_t*)curMovieIdentifier.c_str(),29);//+1 for \0-terminated string...
    SetPayload((uint32_t)4,Box::uint32_to_uint8(curSMPTE),25);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(0),21);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(curMediatime),17);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(0),13);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(curTimeScale),9);
    SetPayload((uint32_t)1,temp,8);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(curBootstrapInfoVersion),4);
  }

  std::string ABST::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Bootstrap Info\n";
    if (isUpdate){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    if (isLive){
      r += std::string(indent+1, ' ')+"Live\n";
    }else{
      r += std::string(indent+1, ' ')+"Recorded\n";
    }
    r += std::string(indent+1, ' ')+"Profile "+JSON::Value((long long int)curProfile).asString()+"\n";
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((long long int)curTimeScale).asString()+"\n";
    r += std::string(indent+1, ' ')+"CurrMediaTime "+JSON::Value((long long int)curMediatime).asString()+"\n";
    r += std::string(indent+1, ' ')+"Segment Run Tables "+JSON::Value((long long int)SegmentRunTables.size()).asString()+"\n";
    for( uint32_t i = 0; i < SegmentRunTables.size(); i++ ) {
      r += ((ASRT*)SegmentRunTables[i])->toPrettyString(indent+2)+"\n";
    }
    r += std::string(indent+1, ' ')+"Fragment Run Tables "+JSON::Value((long long int)FragmentRunTables.size()).asString()+"\n";
    for( uint32_t i = 0; i < FragmentRunTables.size(); i++ ) {
      r += ((AFRT*)FragmentRunTables[i])->toPrettyString(indent+2)+"\n";
    }
    return r;
  }

  void AFRT::SetUpdate( bool Update ) {
    isUpdate = Update;
  }

  void AFRT::AddQualityEntry( std::string Quality, uint32_t Offset ) {
    if(Offset >= QualitySegmentUrlModifiers.size()) {
      QualitySegmentUrlModifiers.resize(Offset+1);
    }
    QualitySegmentUrlModifiers[Offset] = Quality;
  }

  void AFRT::AddFragmentRunEntry( uint32_t FirstFragment, uint32_t FirstFragmentTimestamp, uint32_t FragmentsDuration, uint8_t Discontinuity, uint32_t Offset ) {
    if( Offset >= FragmentRunEntryTable.size() ) {
      FragmentRunEntryTable.resize(Offset+1);
    }
    FragmentRunEntryTable[Offset].FirstFragment = FirstFragment;
    FragmentRunEntryTable[Offset].FirstFragmentTimestamp = FirstFragmentTimestamp;
    FragmentRunEntryTable[Offset].FragmentDuration = FragmentsDuration;
    if( FragmentsDuration == 0) {
      FragmentRunEntryTable[Offset].DiscontinuityIndicator = Discontinuity;
    }
  }

  void AFRT::SetDefaults( ) {
    SetUpdate( );
    SetTimeScale( );
  }

  void AFRT::SetTimeScale( uint32_t Scale ) {
    curTimeScale = Scale;
  }

  void AFRT::WriteContent( ) {
    std::string serializedQualities = "";
    std::string serializedFragmentEntries = "";
    clear();
    
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
      serializedQualities.append(QualitySegmentUrlModifiers[i].c_str());
      serializedQualities += '\0';
    }
    for( uint32_t i = 0; i < FragmentRunEntryTable.size(); i ++ ) {
      serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FirstFragment),4);
      serializedFragmentEntries.append((char*)Box::uint32_to_uint8(0),4);
      serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FirstFragmentTimestamp),4);
      serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FragmentDuration),4);
      if(FragmentRunEntryTable[i].FragmentDuration == 0) {
        serializedFragmentEntries.append((char*)Box::uint8_to_uint8(FragmentRunEntryTable[i].DiscontinuityIndicator),1);
      }
    }
    
    uint32_t OffsetFragmentRunEntryCount = 9 + serializedQualities.size();
    
    SetPayload((uint32_t)serializedFragmentEntries.size(),(uint8_t*)serializedFragmentEntries.c_str(),OffsetFragmentRunEntryCount+4);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(FragmentRunEntryTable.size()),OffsetFragmentRunEntryCount);
    SetPayload((uint32_t)serializedQualities.size(),(uint8_t*)serializedQualities.c_str(),9);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(QualitySegmentUrlModifiers.size()),8);
    SetPayload((uint32_t)4,Box::uint32_to_uint8(curTimeScale),4);
    SetPayload((uint32_t)4,Box::uint32_to_uint8((isUpdate ? 1 : 0)));
  }

  std::string AFRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Fragment Run Table\n";
    if (isUpdate){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((long long int)curTimeScale).asString()+"\n";
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)QualitySegmentUrlModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+QualitySegmentUrlModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Fragments "+JSON::Value((long long int)FragmentRunEntryTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < FragmentRunEntryTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+"Duration "+JSON::Value((long long int)FragmentRunEntryTable[i].FragmentDuration).asString()+", starting at "+JSON::Value((long long int)FragmentRunEntryTable[i].FirstFragment).asString()+" @ "+JSON::Value((long long int)FragmentRunEntryTable[i].FirstFragmentTimestamp).asString();
    }
    return r;
  }

  void ASRT::SetUpdate( bool Update ) {
    isUpdate = Update;
  }

  void ASRT::AddQualityEntry( std::string Quality, uint32_t Offset ) {
    if(Offset >= QualitySegmentUrlModifiers.size()) {
      QualitySegmentUrlModifiers.resize(Offset+1);
    }
    QualitySegmentUrlModifiers[Offset] = Quality;
  }

  void ASRT::AddSegmentRunEntry( uint32_t FirstSegment, uint32_t FragmentsPerSegment, uint32_t Offset ) {
    if( Offset >= SegmentRunEntryTable.size() ) {
      SegmentRunEntryTable.resize(Offset+1);
    }
    SegmentRunEntryTable[Offset].FirstSegment = FirstSegment;
    SegmentRunEntryTable[Offset].FragmentsPerSegment = FragmentsPerSegment;
  }

  void ASRT::SetVersion( bool NewVersion ) {
    Version = NewVersion;
  }

  void ASRT::SetDefaults( ) {
    SetUpdate( );
  }

  void ASRT::WriteContent( ) {
    std::string serializedQualities = "";
    ResetPayload( );
    
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
      serializedQualities.append(QualitySegmentUrlModifiers[i].c_str());
      serializedQualities += '\0';
    }
    
    uint32_t OffsetSegmentRunEntryCount = 5 + serializedQualities.size();
    
    for( uint32_t i = 0; i < SegmentRunEntryTable.size(); i ++ ) {
      SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable[i].FragmentsPerSegment),(8*i)+OffsetSegmentRunEntryCount+8);
      SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable[i].FirstSegment),(8*i)+OffsetSegmentRunEntryCount+4);
    }
    SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable.size()),OffsetSegmentRunEntryCount);
    SetPayload((uint32_t)serializedQualities.size(),(uint8_t*)serializedQualities.c_str(),5);
    SetPayload((uint32_t)1,Box::uint8_to_uint8(QualitySegmentUrlModifiers.size()),4);
    SetPayload((uint32_t)4,Box::uint32_to_uint8((isUpdate ? 1 : 0)));
  }

  std::string ASRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Segment Run Table\n";
    if (isUpdate){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)QualitySegmentUrlModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+QualitySegmentUrlModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Segments "+JSON::Value((long long int)SegmentRunEntryTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < SegmentRunEntryTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+JSON::Value((long long int)SegmentRunEntryTable[i].FragmentsPerSegment).asString()+" fragments per, starting at "+JSON::Value((long long int)SegmentRunEntryTable[i].FirstSegment).asString();
    }
    return r;
  }

  std::string mdatFold(std::string data){
    std::string Result;
    unsigned int t_int;
    t_int = htonl(data.size()+8);
    Result.append((char*)&t_int, 4);
    t_int = htonl(0x6D646174);
    Result.append((char*)&t_int, 4);
    Result.append(data);
    return Result;
  }
*/

};
