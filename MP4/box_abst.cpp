#include "box_abst.h"

Box_abst::Box_abst( ) {
  Container = new Box( 0x61627374 );
}

Box_abst::~Box_abst() {
  delete Container;
}

Box * Box_abst::GetBox() {
  return Container;
}

void Box_abst::SetBootstrapVersion( uint32_t Version ) {
  curBootstrapInfoVersion = Version;
}

void Box_abst::SetProfile( uint8_t Profile ) {
  curProfile = Profile;
}

void Box_abst::SetLive( bool Live ) {
  isLive = Live;
}

void Box_abst::SetUpdate( bool Update ) {
  isUpdate = Update;
}

void Box_abst::SetTimeScale( uint32_t Scale ) {
  curTimeScale = Scale;
}

void Box_abst::SetMediaTime( uint32_t Time ) {
  curMediaTime = Time;
}

void Box_abst::SetSMPTE( uint32_t Smpte ) {
  curSMPTE = Smpte;
}

void Box_abst::SetMovieIdentfier( std::string Identifier ) {
  curMovieIdentifier = Identifier;
}

void Box_abst::SetDRM( std::string Drm ) {
  curDRM = Drm;
}

void Box_abst::SetMetaData( std::string MetaData ) {
  curMetaData = MetaData;
}

void Box_abst::AddServerEntry( std::string Url, uint32_t Offset ) {
  if(Offset >= Servers.size()) {
    Servers.resize(Offset+1);
  }
  Servers[Offset].ServerBaseUrl = Url;
}

void Box_abst::AddQualityEntry( std::string Quality, uint32_t Offset ) {
  if(Offset >= Qualities.size()) {
    Qualities.resize(Offset+1);
  }
  Qualities[Offset].QualityModifier = Quality;
}

void Box_abst::AddSegmentRunTable( Box * newSegment, uint32_t Offset ) {
  if( Offset >= SegmentRunTables.size() ) {
    SegmentRunTables.resize(Offset+1);
  }
  if( SegmentRunTables[Offset] ) {
    delete SegmentRunTables[Offset];
  }
  SegmentRunTables[Offset] = newSegment;
}

void Box_abst::AddFragmentRunTable( Box * newFragment, uint32_t Offset ) {
  if( Offset >= FragmentRunTables.size() ) {
    FragmentRunTables.resize(Offset+1);
  }
  if( FragmentRunTables[Offset] ) {
    delete FragmentRunTables[Offset];
  }
  FragmentRunTables[Offset] = newFragment;
}


void Box_abst::SetDefaults( ) {
  SetProfile( );
  SetLive( );
  SetUpdate( );
  SetTimeScale( );
  SetMediaTime( );
  SetSMPTE( );
  SetMovieIdentfier( );
  SetDRM( );
  SetMetaData( );
}

void SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_abst::WriteContent( ) {
  Container->ResetPayload( );
  SetReserved( );
  Box * current;
  std::string serializedSegments = "";
  int SegmentAmount = 0;
  for( uint32_t i = 0; i < SegmentRunTables.size(); i++ ) {
    current=SegmentRunTables[i];
    if( current ) {
      SegmentAmount ++;
      serializedSegments.append((char*)current->GetBoxedata(),current->GetBoxedDataSize());
    }
  }
  std::string serializedFragments = "";
  int FragmentAmount = 0;
  for( uint32_t i = 0; i < FragmentRunTables.size(); i++ ) {
    current=FragmentRunTables[i];
    if( current ) {
      FragmentAmount ++;
      serializedFragments.append((char*)current->GetBoxedata(),current->GetBoxedDataSize());
    }
  }
  //NO_OFFSET
  uint8_t * temp = new uint8_t[1];
  temp[0] = 0 & ( curProfile << 6 ) & ( (uint8_t)isLive << 7 ) & ( (uint8_t)isUpdate << 7 );
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curBootstrapInfoVersion),4);
  Container->SetPayload((uint32_t)1,temp,8);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curTimeScale),9);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),13);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curMediaTime),17);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),21);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curSMPTE),25);
  Container->SetPayload((uint32_t)curMovieIdentifier.size(),(uint8_t*)curMovieIdentifier.c_str(),29);
  
  //CalcOffsets
  Container->SetPayload((uint32_t)serializedSegments.size(),(uint8_t*)serializedSegments.c_str());
  Container->SetPayload((uint32_t)serializedFragments.size(),(uint8_t*)serializedFragments.c_str());
}
