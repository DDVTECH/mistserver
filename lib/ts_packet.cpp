/// \file ts_packet.cpp
/// Holds all code for the TS namespace.

#include "ts_packet.h"

/// This constructor creates an empty TS::Packet, ready for use for either reading or writing.
/// All this constructor does is call TS::Packet::Clear().
TS::Packet::Packet() { 
  strBuf.reserve( 188 );
  Clear( );
}

/// This function fills a TS::Packet from provided Data.
/// It fills the content with the first 188 bytes of Data.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
bool TS::Packet::FromString( std::string & Data ) {
  if( Data.size() < 188 ) {
    return false;  
  } else {
    strBuf = Data.substr(0,188);
    Data.erase(0,188);
  }
  return true;
}

/// The deconstructor deletes all space that may be occupied by a TS::Packet.
TS::Packet::~Packet() { }

/// Sets the PID of a single TS::Packet.
/// \param NewPID The new PID of the packet.
void TS::Packet::PID( int NewPID ) {
  strBuf[1] = (strBuf[1] & 0xE0) + ((NewPID & 0x1F00) >> 8 );
  strBuf[2] = (NewPID & 0x00FF);
}

/// Gets the PID of a single TS::Packet.
/// \return The value of the PID.
int TS::Packet::PID() {
  return (( strBuf[1] & 0x1F ) << 8 ) + strBuf[2];
}

/// Sets the Continuity Counter of a single TS::Packet.
/// \param NewContinuity The new Continuity Counter of the packet.
void TS::Packet::ContinuityCounter( int NewContinuity ) {
  strBuf[3] = ( strBuf[3] & 0xF0 ) + ( NewContinuity & 0x0F );
}

/// Gets the Continuity Counter of a single TS::Packet.
/// \return The value of the Continuity Counter.
int TS::Packet::ContinuityCounter() {
  return ( strBuf[3] & 0x0F );
}

/// Gets the amount of bytes that are not written yet in a TS::Packet.
/// \return The amount of bytes that can still be written to this packet.
int TS::Packet::BytesFree( ) {
  return 188 - strBuf.size();
}

/// Clears a TS::Packet.
void TS::Packet::Clear( ) {
  strBuf.resize(4);
  strBuf[0] = 0x47;
  strBuf[1] = 0x00;
  strBuf[2] = 0x00;
  strBuf[3] = 0x10;
}

/// Sets the selection value for an adaptationfield of a TS::Packet.
/// \param NewSelector The new value of the selection bits.
/// - 1: No AdaptationField.
/// - 2: AdaptationField Only.
/// - 3: AdaptationField followed by Data.
void TS::Packet::AdaptationField( int NewSelector ) {
  strBuf[3] = ( strBuf[3] & 0xCF ) + ((NewSelector & 0x03) << 4);
  if( NewSelector & 0x02 ) {
    strBuf[4] = 0x00;
  } else {
    strBuf.resize(4);
  }
}

/// Gets whether a TS::Packet contains an adaptationfield.
/// \return The existence of an adaptationfield.
/// - 0: No adaptationfield present.
/// - 1: Adaptationfield is present.
int TS::Packet::AdaptationField( ) {
  return ((strBuf[3] & 0x30) >> 4 );
}

/// Sets the PCR (Program Clock Reference) of a TS::Packet.
/// \param NewVal The new PCR Value.
void TS::Packet::PCR( int64_t NewVal ) {
  if( strBuf.size() < 12 ) {
    strBuf.resize( 12 );
  }
  AdaptationField( 3 );
  strBuf[4] = 0x07;
  strBuf[5] = (strBuf[5] | 0x10 );
  int64_t TmpVal = NewVal / 300;
  strBuf[6] = (((TmpVal>>1)>>24) & 0xFF);
  strBuf[7] = (((TmpVal>>1)>>16) & 0xFF);
  strBuf[8] = (((TmpVal>>1)>>8) & 0xFF);
  strBuf[9] = ((TmpVal>>1) & 0xFF);
  int Remainder = NewVal % 300;
  strBuf[10] = 0x7E + ((TmpVal & 0x01)<<7) + ((Remainder & 0x0100) >> 8 );
  strBuf[11] = (Remainder & 0x00FF);
}

