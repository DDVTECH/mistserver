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
  amfdata = parseAMF( (const unsigned char*)temp.c_str(), temp.length()-1 );
  amfdata.Print( );
  temp = amfdata.Pack( );
  std::ofstream ofs( "output.bin" );
  for( unsigned int i = 0; i < temp.size( ); i++ ) {
    ofs << temp[i];
  }
  return 0;
}
