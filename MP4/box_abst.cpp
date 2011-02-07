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
