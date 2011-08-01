#include <iostream>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>

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

struct program_mapping_table_entry {
  unsigned char Stream_Type;
  unsigned char Reserved_1;
  unsigned int Elementary_PID;
  unsigned char Reserved_2;
  unsigned int ES_Info_Length;
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
  std::vector<program_mapping_table_entry> Entries;
  unsigned int CRC_32;
};

struct adaptation_field {
  unsigned char Adaptation_Field_Length;
  bool Discontinuity_Indicator;
  bool Random_Access_Indicator;
  bool Elementary_Stream_Priority_Indicator;
  bool PCR_Flag;
  bool OPCR_Flag;
  bool Splicing_Point_Flag;
  bool Transport_Private_Data_Flag;
  bool Adaptation_Field_Extension_Flag;
  
  unsigned char Program_Clock_Reference_Base_MSB;
  unsigned int Program_Clock_Reference_Base;
  unsigned char PCR_Reserved;
  unsigned int Program_Clock_Reference_Extension;

  unsigned char Original_Program_Clock_Reference_Base_MSB;
  unsigned int Original_Program_Clock_Reference_Base;
  unsigned char OPCR_Reserved;
  unsigned int Original_Program_Clock_Reference_Extension;

  unsigned char Splice_Countdown;

  unsigned char Transport_Private_Data_Length;
  std::vector<unsigned char> Private_Data_Byte;
  
  unsigned char Adaptation_Field_Extension_Length;
};

struct pes_packet {
  unsigned int Packet_Start_Code_Prefix;
  unsigned char Stream_ID;
  unsigned int PES_Packet_Length;
  
  unsigned char Two;
  unsigned char PES_Scrambling_Control;
  bool PES_Priority;
  bool Data_Alignment_Indicator;
  bool Copyright;
  bool Original_Or_Copy;
  unsigned char PTS_DTS_Flags;
  bool ESCR_Flag;
  bool ES_Rate_Flag;
  bool DSM_Trick_Mode_Flag;
  bool Additional_Copy_Info_Flag;
  bool PES_CRC_Flag;
  bool PES_Extension_Flag;
  unsigned char PES_Header_Data_Length;
  
  unsigned char PTS_Spacer;
  unsigned char PTS_MSB;
  unsigned int PTS;
  
  unsigned char DTS_Spacer;
  unsigned char DTS_MSB;
  unsigned int DTS;
  
  std::vector<unsigned char> Header_Stuffing;
  std::vector<unsigned char> First_Bytes;
};

