/// \file ts_packet.cpp
/// Holds all code for the TS namespace.

#include <sstream>
#include "ts_packet.h"

#ifndef FILLER_DATA
#define FILLER_DATA "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent commodo vulputate urna eu commodo. Cras tempor velit nec nulla placerat volutpat. Proin eleifend blandit quam sit amet suscipit. Pellentesque vitae tristique lorem. Maecenas facilisis consequat neque, vitae iaculis eros vulputate ut. Suspendisse ut arcu non eros vestibulum pulvinar id sed erat. Nam dictum tellus vel tellus rhoncus ut mollis tellus fermentum. Fusce volutpat consectetur ante, in mollis nisi euismod vulputate. Curabitur vitae facilisis ligula. Sed sed gravida dolor. Integer eu eros a dolor lobortis ullamcorper. Mauris interdum elit non neque interdum dictum. Suspendisse imperdiet eros sed sapien cursus pulvinar. Vestibulum ut dolor lectus, id commodo elit. Cras convallis varius leo eu porta. Duis luctus sapien nec dui adipiscing quis interdum nunc congue. Morbi pharetra aliquet mauris vitae tristique. Etiam feugiat sapien quis augue elementum id ultricies magna vulputate. Phasellus luctus, leo id egestas consequat, eros tortor commodo neque, vitae hendrerit nunc sem ut odio."
#endif

/// This constructor creates an empty TS::Packet, ready for use for either reading or writing.
/// All this constructor does is call TS::Packet::Clear().
TS::Packet::Packet(){
  strBuf.reserve(188);
  Clear();
}

/// This function fills a TS::Packet from provided Data.
/// It fills the content with the first 188 bytes of Data.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
bool TS::Packet::FromString(std::string & Data){
  if (Data.size() < 188){
    return false;
  }else{
    strBuf = Data.substr(0, 188);
    Data.erase(0, 188);
  }
  return true;
}

/// The deconstructor deletes all space that may be occupied by a TS::Packet.
TS::Packet::~Packet(){
}

/// Sets the PID of a single TS::Packet.
/// \param NewPID The new PID of the packet.
void TS::Packet::PID(int NewPID){
  strBuf[1] = (strBuf[1] & 0xE0) + ((NewPID & 0x1F00) >> 8);
  strBuf[2] = (NewPID & 0x00FF);
}

/// Gets the PID of a single TS::Packet.
/// \return The value of the PID.
int TS::Packet::PID(){
  return ((strBuf[1] & 0x1F) << 8) + strBuf[2];
}

/// Sets the Continuity Counter of a single TS::Packet.
/// \param NewContinuity The new Continuity Counter of the packet.
void TS::Packet::ContinuityCounter(int NewContinuity){
  if (strBuf.size() < 4){
    strBuf.resize(4);
  }
  strBuf[3] = (strBuf[3] & 0xF0) + (NewContinuity & 0x0F);
}

/// Gets the Continuity Counter of a single TS::Packet.
/// \return The value of the Continuity Counter.
int TS::Packet::ContinuityCounter(){
  return (strBuf[3] & 0x0F);
}

/// Gets the amount of bytes that are not written yet in a TS::Packet.
/// \return The amount of bytes that can still be written to this packet.
int TS::Packet::BytesFree(){
  return 188 - strBuf.size();
}

