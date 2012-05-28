#include "ts_packet.h"

TS_Packet::TS_Packet() {
  Clear( );
}

TS_Packet::TS_Packet( std::string & Data ) {
  if( Data.size() < 188 ) {
    Clear( );    
  } else {
    for( int i = 0; i < 188; i++ ) {
      Buffer[i] = Data[i];
    }
    Data.erase(0,188);
    Free = 0;
  }
}

TS_Packet::~TS_Packet() { }

void TS_Packet::PID( int NewVal ) {
  Buffer[1] = (Buffer[1] & 0xE0) + ((NewVal & 0x1F00) >> 8 );
  Buffer[2] = (NewVal & 0x00FF);
  Free = std::min( Free, 185 );
}

int TS_Packet::PID() {
  return (( Buffer[1] & 0x1F ) << 8 ) + Buffer[2];
}

void TS_Packet::ContinuityCounter( int NewVal ) {
  Buffer[3] = ( Buffer[3] & 0xF0 ) + ( NewVal & 0x0F );
  Free = std::min( Free, 184 );
}

int TS_Packet::ContinuityCounter() {
  return ( Buffer[3] & 0x0F );
}

int TS_Packet::BytesFree( ) {
  return Free;
}

void TS_Packet::Clear( ) {
  Free = 187;
  Buffer[0] = 0x47;
  for( int i = 1; i < 188; i++ ) {
    Buffer[i] = 0x00;
  }
  AdaptionField( 1 );
}

void TS_Packet::AdaptionField( int NewVal ) {
  NewVal = NewVal % 4;
  NewVal = NewVal << 4;
  Buffer[3] = ( Buffer[3] & 0xCF ) + NewVal;
  Free = std::min( Free, 184 );
}

int TS_Packet::AdaptionField( ) {
  return ((Buffer[3] & 0x30) >> 4 ) >= 2;
}

void TS_Packet::PCR( int64_t NewVal ) {
  NewVal += (0xF618 * 300);
  Buffer[3] = (Buffer[3] | 0x30);
  Buffer[4] = 7;
  Buffer[5] = (Buffer[5] | 0x10 );
  int64_t TmpVal = NewVal / 300;
  Buffer[6] = (((TmpVal>>1)>>24) & 0xFF);
  Buffer[7] = (((TmpVal>>1)>>16) & 0xFF);
  Buffer[8] = (((TmpVal>>1)>>8) & 0xFF);
  Buffer[9] = ((TmpVal>>1) & 0xFF);
  int Remainder = NewVal % 300;
  Buffer[10] = 0x7E + ((TmpVal & 0x01)<<7) + ((Remainder & 0x0100) >> 8 );
  Buffer[11] = (Remainder & 0x00FF);
  Free = std::min( Free, 176 );
};

int64_t TS_Packet::PCR( ) {
  if( !AdaptionField() ) {
    return -1;
  }
  if( !(Buffer[5] & 0x10 ) ) {
    return -1;
  }
  int64_t Result = 0;
  Result = (((((((Buffer[6] << 8) + Buffer[7]) << 8) + Buffer[8]) << 8) + Buffer[9]) << 1) + ( Buffer[10] & 0x80 );
  Result = Result * 300;
  Result += ((Buffer[10] & 0x01) << 8 + Buffer[11]);
  return Result;
}

int TS_Packet::AdaptionFieldLen( ) {
  if( !AdaptionField() ) {
    return -1;
  }
  return (int)Buffer[4];
}

void TS_Packet::Print( ) {
  std::cout << "TS Packet: " << (Buffer[0] == 0x47)
            << "\n\tNewUnit: " << UnitStart()
            << "\n\tPID: " << PID()
            << "\n\tContinuity Counter: " << ContinuityCounter()
            << "\n\tAdaption Field: " << AdaptionField() << "\n";
  if( AdaptionField() ) {
    std::cout << "\t\tAdaption Field Length: " << AdaptionFieldLen() << "\n";
    if( AdaptionFieldLen() ) {
      std::cout << "\t\tRandom Access: " << RandomAccess() << "\n";
    }
    if( PCR() != -1 ) {
      std::cout << "\t\tPCR: " << PCR() << "( " << (double)PCR() / 27000000 << " s )\n";
    }
  }
}