void fill_pes( pes_packet & PES, unsigned char * TempChar, int Offset = 4 ) {
  PES.Packet_Start_Code_Prefix = ( TempChar[Offset] << 16 ) + ( TempChar[Offset+1] << 8 ) + TempChar[Offset+2];
  PES.Stream_ID = TempChar[Offset+3];
  PES.PES_Packet_Length = ( TempChar[Offset+4] << 8 ) + TempChar[Offset+5];
  Offset += 6;
  if( true ) { //TODO: Fill in boolean, but for Buck Bunny Always
    PES.Two = ( TempChar[Offset] & 0xC0 ) >> 6;
    PES.PES_Scrambling_Control = ( TempChar[Offset] & 0x30 ) >> 4;
    PES.PES_Priority = ( TempChar[Offset] & 0x08 ) >> 3;
    PES.Data_Alignment_Indicator = ( TempChar[Offset] & 0x04 ) >> 2;
    PES.Copyright = ( TempChar[Offset] & 0x02 ) >> 1;
    PES.Original_Or_Copy = ( TempChar[Offset] & 0x01 );
    Offset ++;
    PES.PTS_DTS_Flags = ( TempChar[Offset] & 0xC0 ) >> 6;
    PES.ESCR_Flag = ( TempChar[Offset] & 0x20 ) >> 5;
    PES.ES_Rate_Flag = ( TempChar[Offset] & 0x10 ) >> 4;
    PES.DSM_Trick_Mode_Flag = ( TempChar[Offset] & 0x08 ) >> 3;
    PES.Additional_Copy_Info_Flag = ( TempChar[Offset] & 0x04 ) >> 2;
    PES.PES_CRC_Flag = ( TempChar[Offset] & 0x02 ) >> 1;
    PES.PES_Extension_Flag = ( TempChar[Offset] & 0x01 );
    Offset ++;
    PES.PES_Header_Data_Length = TempChar[Offset];
    Offset ++;
    int HeaderStart = Offset;
    if( PES.PTS_DTS_Flags >= 2 ) {
      PES.PTS_Spacer = ( TempChar[Offset] & 0xF0 ) >> 4;
      PES.PTS_MSB = ( TempChar[Offset] & 0x08 ) >> 3;
      PES.PTS = ( ( TempChar[Offset] & 0x06 ) << 29 );
      PES.PTS += ( TempChar[Offset+1] ) << 22;
      PES.PTS += ( ( TempChar[Offset+2] ) & 0xFE ) << 14;
      PES.PTS += ( TempChar[Offset+3] ) << 7;
      PES.PTS += ( ( TempChar[Offset+4] & 0xFE ) >> 1 );
      Offset += 5;
    }
    if( PES.PTS_DTS_Flags == 3 ) {
      PES.DTS_Spacer = ( TempChar[Offset] & 0xF0 ) >> 4;
      PES.DTS_MSB = ( TempChar[Offset] & 0x08 ) >> 3;
      PES.DTS = ( ( TempChar[Offset] & 0x06 ) << 29 );
      PES.DTS += ( TempChar[Offset+1] ) << 22;
      PES.DTS += ( ( TempChar[Offset+2] ) & 0xFE ) << 14;
      PES.DTS += ( TempChar[Offset+3] ) << 7;
      PES.DTS += ( ( TempChar[Offset+4] & 0xFE ) >> 1 );
      Offset += 5;
    }
    PES.Header_Stuffing.clear();
    while( Offset < HeaderStart + PES.PES_Header_Data_Length ) {
      PES.Header_Stuffing.push_back( TempChar[Offset] );
      Offset ++;
    }
    PES.First_Bytes.clear();
    for( int i = 0; i < 30; i ++ ) {
      PES.First_Bytes.push_back( TempChar[Offset+i] );
    }
  }
}

