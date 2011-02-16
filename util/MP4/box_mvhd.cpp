#include "box.cpp"
#include <ctime>

#define SECONDS_DIFFERENCE 2082844800

class Box_mvhd {
  public:
    Box_mvhd( );
    ~Box_mvhd();
    Box * GetBox();
    void SetCreationTime( uint32_t TimeStamp = 0 );
    void SetModificationTime( uint32_t TimeStamp = 0 );
    void SetTimeScale( uint32_t TimeUnits = 1 );
    void SetDurationTime( uint32_t TimeUnits = 0 );
    void SetRate( uint32_t Rate = 0x00010000 );
    void SetVolume( uint16_t Volume = 0x0100 );
    void SetNextTrackID( uint32_t TrackID = 0xFFFFFFFF );
  private:
    void SetReserved();
    void SetDefaults();
    Box * Container;
    
};//Box_ftyp Class

Box_mvhd::Box_mvhd( ) {
  Container = new Box( 0x6D766864 );
  SetDefaults();
  SetReserved();
}

Box_mvhd::~Box_mvhd() {
  delete Container;
}

Box * Box_mvhd::GetBox() {
  return Container;
}

void Box_mvhd::SetCreationTime( uint32_t TimeStamp ) {
  uint32_t CreationTime;
  if(!TimeStamp) {
    CreationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    CreationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(CreationTime),4);
}

void Box_mvhd::SetModificationTime( uint32_t TimeStamp ) {
  uint32_t ModificationTime;
  if(!TimeStamp) {
    ModificationTime = time(NULL) + SECONDS_DIFFERENCE;
  } else {
    ModificationTime = TimeStamp;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(ModificationTime),8);
}

void Box_mvhd::SetTimeScale( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),12);
}

void Box_mvhd::SetDurationTime( uint32_t TimeUnits ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TimeUnits),16);
}

void Box_mvhd::SetRate( uint32_t Rate ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(Rate),20);
}

void Box_mvhd::SetVolume( uint16_t Volume ) {
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(Volume),24);
}

void Box_mvhd::SetNextTrackID( uint32_t TrackID ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(TrackID),92);
}

void Box_mvhd::SetReserved() {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),88);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),84);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),80);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),76);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),72);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),68);

  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0x40000000),64);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),60);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),56);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),52);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0x00010000),48);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),44);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),40);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),36);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0x00010000),32);

  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),28);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),24);
  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0),22);

  Container->SetPayload((uint32_t)4,Box::uint16_to_uint8(0));
}

void Box_mvhd::SetDefaults() {
  SetCreationTime();
  SetModificationTime();
  SetDurationTime();
  SetNextTrackID();
  SetRate();
  SetVolume();
  SetTimeScale();
}

