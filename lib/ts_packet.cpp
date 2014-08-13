/// \file ts_packet.cpp
/// Holds all code for the TS namespace.

#include <sstream>
#include <iomanip>
#include <string.h>
#include "ts_packet.h"
#include "defines.h"

#ifndef FILLER_DATA
#define FILLER_DATA "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent commodo vulputate urna eu commodo. Cras tempor velit nec nulla placerat volutpat. Proin eleifend blandit quam sit amet suscipit. Pellentesque vitae tristique lorem. Maecenas facilisis consequat neque, vitae iaculis eros vulputate ut. Suspendisse ut arcu non eros vestibulum pulvinar id sed erat. Nam dictum tellus vel tellus rhoncus ut mollis tellus fermentum. Fusce volutpat consectetur ante, in mollis nisi euismod vulputate. Curabitur vitae facilisis ligula. Sed sed gravida dolor. Integer eu eros a dolor lobortis ullamcorper. Mauris interdum elit non neque interdum dictum. Suspendisse imperdiet eros sed sapien cursus pulvinar. Vestibulum ut dolor lectus, id commodo elit. Cras convallis varius leo eu porta. Duis luctus sapien nec dui adipiscing quis interdum nunc congue. Morbi pharetra aliquet mauris vitae tristique. Etiam feugiat sapien quis augue elementum id ultricies magna vulputate. Phasellus luctus, leo id egestas consequat, eros tortor commodo neque, vitae hendrerit nunc sem ut odio."
#endif

