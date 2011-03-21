#include <stdint.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include "../util/http_parser.cpp"
#include "../util/MP4/box_includes.h"
#include "../util/flv_data.cpp"

int main(){
  HTTPReader H;
  FLV_Pack * F = 0;
  unsigned int P = 0;
  char * Payload = 0;
  
  while (H.ReadSocket(stdin) || H.CleanForNext()){
    if (H.body.size() > 10000){
      Box * TestBox = new Box((uint8_t*)H.body.c_str(), H.body.size());
      Payload = (char*)TestBox->GetPayload();
      printf("First bytes: %2hhu %2hhu %2hhu %2hhu\n", Payload[0], Payload[1], Payload[2], Payload[3]);
      P = 0;
      while (TestBox->GetPayloadSize() > P){
        if (FLV_GetPacket(F, Payload, TestBox->GetPayloadSize(), P)){
          std::cout << "Got a " << F->len << " bytes " << F->tagType() << " FLV tag of time " << F->tagTime() << "." << std::endl;
        }
      }
      delete TestBox;
    }else{
      std::cout << "Skipped too small fragment of size " << H.body.size() << std::endl;
    }
  }
}
