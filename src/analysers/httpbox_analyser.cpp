/// \file HTTP_Box_Parser/main.cpp
/// Debugging tool for F4M HTTP streaming data.
/// Expects raw TCP data through stdin, outputs human-readable information to stderr.
/// This will attempt to read either HTTP requests or responses from stdin, and if the body is more than
/// 10,000 bytes long will attempt to parse the data as a MP4 box. (Other cases show a message about the fragment being too small)
/// Then it will take the payload of this box, print the first four bytes, and attempt to parse the whole payload as FLV data.
/// The parsed FLV data is then pretty-printed, containing information about the codec parameters and types of tags it encounters.

#include <stdint.h>
#include <iostream>
#include <string>
#include <stdio.h>
#include "../../util/http_parser.h"
#include "../../util/MP4/box_includes.h"
#include "../../util/flv_tag.h"

/// Debugging tool for F4M HTTP streaming data.
/// Expects raw TCP data through stdin, outputs human-readable information to stderr.
/// This will attempt to read either HTTP requests or responses from stdin, and if the body is more than
/// 10,000 bytes long will attempt to parse the data as a MP4 box. (Other cases show a message about the fragment being too small)
/// Then it will take the payload of this box, print the first four bytes, and attempt to parse the whole payload as FLV data.
/// The parsed FLV data is then pretty-printed, containing information about the codec parameters and types of tags it encounters.
int main(){
  HTTP::Parser H;
  FLV::Tag F;
  unsigned int P = 0;
  char * Payload = 0;
  
  while (H.Read(stdin) || H.CleanForNext()){
    if (H.body.size() > 10000){
      Box * TestBox = new Box((uint8_t*)H.body.c_str(), H.body.size());
      Payload = (char*)TestBox->GetPayload();
      printf("First bytes: %2hhu %2hhu %2hhu %2hhu\n", Payload[0], Payload[1], Payload[2], Payload[3]);
      P = 0;
      while (TestBox->GetPayloadSize() > P){
        if (F.MemLoader(Payload, TestBox->GetPayloadSize(), P)){
          std::cout << "Got a " << F.len << " bytes " << F.tagType() << " FLV tag of time " << F.tagTime() << "." << std::endl;
        }
      }
      delete TestBox;
    }else{
      std::cout << "Skipped too small fragment of size " << H.body.size() << std::endl;
    }
  }
}//main