namespace TS {
/// This constructor creates an empty Packet, ready for use for either reading or writing.
/// All this constructor does is call Packet::Clear().
  Packet::Packet() {
    strBuf.reserve(188);
    Clear();
  }

/// This function fills a Packet from provided Data.
/// It fills the content with the first 188 bytes of Data.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
  bool Packet::FromString(std::string & Data) {
    if (Data.size() < 188) {
      return false;
    } else {
      strBuf = Data.substr(0, 188);
      Data.erase(0, 188);
    }
    return true;
  }

/// This function fills a Packet from a file.
/// It fills the content with the next 188 bytes int he file.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
  bool Packet::FromFile(FILE * data) {
    strBuf.resize(188);
    if (!fread((void *)strBuf.data(), 188, 1, data)) {
      return false;
    }
    return true;
  }

///This funtion fills a Packet from
///a char array. It fills the content with
///the first 188 characters of a char array
///\param Data The char array that contains the data to be read into the packet
///\return true if successful (which always happens, or else a segmentation fault should occur)
  bool Packet::FromPointer(char * Data) {
    strBuf = std::string(Data, 188);
    return true;
  }



/// The deconstructor deletes all space that may be occupied by a Packet.
  Packet::~Packet() {
  }

/// Sets the PID of a single Packet.
/// \param NewPID The new PID of the packet.
  void Packet::PID(int NewPID) {
    strBuf[1] = (strBuf[1] & 0xE0) + ((NewPID & 0x1F00) >> 8);
    strBuf[2] = (NewPID & 0x00FF);
  }

/// Gets the PID of a single Packet.
/// \return The value of the PID.
  unsigned int Packet::PID() {
    return (unsigned int)(((strBuf[1] & 0x1F) << 8) + strBuf[2]);
  }

/// Sets the Continuity Counter of a single Packet.
/// \param NewContinuity The new Continuity Counter of the packet.
  void Packet::ContinuityCounter(int NewContinuity) {
    if (strBuf.size() < 4) {
      strBuf.resize(4);
    }
    strBuf[3] = (strBuf[3] & 0xF0) + (NewContinuity & 0x0F);
  }

/// Gets the Continuity Counter of a single Packet.
/// \return The value of the Continuity Counter.
  int Packet::ContinuityCounter() {
    return (strBuf[3] & 0x0F);
  }

/// Gets the amount of bytes that are not written yet in a Packet.
/// \return The amount of bytes that can still be written to this packet.
  int Packet::BytesFree() {
    return 188 - strBuf.size();
  }

/// Clears a Packet.
  void Packet::Clear() {
    strBuf.resize(4);
    strBuf[0] = 0x47;
    strBuf[1] = 0x00;
    strBuf[2] = 0x00;
    strBuf[3] = 0x10;
  }

/// Sets the selection value for an adaptationfield of a Packet.
/// \param NewSelector The new value of the selection bits.
/// - 1: No AdaptationField.
/// - 2: AdaptationField Only.
/// - 3: AdaptationField followed by Data.
  void Packet::AdaptationField(int NewSelector) {
    strBuf[3] = (strBuf[3] & 0xCF) + ((NewSelector & 0x03) << 4);
    if (NewSelector & 0x02) {
      strBuf[4] = 0x00;
    } else {
      strBuf.resize(4);
    }
  }

/// Gets whether a Packet contains an adaptationfield.
/// \return The existence of an adaptationfield.
/// - 0: No adaptationfield present.
/// - 1: Adaptationfield is present.
  int Packet::AdaptationField() {
    return ((strBuf[3] & 0x30) >> 4);
  }

/// Sets the PCR (Program Clock Reference) of a Packet.
/// \param NewVal The new PCR Value.
  void Packet::PCR(int64_t NewVal) {
    if (strBuf.size() < 12) {
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

/// Gets the PCR (Program Clock Reference) of a Packet.
/// \return The value of the PCR.
  int64_t Packet::PCR() {
    if (!AdaptationField()) {
      return -1;
    }
    if (!(strBuf[5] & 0x10)) {
      return -1;
    }
    int64_t Result = 0;
    Result = (((strBuf[6] << 24) + (strBuf[7] << 16) + (strBuf[8] << 8) + strBuf[9]) << 1) + (strBuf[10] & 0x80 >> 7);
    Result = Result * 300;
    Result += (((strBuf[10] & 0x01) << 8) + strBuf[11]);
    return Result;
  }

/// Gets the OPCR (Original Program Clock Reference) of a Packet.
/// \return The value of the OPCR.
  int64_t Packet::OPCR() {
    if (!AdaptationField()) {
      return -1;
    }
    if (!(strBuf[5 + 6] & 0x10)) {
      return -1;
    }
    int64_t Result = 0;
    Result = (((strBuf[6 + 6] << 24) + (strBuf[7 + 6] << 16) + (strBuf[8 + 6] << 8) + strBuf[9 + 6]) << 1) + (strBuf[10 + 6] & 0x80 >> 7);
    Result = Result * 300;
    Result += (((strBuf[10 + 6] & 0x01) << 8) + strBuf[11 + 6]);
    return Result;
  }
/// Gets the sync byte of a Packet.
/// \return The packet's sync byte
  unsigned int Packet::getSyncByte() {
    return (unsigned int)strBuf[0];
  }

/// Gets the transport error inficator of a Packet
/// \return  The transport error inficator of a Packet
  unsigned int Packet::getTransportErrorIndicator() {
    return (unsigned int)((strBuf[1] >> 7) & (0x01));
  }

/// Gets the payload unit start inficator of a Packet
/// \return  The payload unit start inficator of a Packet
  unsigned int Packet::getPayloadUnitStartIndicator() {
    return (unsigned int)((strBuf[1] >> 6) & (0x01));
  }

/// Gets the transport priority of a Packet
/// \return  The transport priority of a Packet
  unsigned int Packet::getTransportPriority() {
    return (unsigned int)((strBuf[1] >> 5) & (0x01));
  }

/// Gets the transport scrambling control of a Packet
/// \return  The transport scrambling control of a Packet
  unsigned int Packet::getTransportScramblingControl() {
    return (unsigned int)((strBuf[3] >> 6) & (0x03));
  }

/// Gets the current length of the adaptationfield.
/// \return The length of the adaptationfield.
  int Packet::AdaptationFieldLen() {
    if (!AdaptationField()) {
      return -1;
    }
    return (int)strBuf[4];
  }

/// Prints a packet to stdout, for analyser purposes.
  std::string Packet::toPrettyString(size_t indent) {
    std::stringstream output;
    output << std::string(indent, ' ') << "TS Packet: " << (strBuf[0] == 0x47) << std::endl;
    output << std::string(indent + 2, ' ') << "Transport Error Indicator: " << getTransportErrorIndicator() << std::endl;
    output << std::string(indent + 2, ' ') << "Payload Unit Start Indicator: " << getPayloadUnitStartIndicator() << std::endl;
    output << std::string(indent + 2, ' ') << "Transport Priority: " << getTransportPriority() << std::endl;
    output << std::string(indent + 2, ' ') << "PID: " << PID() << std::endl;
    output << std::string(indent + 2, ' ') << "Transport Scrambling Control: " << getTransportScramblingControl() << std::endl;
    output << std::string(indent + 2, ' ') << "Adaptation Field: " << AdaptationField() << std::endl;
    output << std::string(indent + 2, ' ') << "Continuity Counter: " << ContinuityCounter() << std::endl;
    if (AdaptationField() > 1) {
      output << std::string(indent + 4, ' ') << "Adaptation Length: " << AdaptationFieldLen() << std::endl;
      if (AdaptationFieldLen()) {
        output << std::string(indent + 4, ' ') << "Discontinuity Indicator: " << DiscontinuityIndicator() << std::endl;
        output << std::string(indent + 4, ' ') << "Random Access: " << RandomAccess() << std::endl;
        output << std::string(indent + 4, ' ') << "Elementary Stream Priority Indicator: " << elementaryStreamPriorityIndicator() << std::endl;
        output << std::string(indent + 4, ' ') << "PCRFlag: " << PCRFlag() << std::endl;
        output << std::string(indent + 4, ' ') << "OPCRFlag: " << OPCRFlag() << std::endl;
        output << std::string(indent + 4, ' ') << "Splicing Point Flag: " << splicingPointFlag() << std::endl;
        ///\todo Implement this
        //output << std::string(indent + 4, ' ') << "Transport Private Data Flag: " << transportPrivateDataFlag() << std::endl;
        //output << std::string(indent + 4, ' ') << "Adaptation Field Extension Flag: " << adaptationFieldExtensionFlag() << std::endl;
        if (PCRFlag()) {
          output << std::string(indent + 6, ' ') << "PCR: " << PCR() << "( " << (double)PCR() / 27000000 << " s )" << std::endl;
        }
        if (OPCRFlag()) {
          output << std::string(indent + 6, ' ') << "OPCR: " << PCR() << "( " << (double)OPCR() / 27000000 << " s )" << std::endl;
        }
      }
    }
    if (PID() == 0x00) {
      //program association table
      output << ((ProgramAssociationTable *)this)->toPrettyString(indent + 2);
    } else {
      output << std::string(indent + 2, ' ') << "Data: " << 184 - ((AdaptationField() > 1) ? AdaptationFieldLen() + 1 : 0) << " bytes" << std::endl;
    }
    return output.str();
  }

/// Gets whether a new unit starts in this Packet.
/// \return The start of a new unit.
  int Packet::UnitStart() {
    return (strBuf[1] & 0x40) >> 6;
  }

/// Sets the start of a new unit in this Packet.
/// \param NewVal The new value for the start of a unit.
  void Packet::UnitStart(int NewVal) {
    if (NewVal) {
      strBuf[1] |= 0x40;
    } else {
      strBuf[1] &= 0xBF;
    }
  }

/// Gets the elementary stream priority indicator of a Packet
/// \return  The elementary stream priority indicator of a Packet
  int Packet::elementaryStreamPriorityIndicator() {
    return (strBuf[5] >> 5) & 0x01;
  }



/// Gets whether this Packet can be accessed at random (indicates keyframe).
/// \return Whether or not this Packet contains a keyframe.
  int Packet::DiscontinuityIndicator() {
    return (strBuf[5] >> 7) & 0x01;
  }

  int Packet::RandomAccess() {
    if (AdaptationField() < 2) {
      return -1;
    }
    return (strBuf[5] & 0x40) >> 6;
  }

///Gets the value of the PCR flag
///\return true if there is a PCR, false otherwise
  int Packet::PCRFlag() {
    return (strBuf[5] >> 4) & 0x01;
  }

///Gets the value of the OPCR flag
///\return true if there is an OPCR, false otherwise
  int Packet::OPCRFlag() {
    return (strBuf[5] >> 3) & 0x01;
  }

///Gets the value of the splicing point  flag
///\return the value of the splicing point flag
  int Packet::splicingPointFlag() {
    return (strBuf[5] >> 2) & 0x01;
  }

///Gets the value of the transport private data point flag
///\return the value of the transport private data point flag
  void Packet::RandomAccess(int NewVal) {
    if (AdaptationField() == 3) {
      if (strBuf.size() < 6) {
        strBuf.resize(6);
      }
      if (!strBuf[4]) {
        strBuf[4] = 1;
      }
      if (NewVal) {
        strBuf[5] |= 0x40;
      } else {
        strBuf[5] &= 0xBF;
      }
    } else {
      if (strBuf.size() < 6) {
        strBuf.resize(6);
      }
      AdaptationField(3);
      strBuf[4] = 1;
      if (NewVal) {
        strBuf[5] = 0x40;
      } else {
        strBuf[5] = 0x00;
      }
    }
  }

/// Transforms the Packet into a standard Program Association Table
  void Packet::DefaultPAT() {
    static int MyCntr = 0;
    strBuf = std::string(PAT, 188);
    ContinuityCounter(MyCntr++);
    MyCntr %= 0x10;
  }

/// Transforms the Packet into a standard Program Mapping Table
  void Packet::DefaultPMT() {
    static int MyCntr = 0;
    strBuf = std::string(PMT, 188);
    ContinuityCounter(MyCntr++);
    MyCntr %= 0x10;
  }

/// Generates a string from the contents of the Packet
/// \return A string representation of the packet.
  const char * Packet::ToString() {
    if (strBuf.size() != 188) {
      DEBUG_MSG(DLVL_ERROR, "Size invalid (%i) - invalid data from this point on", (int)strBuf.size());
    }
    return strBuf.c_str();
  }

/// Generates a PES Lead-in for a video frame.
/// Starts at the first Free byte.
/// \param NewLen The length of this frame.
/// \param PTS The timestamp of the frame.
  void Packet::PESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS) {
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
  void Packet::PESAudioLeadIn(unsigned int NewLen, uint64_t PTS) {
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
  void Packet::PESVideoLeadIn(std::string & toSend, long long unsigned int PTS) {
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
  void Packet::PESAudioLeadIn(std::string & toSend, long long unsigned int PTS) {
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
  std::string & Packet::getPESVideoLeadIn(unsigned int NewLen, long long unsigned int PTS) {
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
  std::string & Packet::getPESAudioLeadIn(unsigned int NewLen, long long unsigned int PTS) {
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

/// Fills the free bytes of the Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// \param NewVal The data to store in the packet.
  void Packet::FillFree(std::string & NewVal) {
    unsigned int toWrite = BytesFree();
    if (toWrite == NewVal.size()) {
      strBuf += NewVal;
      NewVal.clear();
    } else {
      strBuf += NewVal.substr(0, toWrite);
      NewVal.erase(0, toWrite);
    }
  }

/// Fills the free bytes of the Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// The minimum of Packet::BytesFree and maxLen is used.
/// \param NewVal The data to store in the packet.
/// \param maxLen The maximum amount of bytes to store.
  int Packet::FillFree(const char * NewVal, int maxLen) {
    int toWrite = std::min((int)BytesFree(), maxLen);
    strBuf += std::string(NewVal, toWrite);
    return toWrite;
  }

/// Adds stuffing to the Packet depending on how much content you want to send.
/// \param NumBytes the amount of non-stuffing content bytes you want to send.
/// \return The amount of content bytes that can be send.
  unsigned int Packet::AddStuffing(int NumBytes) {
    if (BytesFree() <= NumBytes) {
      return BytesFree();
    }
    NumBytes = BytesFree() - NumBytes;
    if (AdaptationField() == 3) {
      strBuf.resize(5 + strBuf[4]);
      strBuf[4] += NumBytes;
      for (int i = 0; i < NumBytes; i++) {
        strBuf.append(FILLER_DATA[i % sizeof(FILLER_DATA)], 0);
      }
    } else {
      AdaptationField(3);
      if (NumBytes > 1) {
        strBuf.resize(6);
        strBuf[4] = (char)(NumBytes - 1);
        strBuf[5] = (char)0x00;
        for (int i = 0; i < (NumBytes - 2); i++) {
          strBuf += FILLER_DATA[i % sizeof(FILLER_DATA)];
        }
      } else {
        strBuf.resize(5);
        strBuf[4] = (char)(NumBytes - 1);
      }
    }
    return BytesFree();
  }

///Gets the string buffer, containing the raw packet data as a string
///\return The raw TS data as a string
  std::string Packet::getStrBuf() {
    return strBuf;
  }

///Gets the buffer, containing the raw packet data as a char arrya
///\return The raw TS data as char array.
  const char * Packet::getBuffer() {
    return strBuf.data();
  }

///Gets the payload of this packet, as a raw char array
///\return The payload of this ts packet as a char pointer
  const char * Packet::getPayload() {
    return strBuf.data() + (4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0));
  }

  ///Gets the length of the payload for this apcket
  ///\return The amount of bytes payload in this packet
  int Packet::getPayloadLength() {
    return 184 - ((AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0));
  }


  ///Retrieves the current offset value for a PAT
  char ProgramAssociationTable::getOffset() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0);
    return strBuf[loc];
  }

  ///Retrieves the ID of this table
  char ProgramAssociationTable::getTableId() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    return strBuf[loc];
  }

  ///Retrieves the current section length
  short ProgramAssociationTable::getSectionLength() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    return (short)(strBuf[loc] & 0x0F) << 8 | strBuf[loc + 1];
  }

  ///Retrieves the Transport Stream ID
  short ProgramAssociationTable::getTransportStreamId() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    return (short)(strBuf[loc] & 0x0F) << 8 | strBuf[loc + 1];
  }

  ///Retrieves the version number
  char ProgramAssociationTable::getVersionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x1F;
  }

