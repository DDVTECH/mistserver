/// \file ts_packet.cpp
/// Holds all code for the TS namespace.

#include "ts_packet.h"

/// This constructor creates an empty TS::Packet, ready for use for either reading or writing.
/// All this constructor does is call TS::Packet::Clear().
TS::Packet::Packet() { Clear( ); }

/// This function fills a TS::Packet from provided Data.
/// It fills the content with the first 188 bytes of Data.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
bool TS::Packet::FromString( std::string & Data ) {
  if( Data.size() < 188 ) {
    return false;  
  } else {
    for( int i = 0; i < 188; i++ ) { Buffer[i] = Data[i]; }
    Data.erase(0,188);
    Free = 0;
  }
  return true;
}

/// The deconstructor deletes all space that may be occupied by a TS::Packet.
TS::Packet::~Packet() { }

/// Sets the PID of a single TS::Packet.
/// \param NewPID The new PID of the packet.
void TS::Packet::PID( int NewPID ) {
  Buffer[1] = (Buffer[1] & 0xE0) + ((NewPID & 0x1F00) >> 8 );
  Buffer[2] = (NewPID & 0x00FF);
  Free = std::min( Free, 184 );
}

/// Gets the PID of a single TS::Packet.
/// \return The value of the PID.
int TS::Packet::PID() {
  return (( Buffer[1] & 0x1F ) << 8 ) + Buffer[2];
}

/// Sets the Continuity Counter of a single TS::Packet.
/// \param NewContinuity The new Continuity Counter of the packet.
void TS::Packet::ContinuityCounter( int NewContinuity ) {
  Buffer[3] = ( Buffer[3] & 0xF0 ) + ( NewContinuity & 0x0F );
  Free = std::min( Free, 184 );
}

/// Gets the Continuity Counter of a single TS::Packet.
/// \return The value of the Continuity Counter.
int TS::Packet::ContinuityCounter() {
  return ( Buffer[3] & 0x0F );
}

/// Gets the amount of bytes that are not written yet in a TS::Packet.
/// \return The amount of bytes that can still be written to this packet.
int TS::Packet::BytesFree( ) {
  return Free;
}

/// Clears a TS::Packet.
void TS::Packet::Clear( ) {
  Free = 184;
  Buffer[0] = 0x47;
  for( int i = 1; i < 188; i++ ) { Buffer[i] = 0x00; }
  AdaptationField( 1 );
}

/// Sets the selection value for an adaptationfield of a TS::Packet.
/// \param NewSelector The new value of the selection bits.
/// - 1: No AdaptationField.
/// - 2: AdaptationField Only.
/// - 3: AdaptationField followed by Data.
void TS::Packet::AdaptationField( int NewSelector ) {
  Buffer[3] = ( Buffer[3] & 0xCF ) + ((NewSelector & 0x03) << 4);
  Buffer[4] = 0;
  Free = std::min( Free, 184 );
}

/// Gets whether a TS::Packet contains an adaptationfield.
/// \return The existence of an adaptationfield.
/// - 0: No adaptationfield present.
/// - 1: Adaptationfield is present.
int TS::Packet::AdaptationField( ) {
  return ((Buffer[3] & 0x30) >> 4 );
}

/// Sets the PCR (Program Clock Reference) of a TS::Packet.
/// \param NewVal The new PCR Value.
void TS::Packet::PCR( int64_t NewVal ) {
  AdaptationField( 3 );
  Buffer[4] = 7;
  Buffer[5] = (Buffer[5] | 0x10 );
  int64_t TmpVal = NewVal / 300;
  fprintf( stderr, "\tSetting PCR_Base: %d\n", TmpVal );
  Buffer[6] = (((TmpVal>>1)>>24) & 0xFF);
  Buffer[7] = (((TmpVal>>1)>>16) & 0xFF);
  Buffer[8] = (((TmpVal>>1)>>8) & 0xFF);
  Buffer[9] = ((TmpVal>>1) & 0xFF);
  int Remainder = NewVal % 300;
  Buffer[10] = 0x7E + ((TmpVal & 0x01)<<7) + ((Remainder & 0x0100) >> 8 );
  Buffer[11] = (Remainder & 0x00FF);
  Free = std::min( Free, 176 );
};