void print_pes( pes_packet PES, std::string offset="\t" ) {
  printf( "%sPES Header\n", offset.c_str() );
  printf( "%s\tPacket Start Code Prefix\t%.6X\n", offset.c_str(), PES.Packet_Start_Code_Prefix );
  printf( "%s\tStream ID\t\t\t%X\n", offset.c_str(), PES.Stream_ID );
  printf( "%s\tPES Packet Length\t\t%X\n", offset.c_str(), PES.PES_Packet_Length );
  if( true ) { //TODO: Fill in boolean, but for Buck Bunny Always
    printf( "%s\tTwo:\t\t\t\t%d\n", offset.c_str(), PES.Two );
    printf( "%s\tPES Scrambling Control:\t\t%d\n", offset.c_str(), PES.PES_Scrambling_Control );
    printf( "%s\tPES Priority:\t\t\t%d\n", offset.c_str(), PES.PES_Priority );
    printf( "%s\tData Alignment Indicator:\t%d\n", offset.c_str(), PES.Data_Alignment_Indicator );
    printf( "%s\tCopyright:\t\t\t%d\n", offset.c_str(), PES.Copyright );
    printf( "%s\tOriginal Or Copy:\t\t%d\n", offset.c_str(), PES.Original_Or_Copy );
    printf( "%s\tPTS DTS Flags:\t\t\t%d\n", offset.c_str(), PES.PTS_DTS_Flags );
    printf( "%s\tESCR Flag:\t\t\t%d\n", offset.c_str(), PES.ESCR_Flag );
    printf( "%s\tES Rate Flag:\t\t\t%d\n", offset.c_str(), PES.ES_Rate_Flag );
    printf( "%s\tDSM Trick Mode Flag:\t\t%d\n", offset.c_str(), PES.DSM_Trick_Mode_Flag );
    printf( "%s\tAdditional Copy Info Flag:\t%d\n", offset.c_str(), PES.Additional_Copy_Info_Flag );
    printf( "%s\tPES CRC Flag:\t\t\t%d\n", offset.c_str(), PES.PES_CRC_Flag );
    printf( "%s\tPES Extension Flag:\t\t%d\n", offset.c_str(), PES.PES_Extension_Flag );
    printf( "%s\tPES Header Data Length:\t\t%d\n", offset.c_str(), PES.PES_Header_Data_Length );
    if( PES.PTS_DTS_Flags >= 2 ) {
      printf( "%s\tPTS Spacer\t\t\t%d\n", offset.c_str(), PES.PTS_Spacer );
      printf( "%s\tPTS\t\t\t\t%X%.8X\n", offset.c_str(), PES.PTS_MSB, PES.PTS );
    }
    if( PES.PTS_DTS_Flags == 3 ) {
      printf( "%s\tDTS Spacer\t\t\t%d\n", offset.c_str(), PES.DTS_Spacer );
      printf( "%s\tDTS\t\t\t\t%X%.8X\n", offset.c_str(), PES.DTS_MSB, PES.DTS );
    }
    printf( "%s\tHeader Stuffing\t\t\t", offset.c_str() );
    for( int i = 0; i < PES.Header_Stuffing.size(); i++ ) {
      printf( "%.2X ", PES.Header_Stuffing[i] );
    }
    printf( "\n" );
    printf( "%s\tFirst_Bytes\t\t\t", offset.c_str() );
    for( int i = 0; i < PES.First_Bytes.size(); i++ ) {
      printf( "%.2X ", PES.First_Bytes[i] );
    }
    printf( "\n" );
  }
}

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
  int CurrentOffset;
  PMT.Pointer_Field = TempChar[4];
  PMT.Table_ID = TempChar[5];
  PMT.Section_Syntax_Indicator = (( TempChar[6] & 0x80 ) != 0 );
  PMT.Zero = (( TempChar[6] & 0x40 ) != 0 );
  PMT.Reserved_1 = (( TempChar[6] & 0x30 ) >> 4 );
  PMT.Section_Length = (( TempChar[6] & 0x0F ) << 8 ) + TempChar[7];
  PMT.Program_Number = (TempChar[8] << 8) + TempChar[9];
  PMT.Reserved_2 = (( TempChar[10] & 0xC0 ) >> 6 );
  PMT.Version_Number = (( TempChar[10] & 0x1E ) >> 1 );
  PMT.Current_Next_Indicator = ( TempChar[10] & 0x01 );
  PMT.Section_Number = TempChar[11];
  PMT.Last_Section_Number = TempChar[12];
  PMT.Reserved_3 = (( TempChar[13] & 0xE0 ) >> 5 );
  PMT.PCR_PID = (( TempChar[13] & 0x1F ) << 8 ) + TempChar[14];
  PMT.Reserved_4 = (( TempChar[15] & 0xF0 ) >> 4 );
  PMT.Program_Info_Length = ((TempChar[15] & 0x0F ) << 8 ) + TempChar[16];
  CurrentOffset = 17 + PMT.Program_Info_Length;
  PMT.Entries.clear( );
  while( CurrentOffset < PMT.Section_Length - 8 ) {
    program_mapping_table_entry PMT_Entry;
    PMT_Entry.Stream_Type = TempChar[CurrentOffset];
    PMT_Entry.Reserved_1 = (( TempChar[CurrentOffset+1] & 0xE0 ) >> 5 );
    PMT_Entry.Elementary_PID = (( TempChar[CurrentOffset+1] & 0x1F ) << 8 ) + TempChar[CurrentOffset+2];
    PMT_Entry.Reserved_2 = (( TempChar[CurrentOffset+3] & 0xF0 ) >> 4 );
    PMT_Entry.ES_Info_Length = (( TempChar[CurrentOffset+3] & 0x0F ) << 8 ) + TempChar[CurrentOffset+4];
    PMT.Entries.push_back( PMT_Entry );
    CurrentOffset += 4 + PMT_Entry.ES_Info_Length;
  }
    PMT.CRC_32 = ( TempChar[CurrentOffset] << 24 ) + ( TempChar[CurrentOffset+1] << 16 ) + ( TempChar[CurrentOffset+2] << 8 ) + ( TempChar[CurrentOffset+3] );
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
  printf( "%s\tProgram Number:\t\t\t%X\n", offset.c_str(), PMT.Program_Number );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PMT.Reserved_2 );
  printf( "%s\tVersion Number:\t\t\t%d\n", offset.c_str(), PMT.Version_Number );
  printf( "%s\tCurrent_Next_Indicator:\t\t%d\n", offset.c_str(), PMT.Current_Next_Indicator );
  printf( "%s\tSection Number:\t\t\t%d\n", offset.c_str(), PMT.Section_Number );
  printf( "%s\tLast Section Number:\t\t%d\n", offset.c_str(), PMT.Last_Section_Number );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PMT.Reserved_3 );
  printf( "%s\tPCR PID:\t\t\t%X\n", offset.c_str(), PMT.PCR_PID );
  printf( "%s\tReserved:\t\t\t%d\n", offset.c_str(), PMT.Reserved_4 );
  printf( "%s\tProgram Info Length:\t\t%d\n", offset.c_str(), PMT.Program_Info_Length );
  printf( "%s\tProgram Descriptors Go Here\n\n" );
  for( int i = 0; i < PMT.Entries.size(); i++ ) {
    printf( "%s\tEntry %d:\n", offset.c_str(), i );
    printf( "%s\t\tStream Type\t\t%d\n", offset.c_str(), PMT.Entries[i].Stream_Type );
    printf( "%s\t\tReserved\t\t%d\n", offset.c_str(), PMT.Entries[i].Reserved_1 );
    printf( "%s\t\tElementary PID\t\t%X\n", offset.c_str(), PMT.Entries[i].Elementary_PID );
    printf( "%s\t\tReserved\t\t%d\n", offset.c_str(), PMT.Entries[i].Reserved_2 );
    printf( "%s\t\tES Info Length\t\t%d\n", offset.c_str(), PMT.Entries[i].ES_Info_Length );
  }
  printf( "%s\tCRC 32\t\t%X\n", offset.c_str(), PMT.CRC_32 );
}

