#include <iostream>
#include "box_ftyp.h"

int main() {
  Box_ftyp * FileType = new Box_ftyp();
  printf("Boxtype: %x\n", FileType->GetBox()->GetBoxType());
  uint8_t * TestPayload = FileType->GetBox()->GetPayload();
  uint32_t TestPayloadSize = FileType->GetBox()->GetPayloadSize();
  printf("PayloadSize: %d\n", TestPayloadSize);
  for(uint32_t i = 0; i < TestPayloadSize; i++) {
    printf("Payload[%d]: %x\n", i, TestPayload[i]);
  }
  delete FileType;
  return 0;
}
