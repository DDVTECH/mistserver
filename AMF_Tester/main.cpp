#define DEBUG 10 //maximum debugging level evah
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include "amf.cpp"

int main() {
  std::string temp;
  while( std::cin.good() ) {
    temp += std::cin.get();
  }
  static AMFType amfdata("empty", (unsigned char)AMF0_DDV_CONTAINER);
  amfdata = parseAMF( (const unsigned char*)temp.c_str(), temp.length()-1 );
  amfdata.Print( );
  return 0;
}

