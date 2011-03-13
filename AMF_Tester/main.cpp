#define DEBUG 10 //maximum debugging level evah
#include <cstdlib>
#include <iostream>
#include <fstream>
#include <string>
#include "amf.cpp"

int main( int argc, char * argv[] ) {
  if( argc != 2 ) { return 1; }
  std::string temp;
  std::ifstream ifs( argv[1] );
  while( ifs.good() ) {
    temp += ifs.get();
  }
  static AMFType amfdata("empty", (unsigned char)0xFF);
  amfdata = parseAMF( temp );
  amfdata.Print( );
  return 0;
}
