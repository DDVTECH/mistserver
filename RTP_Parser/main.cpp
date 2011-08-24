#include <iostream>
#include <cstdio>

///A struct that will contain all data stored in a RTP Header
struct RTP_Header {
 char Version;
 bool Padding;
 bool Extension;
 char CSRC_Count;
 bool Marker;
 char Payload_Type;
 int Sequence_Number;
 int Timestamp;
 int SSRC;
};//RTP_Header


///Fills a RTP Header
///\param hdr A RTP Header structure
///\param Header A characterpointer to an RTP packet
///\param HeaderSize the expected length of the header
void Read_Header( RTP_Header & hdr, char * Header, int HeaderSize ) {
  hdr.Version = ( Header[0] & 0xC0 ) >> 6;
  hdr.Padding = ( Header[0] & 0x20 ) >> 5;
  hdr.Extension = ( Header[0] & 0x10 ) >> 4;
  hdr.CSRC_Count = ( Header[0] & 0x0F );
  hdr.Marker = ( Header[1] & 0x80 ) >> 7;
  hdr.Payload_Type = ( Header[1] & 0x7F );
  hdr.Sequence_Number = ( ( ( Header[2] ) << 8 ) + ( Header[3] ) ) & 0x0000FFFF;
  hdr.Timestamp = ( ( Header[4] ) << 24 ) + ( ( Header[5] ) << 16 ) + ( ( Header[6] ) << 8 ) + ( Header[7] );
  hdr.SSRC = ( ( Header[8] ) << 24 ) + ( ( Header[9] ) << 16 ) + ( ( Header[10] ) << 8 ) + ( Header[11] );
}

///Prints a RTP header
///\param hdr The RTP Header
void Print_Header( RTP_Header hdr ) {
  printf( "RTP Header:\n" );
  printf( "\tVersion:\t\t%d\n", hdr.Version );
  printf( "\tPadding:\t\t%d\n", hdr.Padding );
  printf( "\tExtension:\t\t%d\n", hdr.Extension );
  printf( "\tCSRC Count:\t\t%d\n", hdr.CSRC_Count );
  printf( "\tMarker:\t\t\t%d\n", hdr.Marker );
  printf( "\tPayload Type:\t\t%d\n", hdr.Payload_Type );
  printf( "\tSequence Number:\t%d\n", hdr.Sequence_Number );
  printf( "\tTimestamp:\t\t%u\n", hdr.Timestamp );
  printf( "\tSSRC:\t\t\t%u\n", hdr.SSRC );
}

int main( ) {
  int HeaderSize = 12;
  char Header[ HeaderSize ];
  
  for( int i = 0; i < HeaderSize; i++ ) {
    if( !std::cin.good() ) { break; }
    Header[ i ] = std::cin.get();
  }
  
  RTP_Header hdr;
  
  Read_Header( hdr, Header, HeaderSize );
  Print_Header( hdr );
  return 0;
}
