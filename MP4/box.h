#include "boxheader.h"

class Box {
  public:
    Box();
    Box(uint32_t BoxType);
    ~Box();

    void SetBoxType(uint32_t BoxType);
    uint32_t GetBoxType();

    void SetPayload(uint32_t PayloadSize, uint8_t * Data);
    uint8_t * GetPayload();
    uint8_t * GetPayload(uint32_t Index, uint32_t Size);
  private:
    BoxHeader header;
    uint8_t * Payload;
};//Box Class

Box::Box() {
  Payload = NULL;
}

Box::Box(uint32_t BoxType) {
  header.BoxType = BoxType;
  Payload = NULL;
}

Box::~Box() {
}

void Box::SetBoxType(uint32_t BoxType) {
  header.BoxType = BoxType;
}

uint32_t Box::GetBoxType() {
  return header.BoxType;
}

void Box::SetPayload(uint32_t PayloadSize, uint8_t * Data ) {
  if ( Payload != NULL ) { delete Payload; }
  Payload = new uint8_t[PayloadSize];
  memcpy( Payload, Data, PayloadSize );
  header.TotalSize = PayloadSize + 8;
}

uint8_t * Box::GetPayload() {
  uint8_t * temp = new uint8_t[header.TotalSize - 8];
  memcpy( temp, Payload, header.TotalSize - 8 );
  return temp;
}

uint8_t * Box::GetPayload(uint32_t Index, uint32_t Size) {
  if(
  uint8_t * temp = new uint8_t[header.TotalSize - 8];
  memcpy( temp, Payload, header.TotalSize - 8 );
  return temp;
}
