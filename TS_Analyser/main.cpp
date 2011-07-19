#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

struct program_association_table_entry {
  unsigned int Program_Number;
  unsigned char Reserved;
  unsigned int Program_Map_PID;
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

struct program_mapping_table {
  unsigned char Pointer_Field;
  unsigned char Table_ID;
  bool Section_Syntax_Indicator;
  bool Zero;
  unsigned char Reserved_1;
  unsigned int Section_Length;
  unsigned int Program_Number;
  unsigned char Reserved_2;
  unsigned char Version_Number;
  bool Current_Next_Indicator;
  unsigned char Section_Number;
  unsigned char Last_Section_Number;
  unsigned char Reserved_3;
  unsigned int PCR_PID;
  unsigned char Reserved_4;
  unsigned int Program_Info_Length;
  //vector Descriptors
  //vector programPIDs
  unsigned int CRC_32;
};

void print_pat( program_association_table PAT, bool Pointer_Field = false, std::string offset="\t" ) {
  printf( "%sProgram Association Table\n", offset.c_str() );
  if( Pointer_Field ) {
    printf( "%s\tPointer Field:\t\t\t%X\n", offset.c_str(), PAT.Pointer_Field );
  }
  printf( "%s\tTable ID:\t\t\t%X\n", offset.c_str(), PAT.Table_ID );
  printf( "%s\tSection Syntax Indicator:\t%d\n", offset.c_str(), PAT.Section_Syntax_Indicator );
  printf( "%s\t0:\t\t\t\t%d\n", offset.c_str(), PAT.Zero );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PAT.Reserved_1 );
  printf( "%s\tSection Length:\t\t\t%X\n", offset.c_str(), PAT.Section_Length );
  printf( "%s\tTransport Stream ID\t\t%X\n", offset.c_str(), PAT.Transport_Stream_ID );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PAT.Reserved_2 );
  printf( "%s\tVersion Number:\t\t\t%X\n", offset.c_str(), PAT.Version_Number );
  printf( "%s\tCurrent Next Indicator:\t\t%d\n", offset.c_str(), PAT.Current_Next_Indicator );
  printf( "%s\tSection Number:\t\t\t%X\n", offset.c_str(), PAT.Section_Number );
  printf( "%s\tLast Section Number:\t\t%d\n\n", offset.c_str(), PAT.Last_Section_Number );
  for( int i = 0; i < PAT.Entries.size(); i++ ) {
    printf( "%s\tEntry %d\n", offset.c_str(), i );
    printf( "%s\t\tProgram Number:\t\t%X\n", offset.c_str(), PAT.Entries[i].Program_Number );
    printf( "%s\t\tReserved:\t\t%X\n", offset.c_str(), PAT.Entries[i].Reserved );
    printf( "%s\t\tProgram Map PID:\t%X\n", offset.c_str(), PAT.Entries[i].Program_Map_PID );
  }
  printf( "\n%s\tCRC_32:\t\t\t\t%X\n", offset.c_str(), PAT.CRC_32 );
}

void fill_pat( program_association_table & PAT, unsigned char * TempChar ) {
  PAT.Pointer_Field = TempChar[4];
  PAT.Table_ID = TempChar[5];
  PAT.Section_Syntax_Indicator = ((TempChar[6] & 0x80 ) != 0 );
  PAT.Zero = (( TempChar[6] & 0x40 ) != 0 );
  PAT.Reserved_1 = (( TempChar[6] & 0x30 ) >> 4 );
  PAT.Section_Length = (( TempChar[6] & 0x0F ) << 8 ) + TempChar[7];
  PAT.Transport_Stream_ID = (( TempChar[8] << 8 ) + TempChar[9] );
  PAT.Reserved_2 = (( TempChar[10] & 0xC0 ) >> 6 );
  PAT.Version_Number = (( TempChar[10] & 0x01 ) >> 1 );
  PAT.Current_Next_Indicator = (( TempChar[10] & 0x01 ) != 0 );
  PAT.Section_Number = TempChar[11];
  PAT.Last_Section_Number = TempChar[12];
  PAT.Entries.clear( );
  for( int i = 0; i < PAT.Section_Length - 9; i += 4 ) {
    program_association_table_entry PAT_Entry;
    PAT_Entry.Program_Number = ( TempChar[13+i] << 8 ) + TempChar[14+i];
    PAT_Entry.Reserved = ( TempChar[15+i] & 0xE0 ) >> 5;
    PAT_Entry.Program_Map_PID = (( TempChar[15+i] & 0x1F ) << 8 ) + TempChar[16+i];
    PAT.Entries.push_back( PAT_Entry );
  }
  PAT.CRC_32 = ( TempChar[8+PAT.Section_Length-4] << 24 ) + ( TempChar[8+PAT.Section_Length-3] << 16 ) + ( TempChar[8+PAT.Section_Length-2] << 8 ) + ( TempChar[8+PAT.Section_Length-1] );
}

void fill_pmt( program_mapping_table & PMT, unsigned char * TempChar ) {
  PMT.Pointer_Field = TempChar[4];
  PMT.Table_ID = TempChar[5];
  PMT.Section_Syntax_Indicator = (( TempChar[6] & 0x80 ) != 0 );
  PMT.Zero = (( TempChar[6] & 0x40 ) != 0 );
  PMT.Reserved_1 = (( TempChar[6] & 0x30 ) >> 4 );
  PMT.Section_Length = (( TempChar[6] & 0x0F ) << 8 ) + TempChar[7];
}

void print_pmt( program_mapping_table PMT, bool Pointer_Field = false, std::string offset="\t" ) {
  if( Pointer_Field ) {
    printf( "%s\tPointer Field:\t\t\t%X\n", offset.c_str(), PMT.Pointer_Field );
  }
  printf( "%s\tTable ID:\t\t\t%X\n", offset.c_str(), PMT.Table_ID );
  printf( "%s\tSection Syntax Indicator:\t%d\n", offset.c_str(), PMT.Section_Syntax_Indicator);
  printf( "%s\t0:\t\t\t\t%d\n", offset.c_str(), PMT.Zero );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PMT.Reserved_1 );
  printf( "%s\tSection Length:\t\t\t%X\n", offset.c_str(), PMT.Section_Length );
}

int find_pid_in_pat( program_association_table PAT, unsigned int PID ) {
  for( int i = 0; i < PAT.Entries.size(); i++ ) {
    if( PAT.Entries[i].Program_Map_PID == PID ) {
      return PAT.Entries[i].Program_Number;
    }
  }
  return -1;
}

int main( ) {
  std::string File;
  unsigned int BlockNo = 1;
  unsigned int EmptyBlocks = 0;
  unsigned char TempChar[188];
  unsigned char Skip;
  unsigned int SkippedBytes = 0;
  unsigned int Adaptation;
  program_association_table PAT;
  program_mapping_table PMT;
  int ProgramNum;
  while( std::cin.good( ) && BlockNo <= 4) {
    for( int i = 0; i < 188; i++ ) {
      if( std::cin.good( ) ){ TempChar[i] = std::cin.get(); }
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
        fill_pat( PAT, TempChar );
        print_pat( PAT, true );
      }
      
      ProgramNum = find_pid_in_pat( PAT, ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) );
      if( ProgramNum != -1 ) {
        printf( "\tProgram Mapping Table for program %X\n", ProgramNum );
        fill_pmt( PMT, TempChar );
        print_pmt( PMT, true );
      }

      BlockNo ++;
      
    } else {
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