/// Clears a TS::Packet.
void TS::Packet::Clear(){
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
void TS::Packet::AdaptationField(int NewSelector){
  strBuf[3] = (strBuf[3] & 0xCF) + ((NewSelector & 0x03) << 4);
  if (NewSelector & 0x02){
    strBuf[4] = 0x00;
  }else{
    strBuf.resize(4);
  }
}

/// Gets whether a TS::Packet contains an adaptationfield.
/// \return The existence of an adaptationfield.
/// - 0: No adaptationfield present.
/// - 1: Adaptationfield is present.
int TS::Packet::AdaptationField(){
  return ((strBuf[3] & 0x30) >> 4);
}

/// Sets the PCR (Program Clock Reference) of a TS::Packet.
/// \param NewVal The new PCR Value.
void TS::Packet::PCR(int64_t NewVal){
  if (strBuf.size() < 12){
    strBuf.resize(12);
  }
  AdaptationField(3);
  strBuf[4] = 0x07;
  strBuf[5] = (strBuf[5] | 0x10);
  int64_t TmpVal = NewVal / 300;
  strBuf[6] = (((TmpVal >> 1) >> 24) & 0xFF);
  strBuf[7] = (((TmpVal >> 1) >> 16) & 0xFF);
  strBuf[8] = (((TmpVal >> 1) >> 8) & 0xFF);
  strBuf[9] = ((TmpVal >> 1) & 0xFF);
  int Remainder = NewVal % 300;
  strBuf[10] = 0x7E + ((TmpVal & 0x01) << 7) + ((Remainder & 0x0100) >> 8);
  strBuf[11] = (Remainder & 0x00FF);
}

/// Gets the PCR (Program Clock Reference) of a TS::Packet.
/// \return The value of the PCR.
int64_t TS::Packet::PCR(){
  if ( !AdaptationField()){
    return -1;
  }
  if ( !(strBuf[5] & 0x10)){
    return -1;
  }
  int64_t Result = 0;
  Result = (((strBuf[6] << 24) + (strBuf[7] << 16) + (strBuf[8] << 8) + strBuf[9]) << 1) + (strBuf[10] & 0x80 >> 7);
  Result = Result * 300;
  Result += (((strBuf[10] & 0x01) << 8) + strBuf[11]);
  return Result;
}

/// Gets the current length of the adaptationfield.
/// \return The length of the adaptationfield.
int TS::Packet::AdaptationFieldLen(){
  if ( !AdaptationField()){
    return -1;
  }
  return (int)strBuf[4];
}

/// Prints a packet to stdout, for analyser purposes.
std::string TS::Packet::toPrettyString(size_t indent){
  std::stringstream output;
  output << std::string(indent,' ') << "TS Packet: " << (strBuf[0] == 0x47) << std::endl;
  output << std::string(indent+2,' ') << "NewUnit: " << UnitStart() << std::endl;
  output << std::string(indent+2,' ') << "PID: " << PID() << std::endl;
  output << std::string(indent+2,' ') << "Continuity Counter: " << ContinuityCounter() << std::endl;
  output << std::string(indent+2,' ') << "Adaption Field: " << AdaptationField() << std::endl;
  if (AdaptationField()){
    output << std::string(indent+4,' ') << "Adaptation Length: " << AdaptationFieldLen() << std::endl;;
    if (AdaptationFieldLen()){
      output << std::string(indent+4,' ') << "Random Access: " << RandomAccess() << std::endl;
    }
    if (PCR() != -1){
      output << std::string(indent+4,' ') << "PCR: " << PCR() << "( " << (double)PCR() / 27000000 << " s )" << std::endl;
    }
  }
  return output.str();
}

/// Gets whether a new unit starts in this TS::Packet.
/// \return The start of a new unit.
int TS::Packet::UnitStart(){
  return (strBuf[1] & 0x40) >> 6;
}

/// Sets the start of a new unit in this TS::Packet.
/// \param NewVal The new value for the start of a unit.
void TS::Packet::UnitStart(int NewVal){
  if (NewVal){
    strBuf[1] |= 0x40;
  }else{
    strBuf[1] &= 0xBF;
  }
}

/// Gets whether this TS::Packet can be accessed at random (indicates keyframe).
/// \return Whether or not this TS::Packet contains a keyframe.
int TS::Packet::RandomAccess(){
  if (AdaptationField() < 2){
    return -1;
  }
  return (strBuf[5] & 0x40) >> 6;
}

/// Sets whether this TS::Packet contains a keyframe
/// \param NewVal Whether or not this TS::Packet contains a keyframe.
void TS::Packet::RandomAccess(int NewVal){
  if (AdaptationField() == 3){
    if (strBuf.size() < 6){
      strBuf.resize(6);
    }
    if ( !strBuf[4]){
      strBuf[4] = 1;
    }
    if (NewVal){
      strBuf[5] |= 0x40;
    }else{
      strBuf[5] &= 0xBF;
    }
  }else{
    if (strBuf.size() < 6){
      strBuf.resize(6);
    }
    AdaptationField(3);
    strBuf[4] = 1;
    if (NewVal){
      strBuf[5] = 0x40;
    }else{
      strBuf[5] = 0x00;
    }
  }
}

/// Transforms the TS::Packet into a standard Program Association Table
void TS::Packet::DefaultPAT(){
  static int MyCntr = 0;
  strBuf = std::string(TS::PAT, 188);
  ContinuityCounter(MyCntr++);
  MyCntr %= 0x10;
}

/// Transforms the TS::Packet into a standard Program Mapping Table
void TS::Packet::DefaultPMT(){
  static int MyCntr = 0;
  strBuf = std::string(TS::PMT, 188);
  ContinuityCounter(MyCntr++);
  MyCntr %= 0x10;
}

/// Generates a string from the contents of the TS::Packet
/// \return A string representation of the packet.
const char* TS::Packet::ToString(){
  if (strBuf.size() != 188){
    std::cerr << "Error: Size invalid (" << strBuf.size() << ") Invalid data from this point on." << std::endl;
  }
  return strBuf.c_str();
}

/// Generates a PES Lead-in for a video frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this frame.
/// \param PTS The timestamp of the frame.
void TS::Packet::PESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS){
  //NewLen += 19;
  NewLen = 0;
  strBuf += (char)0x00; //PacketStartCodePrefix
  strBuf += (char)0x00; //PacketStartCodePrefix (Cont)
  strBuf += (char)0x01; //PacketStartCodePrefix (Cont)
  strBuf += (char)0xe0; //StreamType Video
  strBuf += (char)((NewLen & 0xFF00) >> 8); //PES PacketLength
  strBuf += (char)(NewLen & 0x00FF); //PES PacketLength (Cont)
  strBuf += (char)0x84; //Reserved + Flags
  strBuf += (char)0xC0; //PTSOnlyFlag + Flags
  strBuf += (char)0x0A; //PESHeaderDataLength
  strBuf += (char)(0x30 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + PTS 
  strBuf += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  strBuf += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  strBuf += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  strBuf += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
  strBuf += (char)(0x10 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + DTS 
  strBuf += (char)((PTS & 0x03FC00000LL) >> 22); //DTS (Cont)
  strBuf += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //DTS (Cont)
  strBuf += (char)((PTS & 0x000007F80LL) >> 7); //DTS (Cont)
  strBuf += (char)(((PTS & 0x00000007FLL) << 1) + 1); //DTS (Cont)
  //PesPacket-Wise Prepended Data
  strBuf += (char)0x00; //NALU StartCode
  strBuf += (char)0x00; //NALU StartCode (Cont)
  strBuf += (char)0x00; //NALU StartCode (Cont)
  strBuf += (char)0x01; //NALU StartCode (Cont)
  strBuf += (char)0x09; //NALU EndOfPacket (Einde Vorige Packet)
  strBuf += (char)0xF0; //NALU EndOfPacket (Cont)
}

/// Generates a PES Lead-in for an audio frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this frame.
/// \param PTS The timestamp of the frame.
void TS::Packet::PESAudioLeadIn(unsigned int NewLen, uint64_t PTS){
  NewLen += 5;
  strBuf += (char)0x00; //PacketStartCodePrefix
  strBuf += (char)0x00; //PacketStartCodePrefix (Cont)
  strBuf += (char)0x01; //PacketStartCodePrefix (Cont)
  strBuf += (char)0xc0; //StreamType Audio
  strBuf += (char)((NewLen & 0xFF00) >> 8); //PES PacketLength
  strBuf += (char)(NewLen & 0x00FF); //PES PacketLength (Cont)
  strBuf += (char)0x84; //Reserved + Flags
  strBuf += (char)0x80; //PTSOnlyFlag + Flags
  strBuf += (char)0x05; //PESHeaderDataLength
  strBuf += (char)(0x30 + ((PTS & 0x1C0000000LL) >> 29) + 1); //PTS
  strBuf += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  strBuf += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  strBuf += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  strBuf += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
}

/// Generates a PES Lead-in for a video frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param toSend Data that is to be send, will be modified.
/// \param PTS The timestamp of the frame.
void TS::Packet::PESVideoLeadIn(std::string & toSend, long long unsigned int PTS){
  std::string tmpStr;
  tmpStr.reserve(25);
  tmpStr.append("\000\000\001\340\000\000\204\300\012", 9);
  tmpStr += (char)(0x30 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + PTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
  tmpStr += (char)(0x10 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + DTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //DTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //DTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //DTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //DTS (Cont)
  tmpStr.append("\000\000\000\001\011\360", 6);
  toSend.insert(0, tmpStr);
}

/// Generates a PES Lead-in for an audio frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param toSend Data that is to be send, will be modified.
/// \param PTS The timestamp of the frame.
void TS::Packet::PESAudioLeadIn(std::string & toSend, long long unsigned int PTS){
  std::string tmpStr;
  tmpStr.reserve(14);
  unsigned int NewLen = toSend.size() + 5;
  tmpStr.append("\000\000\001\300", 4);
  tmpStr += (char)((NewLen & 0xFF00) >> 8); //PES PacketLength
  tmpStr += (char)(NewLen & 0x00FF); //PES PacketLength (Cont)
  tmpStr.append("\204\200\005", 3);
  tmpStr += (char)(0x30 + ((PTS & 0x1C0000000LL) >> 29) + 1); //PTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
  toSend.insert(0, tmpStr);
}

/// Generates a PES Lead-in for a video frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param NewLen The length of this frame.
/// \param PTS The timestamp of the frame.
std::string & TS::Packet::getPESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS){
  static std::string tmpStr;
  tmpStr.clear();
  tmpStr.reserve(25);
  tmpStr.append("\000\000\001\340\000\000\204\300\012", 9);
  tmpStr += (char)(0x30 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + PTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
  tmpStr += (char)(0x10 + ((PTS & 0x1C0000000LL) >> 29) + 1); //Fixed + DTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //DTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //DTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //DTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //DTS (Cont)
  tmpStr.append("\000\000\000\001\011\360", 6);
  return tmpStr;
}

/// Generates a PES Lead-in for an audio frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param NewLen The length of this frame.
/// \param PTS The timestamp of the frame.
std::string & TS::Packet::getPESAudioLeadIn(unsigned int NewLen, long long unsigned int PTS){
  static std::string tmpStr;
  tmpStr.clear();
  tmpStr.reserve(14);
  NewLen = NewLen + 8;
  tmpStr.append("\000\000\001\300", 4);
  tmpStr += (char)((NewLen & 0xFF00) >> 8); //PES PacketLength
  tmpStr += (char)(NewLen & 0x00FF); //PES PacketLength (Cont)
  tmpStr.append("\204\200\005", 3);
  tmpStr += (char)(0x20 + ((PTS & 0x1C0000000LL) >> 29) + 1); //PTS
  tmpStr += (char)((PTS & 0x03FC00000LL) >> 22); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x0003F8000LL) >> 14) + 1); //PTS (Cont)
  tmpStr += (char)((PTS & 0x000007F80LL) >> 7); //PTS (Cont)
  tmpStr += (char)(((PTS & 0x00000007FLL) << 1) + 1); //PTS (Cont)
  return tmpStr;
}

/// Fills the free bytes of the TS::Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// \param NewVal The data to store in the packet.
void TS::Packet::FillFree(std::string & NewVal){
  unsigned int toWrite = BytesFree();
  if (toWrite == NewVal.size()){
    strBuf += NewVal;
    NewVal.clear();
  }else{
    strBuf += NewVal.substr(0, toWrite);
    NewVal.erase(0, toWrite);
  }
}

/// Fills the free bytes of the TS::Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// The minimum of TS::Packet::BytesFree and maxLen is used.
/// \param NewVal The data to store in the packet.
/// \param maxLen The maximum amount of bytes to store.
int TS::Packet::FillFree(const char* NewVal, int maxLen){
  int toWrite = std::min((int)BytesFree(), maxLen);
  strBuf += std::string(NewVal, toWrite);
  return toWrite;
}

/// Adds stuffing to the TS::Packet depending on how much content you want to send.
/// \param NumBytes the amount of non-stuffing content bytes you want to send.
/// \return The amount of content bytes that can be send.
unsigned int TS::Packet::AddStuffing(int NumBytes){
  if (BytesFree() <= NumBytes){
    return BytesFree();
  }
  NumBytes = BytesFree() - NumBytes;
  if (AdaptationField() == 3){
    strBuf.resize(5 + strBuf[4]);
    strBuf[4] += NumBytes;
    for (int i = 0; i < NumBytes; i++){
      strBuf.append(FILLER_DATA + (i % sizeof(FILLER_DATA)), 1);
    }
  }else{
    AdaptationField(3);
    if (NumBytes > 1){
      strBuf.resize(6);
      strBuf[4] = (char)(NumBytes - 1);
      strBuf[5] = (char)0x00;
      for (int i = 0; i < (NumBytes - 2); i++){
        strBuf += FILLER_DATA[i % sizeof(FILLER_DATA)];
      }
    }else{
      strBuf.resize(5);
      strBuf[4] = (char)(NumBytes - 1);
    }
  }
  return BytesFree();
}