int TS_Packet::UnitStart( ) {
  return ( Buffer[1] & 0x40) >> 6;
}

void TS_Packet::UnitStart( int NewVal ) {
  if( NewVal ) {
    Buffer[1] = (Buffer[1] | 0x40);
  } else {
    Buffer[1] = (Buffer[1] & 0xBF);
  }
}

int TS_Packet::RandomAccess( ) {
  if( !AdaptionField() ) {
    return -1;
  }
  return ( Buffer[5] & 0x40) >> 6;
}

void TS_Packet::RandomAccess( int NewVal ) {
  if( AdaptionField() ) {
    if( NewVal ) {
      Buffer[5] = (Buffer[5] | 0x40);
    } else {
      Buffer[5] = (Buffer[5] & 0xBF);
    }
  } else {
    Buffer[3] = (Buffer[3] | 0x30);
    Buffer[4] = 1;
    if( NewVal ) {
      Buffer[5] = 0x40;
    } else {
      Buffer[5] = 0x00;
    }
    Free = 183;
  }
}

void TS_Packet::DefaultPAT( ) {
  static int MyCntr = 0;
  UnitStart( 1 );
  PID( 0 );
  ContinuityCounter( MyCntr );
  AdaptionField( 0x01 );
  Buffer[ 4] = 0x00;//Pointer Field
  Buffer[ 5] = 0x00;//TableID
  Buffer[ 6] = 0xB0,//Reserved + SectionLength
  Buffer[ 7] = 0x0D;//SectionLength (Cont)
  Buffer[ 8] = 0x00;//Transport_Stream_ID
  Buffer[ 9] = 0x01;//Transport_Stream_ID (Cont)
  Buffer[10] = 0xC1;//Reserved + VersionNumber + Current_Next_Indicator
  Buffer[11] = 0x00;//Section_Number
  Buffer[12] = 0x00;//Last_Section_Number
  Buffer[13] = 0x00;  //Program Number
  Buffer[14] = 0x01;  //Program Number (Cont)
  Buffer[15] = 0xF0;  //Reserved + Program_Map_PID
  Buffer[16] = 0x00;  //Program_Map_PID (Cont)
  Buffer[17] = 0x2A;//CRC32
  Buffer[18] = 0xB1;//CRC32 (Cont)
  Buffer[19] = 0x04;//CRC32 (Cont)
  Buffer[20] = 0xB2;//CRC32 (Cont)
  std::fill( Buffer+21, Buffer+188, 0xFF );
  Free = 0;
  MyCntr = ( (MyCntr + 1) % 0xF);
}

void TS_Packet::DefaultPMT( ) {
  static int MyCntr = 0;
  UnitStart( 1 );
  PID( 0x1000 );
  ContinuityCounter( MyCntr );
  AdaptionField( 0x01 );
  Buffer[ 4] = 0x00;//Pointer Field
  Buffer[ 5] = 0x02;//TableID
  Buffer[ 6] = 0xb0;//Reserved + Section_Length
  Buffer[ 7] = 0x12;//Section_Length (Cont)
  Buffer[ 8] = 0x00;//Program_Number
  Buffer[ 9] = 0x01;//Program_Number (Cont)
  Buffer[10] = 0xc1;//Reserved + VersionNumber + Current_Next_Indicator
  Buffer[11] = 0x00;//Section_Number
  Buffer[12] = 0x00;//Last_Section_Number
  Buffer[13] = 0xe1;//Reserved + PCR_PID
  Buffer[14] = 0x00;//PCR_PID (Cont)
  Buffer[15] = 0xf0;//Reserved + Program_Info_Length
  Buffer[16] = 0x00;//Program_Info_Length (Cont)
  Buffer[17] = 0x1b;  //Stream_Type
  Buffer[18] = 0xe1;  //Reserved + Elementary_PID
  Buffer[19] = 0x00;  //Elementary_PID (Cont)
  Buffer[20] = 0xf0;  //Reserved + ES_Info_Length
  Buffer[21] = 0x00;  //ES_Info_Length (Cont)
  Buffer[22] = 0x15;//CRC32
  Buffer[23] = 0xbd;//CRC32 (Cont)
  Buffer[24] = 0x4d;//CRC32 (Cont)
  Buffer[25] = 0x56;//CRC32 (Cont)
  std::fill( Buffer+26, Buffer+188, 0xFF );
  Free = 0;
  MyCntr = ( (MyCntr + 1) % 0xF);
}