void fill_af( adaptation_field & AF, unsigned char * TempChar ) {
  AF.Adaptation_Field_Length = TempChar[4];
  AF.Discontinuity_Indicator = (( TempChar[5] & 0x80 ) >> 7 );
  AF.Random_Access_Indicator = (( TempChar[5] & 0x40 ) >> 6 );
  AF.Elementary_Stream_Priority_Indicator = (( TempChar[5] & 0x20 ) >> 5 );
  AF.PCR_Flag = (( TempChar[5] & 0x10 ) >> 4 );
  AF.OPCR_Flag = (( TempChar[5] & 0x08 ) >> 3 );
  AF.Splicing_Point_Flag = (( TempChar[5] & 0x04 ) >> 2 );
  AF.Transport_Private_Data_Flag = (( TempChar[5] & 0x02 ) >> 1 );
  AF.Adaptation_Field_Extension_Flag = (( TempChar[5] & 0x01 ) );
  int CurrentOffset = 6;
  if( AF.PCR_Flag ) {
    AF.Program_Clock_Reference_Base_MSB = ( ( ( TempChar[CurrentOffset] ) & 0x80 ) >> 7 );
    AF.Program_Clock_Reference_Base = ( ( ( TempChar[CurrentOffset] ) & 0x7F ) << 25 );
    AF.Program_Clock_Reference_Base += ( ( TempChar[CurrentOffset+1] ) << 17 );
    AF.Program_Clock_Reference_Base += ( ( TempChar[CurrentOffset+2] ) << 9 );
    AF.Program_Clock_Reference_Base += ( ( ( TempChar[CurrentOffset+3] ) & 0x80 ) >> 7 );
    AF.PCR_Reserved = ( ( TempChar[CurrentOffset+3] ) & 0x7E ) >> 1;
    AF.Program_Clock_Reference_Extension = ( ( TempChar[CurrentOffset+3] ) & 0x01 ) << 8 + TempChar[CurrentOffset+4];
    CurrentOffset += 5;
  } 
}