/// Gets the PCR (Program Clock Reference) of a TS::Packet.
/// \return The value of the PCR.
int64_t TS::Packet::PCR( ) {
  if( !AdaptationField() ) {
    return -1;
  }
  if( !(strBuf[5] & 0x10 ) ) {
    return -1;
  }
  int64_t Result = 0;
  Result = (((strBuf[6] << 24) + (strBuf[7] << 16) + (strBuf[8] << 8) + strBuf[9]) << 1) + ( strBuf[10] & 0x80 >> 7 );
  Result = Result * 300;
  Result += ((strBuf[10] & 0x01) << 8 + strBuf[11]);
  return Result;
}

/// Gets the current length of the adaptationfield.
/// \return The length of the adaptationfield.
int TS::Packet::AdaptationFieldLen( ) {
  if( !AdaptationField() ) {
    return -1;
  }
  return (int)strBuf[4];
}

/// Prints a packet to stdout, for analyser purposes.
void TS::Packet::Print( ) {
  std::cout << "TS Packet: " << (strBuf[0] == 0x47)
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
  return ( strBuf[1] & 0x40) >> 6;
}

/// Sets the start of a new unit in this TS::Packet.
/// \param NewVal The new value for the start of a unit.
void TS::Packet::UnitStart( int NewVal ) {
  if( NewVal ) {
    strBuf[1] |= 0x40;
  } else {
    strBuf[1] &= 0xBF;
  }
}

/// Gets whether this TS::Packet can be accessed at random (indicates keyframe).
/// \return Whether or not this TS::Packet contains a keyframe.
int TS::Packet::RandomAccess( ) {
  if( AdaptationField() < 2 ) {
    return -1;
  }
  return ( strBuf[5] & 0x40) >> 6;
}

/// Sets whether this TS::Packet contains a keyframe
/// \param NewVal Whether or not this TS::Packet contains a keyframe.
void TS::Packet::RandomAccess( int NewVal ) {
  if( AdaptationField() == 3 ) {
    if( strBuf.size() < 6 ) {
      strBuf.resize(6);
    }
    if( !strBuf[4] ) {
        strBuf[4] = 1;
    }
    if( NewVal ) {
      strBuf[5] |= 0x40;
    } else {
      strBuf[5] &= 0xBF;
    }
  } else {
    if( strBuf.size() < 6 ) {
      strBuf.resize(6);
    }
    AdaptationField( 3 );
    strBuf[4] = 1;
    if( NewVal ) {
      strBuf[5] = 0x40;
    } else {
      strBuf[5] = 0x00;
    }
  }
}

/// Transforms the TS::Packet into a standard Program Association Table
void TS::Packet::DefaultPAT( ) {
  static int MyCntr = 0;
  strBuf = std::string( TS::PAT, 188 );
  ContinuityCounter( MyCntr );
  MyCntr = ( (MyCntr + 1) % 0x10);
}

/// Transforms the TS::Packet into a standard Program Mapping Table
void TS::Packet::DefaultPMT( ) {
  static int MyCntr = 0;
  strBuf = std::string( TS::PMT, 188 );
  ContinuityCounter( MyCntr );
  MyCntr = ( (MyCntr + 1) % 0x10);
}

/// Generates a string from the contents of the TS::Packet
/// \return A string representation of the packet.
const char* TS::Packet::ToString( ) {
  if( strBuf.size() != 188 ) {
    std::cerr << "Error: Size invalid (" << strBuf.size() << ") Invalid data from this point on." << std::endl;
  }
  return strBuf.c_str( );
}

