#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

struct program_association_table_entry {
  unsigned int Program_Num;
  unsigned char Reserved;
  unsigned int Program_PID;
};

struct program_association_table {
  unsigned char Pointer_Field;
  unsigned char Table_ID;
  bool Section_Syntax_Indicator;
  bool Zero;
  unsigned char Reserved_1;
  unsigned int Section_Length;
  unsigned int Transport_Stream_ID;
  unsigned char Reserved_2;
  unsigned char Version_Number;
  bool Current_Next_Indicator;
  unsigned char Section_Number;
  unsigned char Last_Section_Number;
  std::vector<program_association_table_entry> Entries;
  unsigned int CRC_32;
};

void print_pat( program_association_table PAT, bool Pointer_Field = false, std::string offset="\t" ) {
  printf( "\tProgram Association Table\n" );
  if( Pointer_Field ) {
    printf( "%s\tPointer Field:\t\t\t%X\n", offset.c_str(), PAT.Pointer_Field );
  }
  printf( "%s\tTable ID:\t\t\t%X\n", offset.c_str(), PAT.Table_ID );
  printf( "%s\tSection Syntax Indicator:\t%d\n", offset.c_str(), PAT.Section_Syntax_Indicator );
}

int main( ) {
  std::string File;
  unsigned int BlockNo = 1;
  unsigned int EmptyBlocks = 0;
  unsigned char TempChar[188];
  unsigned char Skip;
  unsigned int SkippedBytes = 0;
  unsigned int Adaptation;
  while( std::cin.good( ) && BlockNo <= 4) {
    for( int i = 0; i < 188; i++ ) {
      TempChar[i] = std::cin.get();
    }

    if( ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) != 0x1FFF ) {    
      printf( "Block %d:\n", BlockNo );
      printf( "\tSync Byte:\t\t\t%X\n", TempChar[0] );
      printf( "\tTransport Error Indicator:\t%d\n", ( ( TempChar[1] & 0x80 ) != 0 ) );
      printf( "\tPayload Unit Start Indicator:\t%d\n", ( ( TempChar[1] & 0x40 ) != 0 ) );
      printf( "\tTransport Priority:\t\t%d\n", ( ( TempChar[1] & 0x20 ) != 0 ) );
      printf( "\tPID:\t\t\t\t%X\n", ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) );
      printf( "\tScrambling control:\t\t%d%d\n", ( ( TempChar[3] & 0x80 ) != 0 ), ( ( TempChar[3] & 0x40 ) != 0 ) );
      printf( "\tAdaptation Field Exists:\t%d%d\n", ( ( TempChar[3] & 0x20 ) != 0 ), ( ( TempChar[3] & 0x10 ) != 0 ) );
      printf( "\tContinuity Counter:\t\t%X\n", ( TempChar[3] & 0x0F ) );
      
      Adaptation = ( ( TempChar[3] & 0x30 ) >> 4 );
      
      //Adaptation Field Exists
      if( Adaptation == 2 || Adaptation == 3 ) {
        printf( "\tAdaptation Field:\n" );
        printf( "\t\tNOT IMPLEMENTED YET!!\n" );
      }
      

      if( ( ( ( TempChar[1] & 0x1F ) << 8 ) +  TempChar[2] ) == 0 ) {
        printf( "\tProgram Association Table\n" );
        printf( "\t\tpointer_field:\t\t\t%X\n", TempChar[4] );
        printf( "\t\ttable_id:\t\t\t%X\n", TempChar[5] );
        printf( "\t\tsection_syntax_indicator:\t%d\n", ( ( TempChar[6] & 0x80 ) != 0 ) );
        
        printf( "\t\t0:\t\t\t\t%d\n", ( ( TempChar[6] & 0x40 ) != 0 ) );
        printf( "\t\treserved:\t\t\t%d\n", ( ( TempChar[6] & 0x30 ) >> 4 ) );
        printf( "\t\tsection_length:\t\t\t%X\n", ( ( TempChar[6] & 0x0F ) << 8 ) + TempChar[7] );
        printf( "\t\ttransport_stream_id\t\t%X\n", ( ( TempChar[8] << 8 ) + TempChar[9] ) );
        printf( "\t\treserved:\t\t\t%d\n", ( ( TempChar[10] & 0xC0 ) >> 6 ) );
        printf( "\t\tversion_number:\t\t\t%X\n", ( ( TempChar[10] & 0x3E ) >> 1 ) );
        printf( "\t\tcurrent_next_indicator:\t\t%d\n", ( ( TempChar[10] & 0x01 ) != 0 ) );
        printf( "\t\tsection_number:\t\t\t%X\n", TempChar[11] );
        printf( "\t\tlast_section_number:\t\t%d\n", TempChar[12] );
        int SectionLength = ( ( TempChar[6] & 0x0F ) << 8 ) + TempChar[7];
        for( int i = 0; i < SectionLength - 9; i += 4 ) {
          printf( "\t\tENTRY %d:\n", i / 4 );
          printf( "\t\t\tProgram Number:\t%X\n", ( TempChar[13+i] << 8 ) + TempChar[14+i] );
          printf( "\t\t\tReserved:\t%X\n", ( TempChar[15+i] & 0xE0 ) >> 5 );
          if( ( ( TempChar[13+i] << 8 ) + TempChar[14+i] ) == 0 ) {
            printf( "\t\t\tnetwork_PID:\t\t%X\n", ( ( TempChar[15+i] & 0x1F ) << 8 ) + TempChar[16+i] );
          } else {
            printf( "\t\t\tprogram_map_PID:\t\t%X\n", ( ( TempChar[15+i] & 0x1F ) << 8 ) + TempChar[16+i] );          
          }
        }
        printf( "\t\tCRC_32\t\t\t\t%x\n", ( TempChar[8+SectionLength-4] << 24 ) + ( TempChar[8+SectionLength-3] << 16 ) + ( TempChar[8+SectionLength-2] << 8 ) + ( TempChar[8+SectionLength-1] ) );
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
  }
  return 0;
}