void print_af( adaptation_field AF, std::string offset="\t" ) {
  printf( "%sAdaptation Field\n", offset.c_str() );
  printf( "%s\tAdaptation Field Length\t\t\t%X\n", offset.c_str(), AF.Adaptation_Field_Length );
  printf( "%s\tDiscontinuity Indicator\t\t\t%X\n", offset.c_str(), AF.Discontinuity_Indicator );
  printf( "%s\tRandom Access Indicator\t\t\t%X\n", offset.c_str(), AF.Random_Access_Indicator );
  printf( "%s\tElementary Stream Priority Indicator\t%X\n", offset.c_str(), AF.Elementary_Stream_Priority_Indicator );
  printf( "%s\tPCR Flag\t\t\t\t%X\n", offset.c_str(), AF.PCR_Flag );
  printf( "%s\tOPCR Flag\t\t\t\t%X\n", offset.c_str(), AF.OPCR_Flag );
  printf( "%s\tSplicing Point Flag\t\t\t%X\n", offset.c_str(), AF.Splicing_Point_Flag );
  printf( "%s\tTransport Private Data Flag\t\t%X\n", offset.c_str(), AF.Transport_Private_Data_Flag );
  printf( "%s\tAdaptation Field Extension Flag\t\t%X\n", offset.c_str(), AF.Adaptation_Field_Extension_Flag );
  if( AF.PCR_Flag ) {
    printf( "\n%s\tProgram Clock Reference Base\t\t%X%.8X\n", offset.c_str(), AF.Program_Clock_Reference_Base_MSB, AF.Program_Clock_Reference_Base );
    printf( "%s\tReserved\t\t\t\t%d\n", offset.c_str(), AF.PCR_Reserved );
    printf( "%s\tProgram Clock Reference Extension\t%X\n", offset.c_str(), AF.Program_Clock_Reference_Extension );
  }
}

int find_pid_in_pat( program_association_table PAT, unsigned int PID ) {
  for( int i = 0; i < PAT.Entries.size(); i++ ) {
    if( PAT.Entries[i].Program_Map_PID == PID ) {
      return PAT.Entries[i].Program_Number;
    }
  }
  return -1;
}

bool is_elementary_pid( program_mapping_table PMT, unsigned int PID ) {
  for( int i = 0; i < PMT.Entries.size(); i++ ) {
    if( PMT.Entries[i].Elementary_PID == PID ) {
      return true;
    }
  }
  return false;
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
  adaptation_field AF;
  pes_packet PES;
  int ProgramNum;
  std::ofstream outfile;
  outfile.open( "out.ts" );
  while( std::cin.good( ) ) { // && BlockNo <= 1000 ) {
    for( int i = 0; i < 188; i++ ) {
      if( std::cin.good( ) ){ TempChar[i] = std::cin.get(); }
    }
 
    int PID = ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] );
    if( true ) { 
/*      for( int i = 0; i < 188; i++ ) {
        outfile << TempChar[i];
      } */
      
      printf( "Block %d:\n", BlockNo );
      printf( "\tSync Byte:\t\t\t%X\n", TempChar[0] );
      printf( "\tTransport Error Indicator:\t%d\n", ( ( TempChar[1] & 0x80 ) != 0 ) );
      printf( "\tPayload Unit Start Indicator:\t%d\n", ( ( TempChar[1] & 0x40 ) != 0 ) );
      printf( "\tTransport Priority:\t\t%d\n", ( ( TempChar[1] & 0x20 ) != 0 ) );
      printf( "\tPID:\t\t\t\t%X\n", ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) );
      printf( "\tScrambling control:\t\t%d\n", ( ( TempChar[3] & 0xC0 ) >> 6 ) );
      printf( "\tAdaptation Field Exists:\t%d\n", ( ( TempChar[3] & 0x30 ) >> 4 ) );
      printf( "\tContinuity Counter:\t\t%X\n", ( TempChar[3] & 0x0F ) );
      
      Adaptation = ( ( TempChar[3] & 0x30 ) >> 4 );
      
      //Adaptation Field Exists
      if( Adaptation == 2 || Adaptation == 3 ) {
        fill_af( AF, TempChar );
        print_af( AF );
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

      if( ( ( TempChar[1] & 0x40 )  ) && ( ( TempChar[1] & 0x1F ) << 8 ) + ( TempChar[2] ) ) {
        fill_pes( PES, TempChar, ( Adaption == 3 ? 4 + TempChar[4] : 4 ) );
        print_pes( PES );
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