/// Generates a PES Lead-in for a video frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this video frame.
void TS::Packet::PESVideoLeadIn( int NewLen, long long unsigned int PTS ) {
  NewLen += ( PTS == 1 ? 9 : 14 );
  strBuf += (char)0x00;//PacketStartCodePrefix
  strBuf += (char)0x00;//PacketStartCodePrefix (Cont)
  strBuf += (char)0x01;//PacketStartCodePrefix (Cont)
  strBuf += (char)0xe0;//StreamType Video
  strBuf += (char)((NewLen & 0xFF00) >> 8);//PES PacketLength
  strBuf += (char)(NewLen & 0x00FF);//PES PacketLength (Cont)
  strBuf += (char)0x80;//Reserved + Flags
  if( PTS != 1 ) {  
    strBuf += (char)0x80;//PTSOnlyFlag + Flags
    strBuf += (char)0x05;//PESHeaderDataLength
    strBuf += (char)(0x20 +  ((PTS & 0x1C0000000) >> 29 ) + 1);//PTS
    strBuf += (char)((PTS & 0x03FC00000) >> 22 );//PTS (Cont)
    strBuf += (char)(((PTS & 0x0003F8000) >> 14 ) + 1);//PTS (Cont)
    strBuf += (char)((PTS & 0x000007F80) >> 7 );//PTS (Cont)
    strBuf += (char)(((PTS & 0x00000007F) << 1) + 1);//PTS (Cont)
  } else {
    strBuf += (char)0x00;//PTSOnlyFlag + Flags
    strBuf += (char)0x00;//PESHeaderDataLength
  }
  //PesPacket-Wise Prepended Data
  
  strBuf += (char)0x00;//NALU StartCode
  strBuf += (char)0x00;//NALU StartCode (Cont)
  strBuf += (char)0x00;//NALU StartCode (Cont)
  strBuf += (char)0x01;//NALU StartCode (Cont)
  strBuf += (char)0x09;//NALU EndOfPacket (Einde Vorige Packet)
  strBuf += (char)0xF0;//NALU EndOfPacket (Cont)
}

/// Generates a PES Lead-in for an audio frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this audio frame.
/// \param PTS The timestamp of the audio frame.
void TS::Packet::PESAudioLeadIn( int NewLen, uint64_t PTS ) {
  NewLen += 8;
  strBuf += (char)0x00;//PacketStartCodePrefix
  strBuf += (char)0x00;//PacketStartCodePrefix (Cont)
  strBuf += (char)0x01;//PacketStartCodePrefix (Cont)
  strBuf += (char)0xc0;//StreamType Audio
  
  strBuf += (char)((NewLen & 0xFF00) >> 8);//PES PacketLength
  strBuf += (char)(NewLen & 0x00FF);//PES PacketLength (Cont)
  strBuf += (char)0x80;//Reserved + Flags
  strBuf += (char)0x80;//PTSOnlyFlag + Flags
  strBuf += (char)0x05;//PESHeaderDataLength
  strBuf += (char)(0x20 + ((PTS & 0x1C0000000) >> 29 ) + 1);//PTS
  strBuf += (char)((PTS & 0x03FC00000) >> 22 );//PTS (Cont)
  strBuf += (char)(((PTS & 0x0003F8000) >> 14 ) + 1);//PTS (Cont)
  strBuf += (char)((PTS & 0x000007F80) >> 7 );//PTS (Cont)
  strBuf += (char)(((PTS & 0x00000007F) << 1) + 1);//PTS (Cont)
}

/// Fills the free bytes of the TS::Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// \param NewVal The data to store in the packet.
void TS::Packet::FillFree( std::string & NewVal ) {
  int toWrite = 188-strBuf.size();
  strBuf += NewVal.substr(0,toWrite);
  NewVal.erase(0,toWrite);
}

/// Adds NumBytes of stuffing to the TS::Packet.
/// \param NumBytes the amount of stuffing bytes.
void TS::Packet::AddStuffing( int NumBytes ) {
  if( NumBytes <= 0 ) { return; }
  if( AdaptationField( ) == 3 ) {
    int Offset = strBuf[4];
    strBuf[4] = Offset + NumBytes - 1;
    strBuf.resize(7+Offset+NumBytes-2);
    for( int i = 0; i < ( NumBytes -2 ); i ++ ) {
      strBuf[6+Offset+i] = 0xFF;
    }
  } else {
    AdaptationField( 3 );
    strBuf.resize(6);
    strBuf[4] = (char)(NumBytes - 1);
    strBuf[5] = (char)0x00;
    for( int i = 0; i < ( NumBytes - 2 ); i ++ ) {
      strBuf += (char)0xFF;
    }
  }
}
