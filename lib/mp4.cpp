#include "mp4.h"
#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends

/// Contains all MP4 format related code.
namespace MP4{

Box::Box() {
  Payload = (uint8_t *)malloc(8);
  PayloadSize = 0;
}

Box::Box(uint32_t BoxType) {
  Payload = (uint8_t *)malloc(8);
  SetBoxType(BoxType);
  PayloadSize = 0;
}

Box::Box(uint8_t * Content, uint32_t length) {
  PayloadSize = length-8;
  Payload = (uint8_t *)malloc(length);
  memcpy(Payload, Content, length);
}

Box::~Box() {
  if (Payload) free(Payload);
}

void Box::SetBoxType(uint32_t BoxType) {
  ((unsigned int*)Payload)[1] = htonl(BoxType);
}

uint32_t Box::GetBoxType() {
  return ntohl(((unsigned int*)Payload)[1]);
}

void Box::SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index) {
  if ( Index + Size > PayloadSize ) {
    PayloadSize = Index + Size;
    ((unsigned int*)Payload)[0] = htonl(PayloadSize+8);
    Payload = (uint8_t *)realloc(Payload, PayloadSize + 8);
  }
  memcpy(Payload + 8 + Index, Data, Size);
}

uint32_t Box::GetPayloadSize() {
  return PayloadSize;
}

uint8_t * Box::GetPayload() {
  return Payload+8;
}

uint8_t * Box::GetPayload(uint32_t Index, uint32_t & Size) {
  if(Index > PayloadSize) {Size = 0;}
  if(Index + Size > PayloadSize) { Size = PayloadSize - Index; }
  return Payload + 8 + Index;
}

uint32_t Box::GetBoxedDataSize() {
  return ntohl(((unsigned int*)Payload)[0]);
}

uint8_t * Box::GetBoxedData( ) {
  return Payload;
}


uint8_t * Box::uint32_to_uint8( uint32_t data ) {
  uint8_t * temp = new uint8_t[4];
  temp[0] = (data >> 24) & 0x000000FF;
  temp[1] = (data >> 16 ) & 0x000000FF;
  temp[2] = (data >> 8 ) & 0x000000FF;
  temp[3] = (data ) & 0x000000FF;
  return temp;
}

uint8_t * Box::uint16_to_uint8( uint16_t data ) {
  uint8_t * temp = new uint8_t[2];
  temp[0] = (data >> 8) & 0x00FF;
  temp[1] = (data  ) & 0x00FF;
  return temp;
}

uint8_t * Box::uint8_to_uint8( uint8_t data ) {
  uint8_t * temp = new uint8_t[1];
  temp[0] = data;
  return temp;
}

void Box::ResetPayload( ) {
  PayloadSize = 0;
  Payload = (uint8_t *)realloc(Payload, PayloadSize + 8);
  ((unsigned int*)Payload)[0] = htonl(0);
}

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
  SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
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
  
  ResetPayload( );
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
  ResetPayload( );
  
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

std::string GenerateLiveBootstrap( uint32_t CurMediaTime ) {
  AFRT afrt;
  afrt.SetUpdate(false);
  afrt.SetTimeScale(1000);
  afrt.AddQualityEntry("");
  afrt.AddFragmentRunEntry(1, 0 , 4000); //FirstFragment, FirstFragmentTimestamp,Fragment Duration in milliseconds
  afrt.WriteContent();

  ASRT asrt;
  asrt.SetUpdate(false);
  asrt.AddQualityEntry("");
  asrt.AddSegmentRunEntry(1, 199);//1 Segment, 199 Fragments
  asrt.WriteContent();

  ABST abst;
  abst.AddFragmentRunTable(&afrt);
  abst.AddSegmentRunTable(&asrt);
  abst.SetBootstrapVersion(1);
  abst.SetProfile(0);
  abst.SetLive(true);
  abst.SetUpdate(false);
  abst.SetTimeScale(1000);
  abst.SetMediaTime(0xFFFFFFFF);
  abst.SetSMPTE(0);
  abst.SetMovieIdentifier("fifa");
  abst.SetDRM("");
  abst.SetMetaData("");
  abst.AddServerEntry("");
  abst.AddQualityEntry("");
  abst.WriteContent();
  
  std::string Result;
  Result.append((char*)abst.GetBoxedData(), (int)abst.GetBoxedDataSize());
  return Result;
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

};
