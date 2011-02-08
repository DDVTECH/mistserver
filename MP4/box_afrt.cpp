#include "box_afrt.h"

Box_afrt::Box_afrt( ) {
  Container = new Box( 0x61667274 );
}

Box_afrt::~Box_afrt() {
  delete Container;
}

Box * Box_afrt::GetBox() {
  return Container;
}

void Box_afrt::SetUpdate( bool Update ) {
  isUpdate = Update;
}

void Box_afrt::AddQualityEntry( std::string Quality, uint32_t Offset ) {
  if(Offset >= QualitySegmentUrlModifiers.size()) {
    QualitySegmentUrlModifiers.resize(Offset+1);
  }
  QualitySegmentUrlModifiers[Offset] = Quality;
}

void Box_afrt::AddFragmentRunEntry( uint32_t FirstFragment, uint32_t FirstFragmentTimestamp, uint32_t FragmentsDuration, uint8_t Discontinuity, uint32_t Offset ) {
  if( Offset >= FragmentRunEntryTable.size() ) {
    FragmentRunEntryTable.resize(Offset+1);
  }
  FragmentRunEntryTable[Offset].FirstFragment = FirstFragment;
  FragmentRunEntryTable[Offset].FirstFragmentTimestamp = FirstFragmentTimestamp;
  FragmentRunEntryTable[Offset].FragmentDuration = FragmentsDuration;
  FragmentRunEntryTable[Offset].DiscontinuityIndicator = Discontinuity;
}

void Box_afrt::SetDefaults( ) {
  SetUpdate( );
  SetTimeScale( );
}

void Box_afrt::SetTimeScale( uint32_t Scale ) {
  curTimeScale = Scale;
}

void Box_afrt::WriteContent( ) {
  std::string serializedQualities = "";
  std::string serializedFragmentEntries = "";
  Container->ResetPayload( );

  for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
    serializedQualities.append(QualitySegmentUrlModifiers[i].c_str());
    serializedQualities += '\0';
  }
  for( uint32_t i = 0; i < FragmentRunEntryTable.size(); i ++ ) {
    serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FirstFragment));
    serializedFragmentEntries.append((char*)Box::uint32_to_uint8(0));
    serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FirstFragmentTimestamp));
    serializedFragmentEntries.append((char*)Box::uint32_to_uint8(FragmentRunEntryTable[i].FragmentDuration));
    if(FragmentRunEntryTable[i].FragmentDuration == 0) {
    serializedFragmentEntries.append((char*)Box::uint8_to_uint8(FragmentRunEntryTable[i].DiscontinuityIndicator));
    }
  }

  uint32_t OffsetFragmentRunEntryCount = 9 + serializedQualities.size();

  Container->SetPayload((uint32_t)serializedFragmentEntries.size(),(uint8_t*)serializedFragmentEntries.c_str(),OffsetFragmentRunEntryCount+4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(FragmentRunEntryTable.size()),OffsetFragmentRunEntryCount);
  Container->SetPayload((uint32_t)serializedQualities.size(),(uint8_t*)serializedQualities.c_str(),9);
  Container->SetPayload((uint32_t)1,Box::uint8_to_uint8(QualitySegmentUrlModifiers.size()),8);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(curTimeScale));
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8((isUpdate ? 1 : 0)));
}