std::string TS_Packet::ToString( ) {
  std::string Result( Buffer, 188 );
  return Result;
}

void TS_Packet::PESLeadIn( int NewLen ) {
  static uint64_t PTS = 126000;
  NewLen += 14;
  int Offset = ( 188 - Free );
  Buffer[Offset] = 0x00;//PacketStartCodePrefix
  Buffer[Offset+1] = 0x00;//PacketStartCodePrefix (Cont)
  Buffer[Offset+2] = 0x01;//PacketStartCodePrefix (Cont)
  Buffer[Offset+3] = 0xe0;//StreamType Video
  Buffer[Offset+4] = (NewLen & 0xFF00) >> 8;//PES PacketLength
  Buffer[Offset+5] = (NewLen & 0x00FF);//PES PacketLength (Cont)
  Buffer[Offset+6] = 0x80;//Reserved + Flags
  Buffer[Offset+7] = 0x80;//PTSOnlyFlag + Flags
  Buffer[Offset+8] = 0x05;//PESHeaderDataLength
  Buffer[Offset+9] = 0x20 + ((PTS & 0x1C0000000) >> 29 ) + 1;//PTS
  Buffer[Offset+10] = 0x00 + ((PTS & 0x03FC00000) >> 22 );//PTS (Cont)
  Buffer[Offset+11] = 0x00 + ((PTS & 0x0003F8000) >> 14 ) + 1;//PTS (Cont)
  Buffer[Offset+12] = 0x00 + ((PTS & 0x000007F10) >> 7 );//PTS (Cont)
  Buffer[Offset+13] = 0x00 + ((PTS & 0x00000007F) << 1) + 1;//PTS (Cont)
  
  //PesPacket-Wise Prepended Data
  
  Buffer[Offset+14] = 0x00;//NALU StartCode
  Buffer[Offset+15] = 0x00;//NALU StartCode (Cont)
  Buffer[Offset+16] = 0x00;//NALU StartCode (Cont)
  Buffer[Offset+17] = 0x01;//NALU StartCode (Cont)
  Buffer[Offset+18] = 0x09;//NALU EndOfPacket (Einde Vorige Packet)
  Buffer[Offset+19] = 0xF0;//NALU EndOfPacket (Cont)
  Free = Free - 20;
  fprintf( stderr, "\tAdding PES Lead In with Offset %d and Size %d\n", Offset, NewLen );
  fprintf( stderr, "\t\tNew Free is %d\n", Free );
  PTS += 3003;
}

void TS_Packet::FillFree( std::string & NewVal ) {
  int Offset = (188 - Free);
  for( int i = 0; (Offset + i) < 188; i++ ) {
    Buffer[Offset+i] = NewVal[i];
  }
  NewVal.erase(0,Free);
  Free = 0;
}

void TS_Packet::AddStuffing( int NumBytes ) {
  if( NumBytes < 0 ) { return; }
  if( AdaptionField( ) == 3 ) {
    int Offset = Buffer[4];
    Buffer[4] = Offset + NumBytes - 1;
    for( int i = 0; i < ( NumBytes -2 ); i ++ ) {
      Buffer[Offset+i] = 0xFF;
    }
    Free -= NumBytes;
  } else {
    AdaptionField( 3 );
    Buffer[4] = NumBytes - 1;
    Buffer[5] = 0x00;
    for( int i = 0; i < ( NumBytes -2 ); i ++ ) {
      Buffer[6+i] = 0xFF;
    }
    Free -= NumBytes;
  }
}
