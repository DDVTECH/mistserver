#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>

int main( ) {
  std::string File;
  unsigned int BlockNo = 1;
  unsigned int EmptyBlocks = 0;
  unsigned char TempChar[188];
  unsigned char Skip;
  unsigned int SkippedBytes = 0;
  unsigned int Adaptation;
  while( std::cin.good( ) && BlockNo <= 5) {
    for( int i = 0; i < 188; i++ ) {
      TempChar[i] = std::cin.get();
    }

    if( ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) != 0x1FFF ) {    
      printf( "Block %d:\n", BlockNo );
      printf( "\tSync Byte:\t\t\t%X\n", TempChar[0] );
      printf( "\tTransport Error Indicator:\t%d\n", ( ( TempChar[1] & 0x80 ) != 0 ) );
      printf( "\tPayload Unit Start Indicator:\t%d\n", ( ( TempChar[1] & 0x40 ) != 0 ) );
      printf( "\tTransport Priority:\t\t%d\n", ( ( TempChar[1] & 0x20 ) != 0 ) );
      printf( "\tPacket ID:\t\t\t%X\n", ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) );
      printf( "\tScrambling control:\t\t%d%d\n", ( ( TempChar[3] & 0x80 ) != 0 ), ( ( TempChar[3] & 0x40 ) != 0 ) );
      printf( "\tAdaptation Field Exists:\t%d%d\n", ( ( TempChar[3] & 0x20 ) != 0 ), ( ( TempChar[3] & 0x10 ) != 0 ) );
      printf( "\tContinuity Counter:\t\t%d\n", ( TempChar[3] & 0x0F ) );
      
      Adaptation = ( ( TempChar[3] & 0x30 ) >> 4 );
      
      if( Adaptation == 2 || Adaptation == 3 ) {
        printf( "\tAdaptation Field\n" );
      }
      

      if( ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) == 0 ) {
        printf( "\tProgram Association Table\n" );
        printf( "\t\ttable_id:\t\t\t%d\n", TempChar[4] );
        printf( "\t\tsection_syntax_indicator:\t%d\n", ( ( TempChar[5] & 0x80 ) != 0 ) );
        printf( "\t\t0:\t\t\t\t%d\n", ( ( TempChar[5] & 0x40 ) != 0 ) );
        printf( "\t\treserved:\t\t\t%d\n", ( ( TempChar[5] & 0x30 ) >> 4 ) );
        printf( "\t\tsection_length:\t\t\t%d\n", ( ( TempChar[5] & 0x0F ) << 8 ) + TempChar[6] );
        printf( "\t\treserved:\t\t\t%d\n", ( ( TempChar[7] & 0xC0 ) >> 6 ) );
        printf( "\t\tversion_number:\t\t\t%d\n", ( ( TempChar[7] & 0x3E ) >> 1 ) );
        printf( "\t\tcurrent_next_indicator:\t\t%d\n", ( ( TempChar[7] & 0x01 ) != 0 ) );
        printf( "\t\tsection_number:\t\t\t%d\n", TempChar[8] );
        printf( "\t\tlast_section_number:\t\t%d\n", TempChar[9] );
        
      }

      BlockNo ++;
      
    }
  else {
      EmptyBlocks ++;
    }
    
    //Find Next Sync Byte
    SkippedBytes = 0;
    while( (int)std::cin.peek( ) != 0x47 ) {
      std::cin >> Skip;
      SkippedBytes ++;
    }
    printf( "Empty Packet Counter:  %d\n", EmptyBlocks );
  }
  return 0;
}