  ///Retrieves the "current/next" indicator
  bool ProgramAssociationTable::getCurrentNextIndicator() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x01;
  }

  ///Retrieves the section number
  char ProgramAssociationTable::getSectionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    return strBuf[loc];
  }

  ///Retrieves the last section number
  char ProgramAssociationTable::getLastSectionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    return strBuf[loc];
  }

  ///Returns the amount of programs in this table
  short ProgramAssociationTable::getProgramCount() {
    //This is correct, not -12 since we already parsed 4 bytes here
    return (getSectionLength() - 8) / 4;
  }

  short ProgramAssociationTable::getProgramNumber(short index) {
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return ((short)(strBuf[loc + (index * 4)]) << 8) | strBuf[loc + (index * 4) + 1];
  }

  short ProgramAssociationTable::getProgramPID(short index) {
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return (((short)(strBuf[loc + (index * 4) + 2]) << 8) | strBuf[loc + (index * 4) + 3]) & 0x1FFF;
  }

  int ProgramAssociationTable::getCRC() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 9 + (getProgramCount() * 4);
    return ((int)(strBuf[loc]) << 24) | ((int)(strBuf[loc + 1]) << 16) | ((int)(strBuf[loc + 2]) << 8) | strBuf[loc + 3];
  }

///This function prints a program association table,
///prints all values in a human readable format
///\param indent The indentation of the string printed as wanted by the user
///\return The string with human readable data from a PAT
  std::string ProgramAssociationTable::toPrettyString(size_t indent) {
    std::stringstream output;
    output << std::string(indent, ' ') << "[Program Association Table]" << std::endl;
    output << std::string(indent + 2, ' ') << "Pointer Field: " << (int)getOffset() << std::endl;
    output << std::string(indent + 2, ' ') << "Table ID: " << (int)getTableId() << std::endl;
    output << std::string(indent + 2, ' ') << "Sectionlen: " << getSectionLength() << std::endl;
    output << std::string(indent + 2, ' ') << "Transport Stream ID: " << getTransportStreamId() << std::endl;
    output << std::string(indent + 2, ' ') << "Version Number: " << (int)getVersionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Current/Next Indicator: " << (int)getCurrentNextIndicator() << std::endl;
    output << std::string(indent + 2, ' ') << "Section number: " << (int)getSectionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Last Section number: " << (int)getLastSectionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Programs [" << (int)getProgramCount() << "]" << std::endl;
    for (int i = 0; i < getProgramCount(); i++) {
      output << std::string(indent + 4, ' ') << "[" << i + 1 << "] ";
      output << "Program Number: " << getProgramNumber(i) << ", ";
      output << (getProgramNumber(i) == 0 ? "Network" : "Program Map") << " PID: " << getProgramPID(i);
      output << std::endl;
    }
    output << std::string(indent + 2, ' ') << "CRC32: " << std::hex << std::setw(8) << std::setfill('0') << std::uppercase << getCRC() << std::dec << std::endl;
    return output.str();

  }


  char ProgramMappingTable::getOffset() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0);
    return strBuf[loc];
  }

  void ProgramMappingTable::setOffset(char newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0);
    strBuf[loc] = newVal;
  }  

  char ProgramMappingTable::getTableId() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    return strBuf[loc];
  }

  void ProgramMappingTable::setTableId(char newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    if (strBuf.size() < loc + 1) {
      strBuf.resize(loc + 1);
    }
    strBuf[loc] = newVal;
  }

  short ProgramMappingTable::getSectionLength() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    return (((short)strBuf[loc] & 0x0F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setSectionLength(short newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    if (strBuf.size() < loc + 2) {
      strBuf.resize(loc + 2);
    }
    strBuf[loc] = (char)(newVal >> 8);
    strBuf[loc+1] = (char)newVal;
  }

  short ProgramMappingTable::getProgramNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    return (((short)strBuf[loc]) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setProgramNumber(short newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    if (strBuf.size() < loc + 2) {
      strBuf.resize(loc + 2);
    }
    strBuf[loc] = (char)(newVal >> 8);
    strBuf[loc+1] = (char)newVal;
  }

  char ProgramMappingTable::getVersionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x1F;
  }

  void ProgramMappingTable::setVersionNumber(char newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    if (strBuf.size() < loc + 1) {
      strBuf.resize(loc + 1);
    }
    strBuf[loc] = ((newVal & 0x1F) << 1) | 0xC1;
  }

  ///Retrieves the "current/next" indicator
  bool ProgramMappingTable::getCurrentNextIndicator() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x01;
  }

  ///Sets the "current/next" indicator
  void ProgramMappingTable::setCurrentNextIndicator(bool newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    if (strBuf.size() < loc + 1) {
      strBuf.resize(loc + 1);
    }
    strBuf[loc] = (((char)newVal) << 1) | 0xFD;
  }

  ///Retrieves the section number
  char ProgramMappingTable::getSectionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    return strBuf[loc];
  }

  ///Sets the section number
  void ProgramMappingTable::setSectionNumber(char newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    if (strBuf.size() < loc + 1) {
      strBuf.resize(loc + 1);
    }
    strBuf[loc] = newVal;
  }

  ///Retrieves the last section number
  char ProgramMappingTable::getLastSectionNumber() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    return strBuf[loc];
  }

  ///Sets the last section number
  void ProgramMappingTable::setLastSectionNumber(char newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    if (strBuf.size() < loc + 1) {
      strBuf.resize(loc + 1);
    }
    strBuf[loc] = newVal;
  }

  short ProgramMappingTable::getPCRPID() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return (((short)strBuf[loc] & 0x1F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setPCRPID(short newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    if (strBuf.size() < loc + 2) {
      strBuf.resize(loc + 2);
    }
    strBuf[loc] = (char)((newVal >> 8) & 0x1F) | 0xE0;
    strBuf[loc+1] = (char)newVal;
  }

  short ProgramMappingTable::getProgramInfoLength() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 11;
    return (((short)strBuf[loc] & 0x0F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setProgramInfoLength(short newVal) {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 11;
    if (strBuf.size() < loc + 2) {
      strBuf.resize(loc + 2);
    }
    strBuf[loc] = (char)((newVal >> 8) & 0x0F) | 0xF0;
    strBuf[loc+1] = (char)newVal;
  }

  short ProgramMappingTable::getProgramCount() {
    return (getSectionLength() - 13) / 5;
  }
  
  void ProgramMappingTable::setProgramCount(short newVal) {
    setSectionLength(newVal * 5 + 13);
  }

  char ProgramMappingTable::getStreamType(short index) {
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    return strBuf[loc + (index * 5)];
  }

  void ProgramMappingTable::setStreamType(char newVal, short index) {
    if (index > getProgramCount()) {
      return;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    if (strBuf.size() < loc + (index*5) + 1) {
      strBuf.resize(loc + (index*5) + 1);
    }
    strBuf[loc + (index * 5)] = newVal;
  }

  short ProgramMappingTable::getElementaryPID(short index) {
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    return (((short)strBuf[loc + (index * 5) + 1] & 0x1F) << 8) | strBuf[loc + (index * 5) + 2];
  }

  void ProgramMappingTable::setElementaryPID(short newVal, short index) {
    if (index > getProgramCount()) {
      return;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    if (strBuf.size() < loc + (index*5) + 3) {
      strBuf.resize(loc + (index*5) + 3);
    }
    strBuf[loc + (index * 5)+1] = ((newVal >> 8) & 0x1F )| 0xE0;
    strBuf[loc + (index * 5)+2] = (char)newVal;
  }

  short ProgramMappingTable::getESInfoLength(short index) {
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    return (((short)strBuf[loc + (index * 5) + 3] & 0x0F) << 8) | strBuf[loc + (index * 5) + 4];
  }

  void ProgramMappingTable::setESInfoLength(short newVal, short index) {
    if (index > getProgramCount()) {
      return;
    }
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength();
    //return (((short)strBuf[loc + (index * 5) + 3] & 0x0F) << 8) | strBuf[loc + (index * 5) + 4];
    if (strBuf.size() < loc + (index*5) + 5) {
      strBuf.resize(loc + (index*5) + 5);
    }
    strBuf[loc + (index * 5)+3] = ((newVal >> 8) & 0x0F) | 0xF0;
    strBuf[loc + (index * 5)+4] = (char)newVal;
  }

  int ProgramMappingTable::getCRC() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength() + (getProgramCount() * 5);
    return ((int)(strBuf[loc]) << 24) | ((int)(strBuf[loc + 1]) << 16) | ((int)(strBuf[loc + 2]) << 8) | strBuf[loc + 3];
  }

  void ProgramMappingTable::calcCRC() {
    unsigned int loc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 13 + getProgramInfoLength() + (getProgramCount() * 5);
    //return ((int)(strBuf[loc]) << 24) | ((int)(strBuf[loc + 1]) << 16) | ((int)(strBuf[loc + 2]) << 8) | strBuf[loc + 3];
    unsigned int newVal;//this will hold the CRC32 value;
    unsigned int tidLoc = 4 + (AdaptationField() > 1 ? AdaptationFieldLen() + 1 : 0) + getOffset() + 1;//location of table ID
    newVal = checksum::crc32(0, strBuf.c_str() + tidLoc, loc - tidLoc);//calculating checksum over all the fields from table ID to the last stream element
    if (strBuf.size() < 188) {
      strBuf.resize(188);
    }
    strBuf[loc] = newVal >> 24;
    strBuf[loc + 1] = (newVal >> 16) & 0xFF;
    strBuf[loc + 2] = (newVal >> 8) & 0xFF;
    strBuf[loc + 3] = newVal & 0xFF;
    memset((void*)(strBuf.c_str() + loc + 4), 0xFF, 180 - loc);
  }

///Print all PMT values in a human readable format
///\param indent The indentation of the string printed as wanted by the user
///\return The string with human readable data from a PMT table
  std::string ProgramMappingTable::toPrettyString(size_t indent) {
    std::stringstream output;
    output << std::string(indent, ' ') << "[Program Mapping Table]" << std::endl;
    output << std::string(indent + 2, ' ') << "Pointer Field: " << (int)getOffset() << std::endl;
    output << std::string(indent + 2, ' ') << "Table ID: " << (int)getTableId() << std::endl;
    output << std::string(indent + 2, ' ') << "Section Length: " << getSectionLength() << std::endl;
    output << std::string(indent + 2, ' ') << "Program number: " << getProgramNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Version number: " << (int)getVersionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Current next indicator: " << (int)getCurrentNextIndicator() << std::endl;
    output << std::string(indent + 2, ' ') << "Section number: " << (int)getSectionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "Last Section number: " << (int)getLastSectionNumber() << std::endl;
    output << std::string(indent + 2, ' ') << "PCR PID: " << getPCRPID() << std::endl;
    output << std::string(indent + 2, ' ') << "Program Info Length: " << getProgramInfoLength() << std::endl;
    output << std::string(indent + 2, ' ') << "Programs [" << getProgramCount() << "]" << std::endl;
    for (int i = 0; i < getProgramCount(); i++) {
      output << std::string(indent + 4, ' ') << "[" << i + 1 << "] ";
      output << "StreamType: 0x" << std::hex << std::setw(2) << std::setfill('0') << std::uppercase << (int)getStreamType(i) << std::dec << ", ";
      output << "Elementary PID: " << getElementaryPID(i) << ", ";
      output << "ES Info Length: " << getESInfoLength(i);
      output << std::endl;
    }
    output << std::string(indent + 2, ' ') << "CRC32: " << std::hex << std::setw(8) << std::setfill('0') << std::uppercase << getCRC() << std::dec << std::endl;
    return output.str();
  }
}



