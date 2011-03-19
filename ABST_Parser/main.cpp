#include <stdint.h>
#include <iostream>
#include <string>
#include "../util/MP4/box_includes.h"

int main( ) {
  std::string temp;
  bool validinp = true;
  char thischar;
  while(validinp) {
    thischar = std::cin.get( );
    if(std::cin.good( ) ) {
      temp += thischar;
    } else {
      validinp = false;
    }
  }
  Box * TestBox = new Box((uint8_t*)temp.c_str( ), temp.size( ));
  TestBox->Parse( );
  delete TestBox;
}
