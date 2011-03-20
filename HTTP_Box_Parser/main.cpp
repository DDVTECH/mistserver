#include <stdint.h>
#include <iostream>
#include <string>
#include "../util/http_parser.cpp"
#include "../util/MP4/box_includes.h"
#include "../util/flv_data.cpp"

std::string tagType(FLV_Pack * F){
  switch (F->data[0]){
    case 0x09:
      if (F->isKeyframe){
        return "video keyframe";
      }else{
        return "video";
      }
      break;
    case 0x08: return "audio"; break;
    case 0x12: return "data"; break;
  }
  return "unknown";
}

int main( ) {
  std::string temp;

  HTTPReader H;
  FLV_Pack * F = 0;
  unsigned int P = 0;
  
  while (H.ReadSocket(stdin) || H.CleanForNext()){
    if (H.body.size() > 10000){
      Box * TestBox = new Box((uint8_t*)H.body.c_str(), H.body.size());
      TestBox->Parse();
      P = 0;
      while (TestBox->PayloadSize > P){
        if (FLV_GetPacket(F, (char*)TestBox->Payload, TestBox->PayloadSize, P)){
          std::cout << "Got a " << F->len << " bytes " << tagType(F) << " FLV tag." << std::endl;
        }
      }
      delete TestBox;
    }else{
      std::cout << "Skipped too small fragment" << std::endl;
    }
  }

}
