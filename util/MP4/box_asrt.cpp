#include "box.cpp"
#include <string>
#include <vector>

struct asrt_segmentrunentry {
  uint32_t FirstSegment;
  uint32_t FragmentsPerSegment;
};//abst_qualityentry

class Box_asrt {
  public:
    Box_asrt( );
    ~Box_asrt();
    Box * GetBox();
    void SetUpdate( bool Update = false );
    void AddQualityEntry( std::string Quality = "", uint32_t Offset = 0 );
    void AddSegmentRunEntry( uint32_t FirstSegment = 0, uint32_t FragmentsPerSegment = 100, uint32_t Offset = 0 );
    void WriteContent( );
  private:
    void SetDefaults( );
    bool isUpdate;
    std::vector<std::string> QualitySegmentUrlModifiers;
    std::vector<asrt_segmentrunentry> SegmentRunEntryTable;
    Box * Container;
};//Box_ftyp Class

Box_asrt::Box_asrt( ) {
  Container = new Box( 0x61737274 );
}

Box_asrt::~Box_asrt() {
  delete Container;
}

Box * Box_asrt::GetBox() {
  return Container;
}

void Box_asrt::SetUpdate( bool Update ) {
  isUpdate = Update;
}

void Box_asrt::AddQualityEntry( std::string Quality, uint32_t Offset ) {
  if(Offset >= QualitySegmentUrlModifiers.size()) {
    QualitySegmentUrlModifiers.resize(Offset+1);
  }
  QualitySegmentUrlModifiers[Offset] = Quality;
}

void Box_asrt::AddSegmentRunEntry( uint32_t FirstSegment, uint32_t FragmentsPerSegment, uint32_t Offset ) {
  if( Offset >= SegmentRunEntryTable.size() ) {
    SegmentRunEntryTable.resize(Offset+1);
  }
  SegmentRunEntryTable[Offset].FirstSegment = FirstSegment;
  SegmentRunEntryTable[Offset].FragmentsPerSegment = FragmentsPerSegment;
}

void Box_asrt::SetDefaults( ) {
  SetUpdate( );
}

void Box_asrt::WriteContent( ) {
  std::string serializedQualities = "";
  Container->ResetPayload( );

  for( uint32_t i = 0; i < QualitySegmentUrlModifiers.size(); i++ ) {
    serializedQualities.append(QualitySegmentUrlModifiers[i].c_str());
    serializedQualities += '\0';
  }

  uint32_t OffsetSegmentRunEntryCount = 5 + serializedQualities.size();
  
  for( uint32_t i = 0; i < SegmentRunEntryTable.size(); i ++ ) {
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable[i].FragmentsPerSegment),(8*i)+OffsetSegmentRunEntryCount+8);
    Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable[i].FirstSegment),(8*i)+OffsetSegmentRunEntryCount+4);
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(SegmentRunEntryTable.size()),OffsetSegmentRunEntryCount);  
  Container->SetPayload((uint32_t)serializedQualities.size(),(uint8_t*)serializedQualities.c_str(),5);
  Container->SetPayload((uint32_t)1,Box::uint8_to_uint8(QualitySegmentUrlModifiers.size()),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8((isUpdate ? 1 : 0)));
}