/// Gets the PCR (Program Clock Reference) of a TS::Packet.
/// \return The value of the PCR.
int64_t TS::Packet::PCR( ) {
  if( !AdaptationField() ) {
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

/// Gets the current length of the adaptationfield.
/// \return The length of the adaptationfield.
int TS::Packet::AdaptationFieldLen( ) {
  if( !AdaptationField() ) {
    return -1;
  }
  return (int)Buffer[4];
}

/// Prints a packet to stdout, for analyser purposes.
void TS::Packet::Print( ) {
  std::cout << "TS Packet: " << (Buffer[0] == 0x47)
            << "\n\tNewUnit: " << UnitStart()
            << "\n\tPID: " << PID()
            << "\n\tContinuity Counter: " << ContinuityCounter()
            << "\n\tAdaption Field: " << AdaptationField() << "\n";
  if( AdaptationField() ) {
    std::cout << "\t\tAdaption Field Length: " << AdaptationFieldLen() << "\n";
    if( AdaptationFieldLen() ) {
      std::cout << "\t\tRandom Access: " << RandomAccess() << "\n";
    }
    if( PCR() != -1 ) {
      std::cout << "\t\tPCR: " << PCR() << "( " << (double)PCR() / 27000000 << " s )\n";
    }
  }
}

/// Gets whether a new unit starts in this TS::Packet.
/// \return The start of a new unit.
int TS::Packet::UnitStart( ) {
  return ( Buffer[1] & 0x40) >> 6;
}

/// Sets the start of a new unit in this TS::Packet.
/// \param NewVal The new value for the start of a unit.
void TS::Packet::UnitStart( int NewVal ) {
  if( NewVal ) {
    Buffer[1] = (Buffer[1] | 0x40);
  } else {
    Buffer[1] = (Buffer[1] & 0xBF);
  }
}

/// Gets whether this TS::Packet can be accessed at random (indicates keyframe).
/// \return Whether or not this TS::Packet contains a keyframe.
int TS::Packet::RandomAccess( ) {
  if( AdaptationField() < 2 ) {
    return -1;
  }
  return ( Buffer[5] & 0x40) >> 6;
}

/// Sets whether this TS::Packet contains a keyframe
/// \param NewVal Whether or not this TS::Packet contains a keyframe.
void TS::Packet::RandomAccess( int NewVal ) {
  if( AdaptationField() ) {
	if( Buffer[4] == 0 ) {
      Buffer[4] = 1;
	}
    if( NewVal ) {
      Buffer[5] = (Buffer[5] | 0x40);
    } else {
      Buffer[5] = (Buffer[5] & 0xBF);
    }
  } else {
    AdaptationField( 3 );
    Buffer[4] = 1;
    if( NewVal ) {
      Buffer[5] = 0x40;
    } else {
      Buffer[5] = 0x00;
    }
    
  }
  Free = std::min( Free, 182 );
}

/// Transforms the TS::Packet into a standard Program Association Table
void TS::Packet::DefaultPAT( ) {
  static int MyCntr = 0;
  std::copy( TS::PAT, TS::PAT + 188, Buffer );
  ContinuityCounter( MyCntr );
  Free = 0;
  MyCntr = ( (MyCntr + 1) % 0x10);
}

/// Transforms the TS::Packet into a standard Program Mapping Table
void TS::Packet::DefaultPMT( ) {
  static int MyCntr = 0;
  std::copy( TS::PMT, TS::PMT + 188, Buffer );
  ContinuityCounter( MyCntr );
  Free = 0;
  MyCntr = ( (MyCntr + 1) % 0x10);
}

/// Generates a string from the contents of the TS::Packet
/// \return A string representation of the packet.
std::string TS::Packet::ToString( ) {
  std::string Result( Buffer, 188 );
  std::cout.write( Buffer,188 );
  return Result;
}


/// Generates a PES Lead-in for a video frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this video frame.
void TS::Packet::PESVideoLeadIn( int NewLen ) {
  static int PTS = 126000;
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
  Buffer[Offset+12] = 0x00 + ((PTS & 0x000007F80) >> 7 );//PTS (Cont)
  Buffer[Offset+13] = 0x00 + ((PTS & 0x00000007F) << 1) + 1;//PTS (Cont)
  
  //PesPacket-Wise Prepended Data
  
  Buffer[Offset+14] = 0x00;//NALU StartCode
  Buffer[Offset+15] = 0x00;//NALU StartCode (Cont)
  Buffer[Offset+16] = 0x00;//NALU StartCode (Cont)
  Buffer[Offset+17] = 0x01;//NALU StartCode (Cont)
  Buffer[Offset+18] = 0x09;//NALU EndOfPacket (Einde Vorige Packet)
  Buffer[Offset+19] = 0xF0;//NALU EndOfPacket (Cont)
  Free = Free - 20;
  PTS += 3003;
}

/// Generates a PES Lead-in for an audio frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this audio frame.
/// \param PTS The timestamp of the audio frame.
void TS::Packet::PESAudioLeadIn( int NewLen, uint64_t PTS ) {
  PTS = PTS * 90;
  NewLen += 8;
  int Offset = ( 188 - Free ) - 2;
  Buffer[Offset] = 0x00;//PacketStartCodePrefix
  Buffer[Offset+1] = 0x00;//PacketStartCodePrefix (Cont)
  Buffer[Offset+2] = 0x01;//PacketStartCodePrefix (Cont)
  Buffer[Offset+3] = 0xc0;//StreamType Video
  
  Buffer[Offset+4] = (NewLen & 0xFF00) >> 8;//PES PacketLength
  Buffer[Offset+5] = (NewLen & 0x00FF);//PES PacketLength (Cont)
  Buffer[Offset+6] = 0x80;//Reserved + Flags
  Buffer[Offset+7] = 0x80;//PTSOnlyFlag + Flags
  Buffer[Offset+8] = 0x05;//PESHeaderDataLength
  Buffer[Offset+9] = 0x20 + ((PTS & 0x1C0000000) >> 29 ) + 1;//PTS
  Buffer[Offset+10] = 0x00 + ((PTS & 0x03FC00000) >> 22 );//PTS (Cont)
  Buffer[Offset+11] = 0x00 + ((PTS & 0x0003F8000) >> 14 ) + 1;//PTS (Cont)
  Buffer[Offset+12] = 0x00 + ((PTS & 0x000007F80) >> 7 );//PTS (Cont)
  Buffer[Offset+13] = 0x00 + ((PTS & 0x00000007F) << 1) + 1;//PTS (Cont)
  Free = Free - 12;
}

/// Fills the free bytes of the TS::Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// \param NewVal The data to store in the packet.
void TS::Packet::FillFree( std::string & NewVal ) {
  int Offset = 188 - Free;
  std::copy( NewVal.begin(), NewVal.begin() + Free, Buffer + Offset );
  NewVal.erase(0,Free);
  Free = 0;
}

/// Adds NumBytes of stuffing to the TS::Packet.
/// \param NumBytes the amount of stuffing bytes.
void TS::Packet::AddStuffing( int NumBytes ) {
  if( NumBytes <= 0 ) { return; }
  if( AdaptationField( ) == 3 ) {
    int Offset = Buffer[4];
    Buffer[4] = Offset + NumBytes - 1;
    for( int i = 0; i < ( NumBytes -2 ); i ++ ) {
      Buffer[6+Offset+i] = 0xFF;
    }
    Free -= NumBytes;
  } else {
    AdaptationField( 3 );
    Buffer[4] = NumBytes - 1;
    Buffer[5] = 0x00;
    for( int i = 0; i < ( NumBytes -2 ); i ++ ) {
      Buffer[6+i] = 0xFF;
    }
    Free -= NumBytes;
  }
}


/// Transforms this TS::Packet into a standard Service Description Table
void TS::Packet::FFMpegHeader( ) {
  static int MyCntr = 0;
  std::copy( TS::SDT, TS::SDT + 188, Buffer );
  ContinuityCounter( MyCntr );
  Free = 0;
  MyCntr = ( (MyCntr + 1) % 0x10);
}

int TS::Packet::ProgramMapPID( ) {
  if( PID() != 0 ) { return -1; }
  int Offset = Buffer[4];
  int ProgramNumber = ( Buffer[13+Offset] << 8 ) + Buffer[14+Offset];
  if( ProgramNumber == 0 ) { return -1; }
  return ((Buffer[15+Offset] & 0x1F) << 8 ) + Buffer[16+Offset];
}

void TS::Packet::UpdateStreamPID( int & VideoPid, int & AudioPid ) {
  int Offset = Buffer[4] + 5;
  int SectionLength = ( ( Buffer[1+Offset] & 0x0F) << 8 ) + Buffer[2+Offset];
  int ProgramInfoLength = ( (Buffer[10+Offset] & 0x0F) << 8 ) + Buffer[11+Offset];
  int CurOffset = 12 + ProgramInfoLength;
  while( CurOffset < (SectionLength+3)-4 ) {
    if( Buffer[CurOffset] == 0x1B ) {
      VideoPid = ((Buffer[CurOffset+1] & 0x1F) << 8 ) + Buffer[CurOffset+2];
      fprintf( stderr, "Video stream recognized at PID %d\n",  ((Buffer[CurOffset+1] & 0x1F) << 8 ) + Buffer[CurOffset+2] );
    } else if( Buffer[CurOffset] == 0x0F ) {
      AudioPid = ((Buffer[CurOffset+1] & 0x1F) << 8 ) + Buffer[CurOffset+2];
      fprintf( stderr, "Audio stream recognized at PID %d\n",  ((Buffer[CurOffset+1] & 0x1F) << 8 ) + Buffer[CurOffset+2] );
    } else {
      fprintf( stderr, "Unsupported stream type: %0.2X\n", Buffer[CurOffset] );
    }
    int ESLen = (( Buffer[CurOffset+3] & 0x0F ) << 8 ) + Buffer[CurOffset+4];
    CurOffset += ( 5 + ESLen );
  }
}

int TS::Packet::PESTimeStamp( ) {
  if( !UnitStart( ) ) { return -1; }
  int PesOffset = 4;
  if( AdaptationField( ) >= 2 )  { PesOffset += 1 + AdaptationFieldLen( ); }
  fprintf( stderr, "PES Offset: %d\n", PesOffset );
  fprintf( stderr, "PES StartCode: %0.2X %0.2X %0.2X\n", Buffer[PesOffset], Buffer[PesOffset+1], Buffer[PesOffset+2] );
  int MyTimestamp = (Buffer[PesOffset+9] & 0x0F) >> 1;
  MyTimestamp = (MyTimestamp << 8) + Buffer[PesOffset+10];
  MyTimestamp = (MyTimestamp << 7) + ((Buffer[PesOffset+11]) >> 1);
  MyTimestamp = (MyTimestamp << 8) + Buffer[PesOffset+12];
  MyTimestamp = (MyTimestamp << 7) + ((Buffer[PesOffset+13]) >> 1);
  fprintf( stderr, "PES Timestamp: %d\n", MyTimestamp );
  return MyTimestamp;
}

int TS::Packet::GetDataOffset( ) {
  int Offset = 4;
  if( AdaptationField( ) >= 2 ) {
    Offset += 1 + AdaptationFieldLen( );
  }
  if( UnitStart() ) {
    Offset += 8;//Default Header + Flag Bytes
    Offset += 1 + Buffer[Offset];//HeaderLengthByte + HeaderLength
  }
  return Offset;
}

void TS::Packet::toDTSC( std::string Type, DTSC::DTMI & CurrentDTSC ) {
  if( !CurrentDTSC.getContentP( "datatype" ) ) {
    CurrentDTSC.addContent( DTSC::DTMI("datatype", Type ) );
  }
  if( Type == "video" ) {
    if ( (RandomAccess() > 0) ){
      if( !CurrentDTSC.getContentP( "keyframe" ) && !CurrentDTSC.getContentP( "interframe" ) ) {
        CurrentDTSC.addContent(DTSC::DTMI("keyframe", 1));
      }
    } else {
      if( !CurrentDTSC.getContentP( "keyframe" ) && !CurrentDTSC.getContentP( "interframe" ) ) {
        CurrentDTSC.addContent(DTSC::DTMI("interframe", 1));
      }
    }
  }
  if( Type == "audio" ) {
    if( ( RandomAccess() > 0 ) ) {
      if( !CurrentDTSC.getContentP( "keyframe" ) && !CurrentDTSC.getContentP( "interframe" ) ) {
        CurrentDTSC.addContent(DTSC::DTMI("keyframe", 1));
      }
    }
  }
  if( UnitStart() ) {
    if( !CurrentDTSC.getContentP( "time" ) ) {
      if( Type == "audio" ) {
        CurrentDTSC.addContent( DTSC::DTMI( "time", PESTimeStamp( ) / 81000 ) );
      } else {
        CurrentDTSC.addContent( DTSC::DTMI( "time", ( PESTimeStamp( ) / 27000 ) - 700 ) );
        //CurrentDTSC.addContent( DTSC::DTMI( "time", (PESTimeStamp( ) / 27000000) / 91 ) );
      }
    }
  }
  if( Type == "video" ) {
    if( !CurrentDTSC.getContentP( "nalu" ) ) {
      CurrentDTSC.addContent( DTSC::DTMI( "nalu", 1 ) );
    }
    if( !CurrentDTSC.getContentP( "offset" ) ) {
      CurrentDTSC.addContent( DTSC::DTMI( "offset", 0 ) );
    }
  }
  int DataOffset = GetDataOffset();
  std::string ToAppend = std::string((char*)Buffer+DataOffset, (size_t)188-DataOffset);
  std::string CurrentData;
  if( CurrentDTSC.getContentP( "data" ) ) {
    CurrentData = CurrentDTSC.getContent( "data" ).StrValue( );
  }
  CurrentDTSC.addContent(DTSC::DTMI("data", CurrentData + ToAppend ));
}
