/// \file ts_packet.cpp
/// Holds all code for the TS namespace.

#include <sstream>
#include <iomanip>
#include <string.h>
#include <set>
#include <map>
#include "ts_packet.h"
#include "defines.h"

#ifndef FILLER_DATA
#define FILLER_DATA "Lorem ipsum dolor sit amet, consectetur adipiscing elit. Praesent commodo vulputate urna eu commodo. Cras tempor velit nec nulla placerat volutpat. Proin eleifend blandit quam sit amet suscipit. Pellentesque vitae tristique lorem. Maecenas facilisis consequat neque, vitae iaculis eros vulputate ut. Suspendisse ut arcu non eros vestibulum pulvinar id sed erat. Nam dictum tellus vel tellus rhoncus ut mollis tellus fermentum. Fusce volutpat consectetur ante, in mollis nisi euismod vulputate. Curabitur vitae facilisis ligula. Sed sed gravida dolor. Integer eu eros a dolor lobortis ullamcorper. Mauris interdum elit non neque interdum dictum. Suspendisse imperdiet eros sed sapien cursus pulvinar. Vestibulum ut dolor lectus, id commodo elit. Cras convallis varius leo eu porta. Duis luctus sapien nec dui adipiscing quis interdum nunc congue. Morbi pharetra aliquet mauris vitae tristique. Etiam feugiat sapien quis augue elementum id ultricies magna vulputate. Phasellus luctus, leo id egestas consequat, eros tortor commodo neque, vitae hendrerit nunc sem ut odio."
#endif

std::set<unsigned int> pmt_pids;
std::map<unsigned int, std::string> stream_pids;


namespace TS {
/// This constructor creates an empty Packet, ready for use for either reading or writing.
/// All this constructor does is call Packet::clear().
  Packet::Packet() {
    pos=0;
    clear();
  }

  Packet::Packet(const Packet & rhs){
    memcpy(strBuf, rhs.strBuf, 188);
    pos = 188;
  }

/// This function fills a Packet from a file.
/// It fills the content with the next 188 bytes int he file.
/// \param Data The data to be read into the packet.
/// \return true if it was possible to read in a full packet, false otherwise.
  bool Packet::FromFile(FILE * data) {    
    long long int bPos = ftell(data);
    if (!fread((void *)strBuf, 188, 1, data)) {
      return false;
    }
    if (strBuf[0] != 0x47){
      HIGH_MSG("Failed to read a good packet on pos %lld", bPos);
      return false;
    }
    pos=188;
    return true;
  }

///This funtion fills a Packet from
///a char array. It fills the content with
///the first 188 characters of a char array
///\param Data The char array that contains the data to be read into the packet
///\return true if successful (which always happens, or else a segmentation fault should occur)
  bool Packet::FromPointer(const char * data) {    
    memcpy((void *)strBuf, (void *)data, 188);
    pos=188;    
    return true;
  }

/// The deconstructor deletes all space that may be occupied by a Packet.
  Packet::~Packet() {
  }

  ///update position in character array (pos), 
  void Packet::updPos(unsigned int newPos){
    if(pos < newPos){
      pos=newPos;
    }
  }
  
/// Sets the PID of a single Packet.
/// \param NewPID The new PID of the packet.
  void Packet::setPID(int NewPID) {
    strBuf[1] = (strBuf[1] & 0xE0) + ((NewPID & 0x1F00) >> 8);
    strBuf[2] = (NewPID & 0x00FF);
    updPos(2);    
  }

/// Gets the PID of a single Packet.
/// \return The value of the PID.
  unsigned int Packet::getPID() const{
    return (unsigned int)(((strBuf[1] & 0x1F) << 8) + strBuf[2]);
  }

/// Sets the Continuity Counter of a single Packet.
/// \param NewContinuity The new Continuity Counter of the packet.
  void Packet::setContinuityCounter(int NewContinuity) {
    strBuf[3] = (strBuf[3] & 0xF0) | (NewContinuity & 0x0F);
    updPos(3);
  }

/// Gets the Continuity Counter of a single Packet.
/// \return The value of the Continuity Counter.
  int Packet::getContinuityCounter() const{
    return (strBuf[3] & 0x0F);
  }

/// Gets the amount of bytes that are not written yet in a Packet.
/// \return The amount of bytes that can still be written to this packet.
  unsigned int Packet::getBytesFree() const{
    if(pos > 188){
      FAIL_MSG("pos is > 188. Actual pos: %d segfaulting gracefully :)", pos);
      ((char*)0)[0] = 1;
    }
    return 188 - pos;
  }

/// Sets the packet pos to 4, and resets the first 4 fields to defaults (including sync byte on pos 0)
  void Packet::clear() {    
    memset(strBuf,(char)0, 188);
    strBuf[0] = 0x47;
    strBuf[1] = 0x00;
    strBuf[2] = 0x00;
    strBuf[3] = 0x10;    
    pos=4;
  }

/// Sets the selection value for an adaptationfield of a Packet.
/// \param NewSelector The new value of the selection bits.
/// - 1: No AdaptationField.
/// - 2: AdaptationField Only.
/// - 3: AdaptationField followed by Data.
  void Packet::setAdaptationField(int NewSelector) {
    strBuf[3] = (strBuf[3] & 0xCF) + ((NewSelector & 0x03) << 4);
    if (NewSelector & 0x02) {
      strBuf[4] = 0x00;
    } else {
      pos=4;
    }
  }

/// Gets whether a Packet contains an adaptationfield.
/// \return The existence of an adaptationfield.
/// - 0: No adaptationfield present.
/// - 1: Adaptationfield is present.
  int Packet::getAdaptationField() const{
    return ((strBuf[3] & 0x30) >> 4);
  }

/// Sets the PCR (Program Clock Reference) of a Packet.
/// \param NewVal The new PCR Value.
  void Packet::setPCR(int64_t NewVal) {    
    updPos(12);    
    setAdaptationField(3);
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
  int64_t Packet::getPCR() const{
    if (!getAdaptationField()) {
      return -1;
    }
    if (!(strBuf[5] & 0x10)) {
      return -1;
    }
    int64_t Result = (((strBuf[6] << 24) | (strBuf[7] << 16) | (strBuf[8] << 8) | strBuf[9]) << 1) | (strBuf[10] >> 7);
    Result *= 300;
    Result |= (((strBuf[10] & 0x01) << 8) + strBuf[11]);
    return Result;
  }

/// Gets the OPCR (Original Program Clock Reference) of a Packet.
/// \return The value of the OPCR.
  int64_t Packet::getOPCR() const{
    if (!getAdaptationField()) {
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

/// Gets the transport error inficator of a Packet
/// \return  The transport error inficator of a Packet
  bool Packet::hasTransportError() const{
    return strBuf[1] & 0x80;
  }

/// Gets the payload unit start inficator of a Packet
/// \return  The payload unit start inficator of a Packet
  bool Packet::getUnitStart() const{
    return strBuf[1] & 0x40;
  }

/// Gets the transport priority of a Packet
/// \return  The transport priority of a Packet
  bool Packet::hasPriority() const{
    return strBuf[1] & 0x20;
  }

/// Gets the transport scrambling control of a Packet
/// \return  The transport scrambling control of a Packet
  unsigned int Packet::getTransportScramblingControl() const{
    return (unsigned int)((strBuf[3] >> 6) & (0x03));
  }

/// Gets the current length of the adaptationfield.
/// \return The length of the adaptationfield.
  int Packet::getAdaptationFieldLen() const{
    if (!getAdaptationField()) {
      return -1;
    }
    return (int)strBuf[4];
  }

  Packet::operator bool() const{
    return pos && strBuf[0] == 0x47;
  }

/// Prints a packet to stdout, for analyser purposes.
  std::string Packet::toPrettyString(size_t indent, int detailLevel) const{
    if (!(*this)){
      return "[Invalid packet - no sync byte]";
    }
    std::stringstream output;
    output << std::string(indent, ' ') << "[PID " << getPID() << "|" << std::hex << getContinuityCounter() << std::dec << ": " << getDataSize() << "b ";
    if (!getPID()){
      output << "PAT";
    }else{
      if (pmt_pids.count(getPID())){
        output << "PMT";
      }else{
        if (stream_pids.count(getPID())){
          output << stream_pids[getPID()];
        }else{
          output << "Unknown";
        }
      }
    }
    output << "]";
    if (getUnitStart()){
      output << " [Start]";
    }
    if (getAdaptationField() > 1 && getAdaptationFieldLen()) {
      if (hasDiscontinuity()){
        output << " [Discontinuity]";
      }
      if (getRandomAccess()){
        output << " [RandomXS]";
      }
      if (hasPCR()) {
        output << " [PCR " << (double)getPCR() / 27000000 << "s]";
      }
      if (hasOPCR()) {
        output<< " [OPCR: " << (double)getOPCR() / 27000000 << "s]";
      }
    }
    output << std::endl;
    if (!getPID()) {
      //PAT
      if (detailLevel >= 2){
        output << ((ProgramAssociationTable *)this)->toPrettyString(indent + 2);
      }else{
        ((ProgramAssociationTable *)this)->toPrettyString(indent + 2);
      }
      return output.str();
    }
    
    if (pmt_pids.count(getPID())){
      //PMT
      if (detailLevel >= 2){
        output << ((ProgramMappingTable *)this)->toPrettyString(indent + 2);
      }else{
        ((ProgramMappingTable *)this)->toPrettyString(indent + 2);
      }
      return output.str();
    }
    
    if (detailLevel >= 3){
      output << std::string(indent+2, ' ') << "Raw data bytes:";
      unsigned int size = getDataSize();
      
      for (unsigned int i = 0; i < size; ++i){
        if (!(i % 32)){
          output << std::endl << std::string(indent + 4, ' ');
        }
        output << std::hex << std::setw(2) << std::setfill('0') << (unsigned int)(strBuf+188-size)[i] << " ";
        if ((i % 4) == 3){
          output << " ";
        }
      }
      output << std::endl;
    }
    
    return output.str();
  }
  
  unsigned int Packet::getDataSize() const{
    return 184 - ((getAdaptationField() > 1) ? getAdaptationFieldLen() + 1 : 0);
  }

  /// Returns true if this PID contains a PMT.
  /// Important caveat: only works if the corresponding PAT has been pretty-printed earlier!
  bool Packet::isPMT() const{
    return pmt_pids.count(getPID());
  }

/// Sets the start of a new unit in this Packet.
/// \param NewVal The new value for the start of a unit.
  void Packet::setUnitStart(bool NewVal) {
    if (NewVal) {
      strBuf[1] |= 0x40;
    } else {
      strBuf[1] &= 0xBF;
    }
  }

/// Gets the elementary stream priority indicator of a Packet
/// \return  The elementary stream priority indicator of a Packet
  bool Packet::hasESpriority() const{
    return strBuf[5] & 0x20;
  }

  bool Packet::hasDiscontinuity() const{
    return strBuf[5] & 0x80;
  }
  
  void Packet::setDiscontinuity(bool newVal){
    updPos(6);
    if (getAdaptationField() == 3) {
      if (!strBuf[4]) {
        strBuf[4] = 1;
      }
      if (newVal) {
        strBuf[5] |= 0x80;
      } else {
        strBuf[5] &= 0x7F;
      }
    } else {
      setAdaptationField(3);
      strBuf[4] = 1;
      if (newVal) {
        strBuf[5] = 0x80;
      } else {
        strBuf[5] = 0x00;
      }
    }
  }

/// Gets whether this Packet can be accessed at random (indicates keyframe).
/// \return Whether or not this Packet contains a keyframe.
  bool Packet::getRandomAccess() const{
    if (getAdaptationField() < 2) {
      return false;
    }
    return strBuf[5] & 0x40;
  }

///Gets the value of the PCR flag
///\return true if there is a PCR, false otherwise
  bool Packet::hasPCR() const{
    return strBuf[5] & 0x10;
  }

///Gets the value of the OPCR flag
///\return true if there is an OPCR, false otherwise
  bool Packet::hasOPCR() const{
    return strBuf[5] & 0x08;
  }

///Gets the value of the splicing point  flag
///\return the value of the splicing point flag
  bool Packet::hasSplicingPoint() const{
    return strBuf[5] & 0x04;
  }

///Gets the value of the transport private data point flag
///\return the value of the transport private data point flag
  void Packet::setRandomAccess(bool NewVal) {
    updPos(6);
    if (getAdaptationField() == 3) {
      if (!strBuf[4]) {
        strBuf[4] = 1;
      }
      if (NewVal) {
        strBuf[5] |= 0x40;
      } else {
        strBuf[5] &= 0xBF;
      }
    } else {
      setAdaptationField(3);
      strBuf[4] = 1;
      if (NewVal) {
        strBuf[5] = 0x40;
      } else {
        strBuf[5] = 0x00;
      }
    }
  }

/// Transforms the Packet into a standard Program Association Table
  void Packet::setDefaultPAT() {
    static int MyCntr = 0;
    memcpy((void*)strBuf, (void*)PAT, 188);
    pos=188;
    setContinuityCounter(MyCntr++);
    MyCntr %= 0x10;
  }

/// Checks the size of the internal packet buffer (prints error if size !=188), then returns a pointer to the data.
/// \return A character pointer to the internal packet buffer data
  const char * Packet::checkAndGetBuffer() const{
    if (pos != 188) {
      DEBUG_MSG(DLVL_HIGH, "Size invalid (%d) - invalid data from this point on", pos);
    }
    return strBuf;
  }


//BEGIN PES FUNCTIONS
//pes functons do not use the internal strBuf character buffer
  ///\brief Appends the PES-encoded timestamp to a string.
  ///\param strBuf The string to append to
  ///\param fixedLead The "fixed" 4-bit lead value to use
  ///\param time The timestamp to encode
  void encodePESTimestamp(std::string & tmpBuf, char fixedLead, unsigned long long time){
    //FixedLead of 4 bits, bits 32-30 time, 1 marker bit
    tmpBuf += (char)(fixedLead | ((time & 0x1C0000000LL) >> 29) | 0x01);
    //Bits 29-22 time
    tmpBuf += (char)((time & 0x03FC00000LL) >> 22);
    //Bits 21-15 time, 1 marker bit
    tmpBuf += (char)(((time & 0x0003F8000LL) >> 14) | 0x01);
    //Bits 14-7 time
    tmpBuf += (char)((time & 0x000007F80LL) >> 7);
    //Bits 7-0 time, 1 marker bit
    tmpBuf += (char)(((time & 0x00000007FLL) << 1) | 0x01);
  }

/// Generates a PES Lead-in for a video frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param len The length of this frame.
/// \param PTS The timestamp of the frame.
  std::string & Packet::getPESVideoLeadIn(unsigned int len, unsigned long long PTS, unsigned long long offset, bool isAligned) {
    len += (offset ? 13 : 8);
    static std::string tmpStr;
    tmpStr.clear();
    tmpStr.reserve(25);
    tmpStr.append("\000\000\001\340", 4);
    tmpStr += (char)((len >> 8) & 0xFF);
    tmpStr += (char)(len & 0xFF);
    if (isAligned){
      tmpStr.append("\204", 1);
    }else{
      tmpStr.append("\200", 1);
    }
    tmpStr += (char)(offset ? 0xC0 : 0x80) ; //PTS/DTS + Flags
    tmpStr += (char)(offset ? 0x0A : 0x05); //PESHeaderDataLength
    encodePESTimestamp(tmpStr, (offset ? 0x30 : 0x20), PTS + offset);
    if (offset){
      encodePESTimestamp(tmpStr, 0x10, PTS);
    }
    return tmpStr;
  }

/// Generates a PES Lead-in for an audio frame.
/// Prepends the lead-in to variable toSend, assumes toSend's length is all other data.
/// \param len The length of this frame.
/// \param PTS The timestamp of the frame.
  std::string & Packet::getPESAudioLeadIn(unsigned int len, unsigned long long PTS) {
    static std::string tmpStr;
    tmpStr.clear();
    tmpStr.reserve(14);
    len += 8;
    tmpStr.append("\000\000\001\300", 4);
    tmpStr += (char)((len & 0xFF00) >> 8); //PES PacketLength
    tmpStr += (char)(len & 0x00FF); //PES PacketLength (Cont)
    tmpStr.append("\204\200\005", 3);
    encodePESTimestamp(tmpStr, 0x20, PTS);
    return tmpStr;
  }
//END PES FUNCTIONS

/// Fills the free bytes of the Packet.
/// Stores as many bytes from NewVal as possible in the packet.
/// The minimum of Packet::BytesFree and maxLen is used.
/// \param NewVal The data to store in the packet.
/// \param maxLen The maximum amount of bytes to store.
  int Packet::fillFree(const char * NewVal, int maxLen) {
    int toWrite = std::min((int)getBytesFree(), maxLen);    
    memcpy((void*)(strBuf+pos), (void*)NewVal, toWrite);
    pos+=toWrite;
    return toWrite;
  }

/// Adds stuffing to the Packet depending on how much content you want to send.
/// \param NumBytes the amount of non-stuffing content bytes you want to send.
/// \return The amount of content bytes that can be send.
  void Packet::addStuffing() {
    unsigned int numBytes = getBytesFree();
    if (!numBytes) {
      return;
    }
    
    if (getAdaptationField() == 2){
      FAIL_MSG("Can not handle adaptation field 2 - should stuff the entire packet, no data will follow after the adaptation field"); ///\todo more stuffing required
      return;
    }
        
    if (getAdaptationField() == 1){
      //Convert adaptationfield to 3, by shifting the \001 at [4] (and all after it) to [5].
      if (numBytes == 184){
        strBuf[pos++]=0x0; //strBuf.append("\000", 1);
      }else{
        //strBuf.insert(4, 1, (char)0);
        memmove((void*) (strBuf+5), (void*)(strBuf+4), 188-4-1);
        pos++;
      }
      setAdaptationField(3);//sets [4] to 0
    }

    numBytes=getBytesFree(); //get the most recent strBuf len before stuffing

    if (getAdaptationField() == 3 && numBytes ) {
      if (strBuf[4] == 0){//if we already have stuffing
        memmove((void*) (strBuf+5+numBytes), (void*)(strBuf+5), 188-5-numBytes);
        memset((void*)(strBuf+5),'$',numBytes);
        pos+=numBytes;
      }else{
        memmove((void*)(strBuf+5+strBuf[4]+numBytes), (void*)(strBuf+5+strBuf[4]) , 188-5-strBuf[4]-numBytes);
        memset((void*)(strBuf+5+strBuf[4]),'$',numBytes);
        pos+=numBytes;      
      }
      strBuf[4] += numBytes;//add stuffing to the stuffing counter at [4]
    }
    if (numBytes){//if we added stuffing...
      if (numBytes == strBuf[4]){//and the stuffing is ALL the stuffing...
        strBuf[5] = 0x00;//set [5] to zero for some reason...?
        numBytes --;//decrease the stuffing needed by one
      }
      //overwrite bytes [5+currStuffing-newStuffing] onward with newStuffing bytes of prettier filler data
      for (int i = 0; i < numBytes; i++) {
        strBuf[5+(strBuf[4] - numBytes)+i] = FILLER_DATA[i % sizeof(FILLER_DATA)];
      }
    }  
  }

///returns the character buffer with a std::string wrapper
///\return The raw TS data as a string
  //const std::string& Packet::getStrBuf() const{
//    return std::string(strBuf);
//  }

///Gets the payload of this packet, as a raw char array
///\return The payload of this ts packet as a char pointer
  const char * Packet::getPayload() const{
    return strBuf + (4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0));
  }

  ///Gets the length of the payload for this apcket
  ///\return The amount of bytes payload in this packet
  int Packet::getPayloadLength() const{
    return 184 - ((getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0));
  }


  ProgramAssociationTable & ProgramAssociationTable::operator = (const Packet & rhs){
    memcpy(strBuf, rhs.checkAndGetBuffer(), 188);
    pos = 188;
    return *this;
  }
  ///Retrieves the current addStuffingoffset value for a PAT
  char ProgramAssociationTable::getOffset() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0);
    return strBuf[loc];
  }

  ///Retrieves the ID of this table
  char ProgramAssociationTable::getTableId() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    return strBuf[loc];
  }

  ///Retrieves the current section length
  short ProgramAssociationTable::getSectionLength() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    return (short)(strBuf[loc] & 0x0F) << 8 | strBuf[loc + 1];
  }

  ///Retrieves the Transport Stream ID
  short ProgramAssociationTable::getTransportStreamId() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    return (short)(strBuf[loc] & 0x0F) << 8 | strBuf[loc + 1];
  }

  ///Retrieves the version number
  char ProgramAssociationTable::getVersionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x1F;
  }

  ///Retrieves the "current/next" indicator
  bool ProgramAssociationTable::getCurrentNextIndicator() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x01;
  }

  ///Retrieves the section number
  char ProgramAssociationTable::getSectionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    return strBuf[loc];
  }

  ///Retrieves the last section number
  char ProgramAssociationTable::getLastSectionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    return strBuf[loc];
  }

  ///Returns the amount of programs in this table
  short ProgramAssociationTable::getProgramCount() const{
    //This is correct, not -12 since we already parsed 4 bytes here
    return (getSectionLength() - 8) / 4;
  }

  short ProgramAssociationTable::getProgramNumber(short index) const{
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return ((short)(strBuf[loc + (index * 4)]) << 8) | strBuf[loc + (index * 4) + 1];
  }

  short ProgramAssociationTable::getProgramPID(short index) const{
    if (index > getProgramCount()) {
      return 0;
    }
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return (((short)(strBuf[loc + (index * 4) + 2]) << 8) | strBuf[loc + (index * 4) + 3]) & 0x1FFF;
  }

  int ProgramAssociationTable::getCRC() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 9 + (getProgramCount() * 4);
    return ((int)(strBuf[loc]) << 24) | ((int)(strBuf[loc + 1]) << 16) | ((int)(strBuf[loc + 2]) << 8) | strBuf[loc + 3];
  }

///This function prints a program association table,
///prints all values in a human readable format
///\param indent The indentation of the string printed as wanted by the user
///\return The string with human readable data from a PAT
  std::string ProgramAssociationTable::toPrettyString(size_t indent) const{
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
      pmt_pids.insert(getProgramPID(i));
      output << std::endl;
    }
    output << std::string(indent + 2, ' ') << "CRC32: " << std::hex << std::setw(8) << std::setfill('0') << std::uppercase << getCRC() << std::dec << std::endl;
    return output.str();

  }

  ProgramMappingEntry::ProgramMappingEntry(char * begin, char * end){
    data = begin;
    boundary = end;
  }

  ProgramMappingEntry::operator bool() const {
    return data && (data < boundary);
  }

  int ProgramMappingEntry::getStreamType() const{
    return data[0];
  }

  void ProgramMappingEntry::setStreamType(int newType){
    data[0] = newType;
  }

  std::string ProgramMappingEntry::getCodec() const{
    switch (getStreamType()){
      case 0x01:
      case 0x03: return "MPEG1";
      case 0x02: return "MPEG1/2";
      case 0x04:
      case 0x05:
      case 0x06: return "MPEG2";
      case 0x07: return "MHEG";
      case 0x08: return "MPEG2 DSM CC";
      case 0x09: return "H.222.1";
      case 0x0A: return "DSM CC encapsulation";
      case 0x0B: return "DSM CC U-N";
      case 0x0C: return "DSM CC descriptor";
      case 0x0D: return "DSM CC section";
      case 0x0E: return "MPEG2 aux";
      case 0x0F: return "ADTS";
      case 0x10: return "MPEG4";
      case 0x11: return "LATM";
      case 0x12: return "SL/Flex PES";
      case 0x13: return "SL/Flex section";
      case 0x14: return "SDP";
      case 0x15: return "meta PES";
      case 0x16: return "meta section";
      case 0x1B: return "H264";
      case 0x81: return "AC3";
      default: return "unknown";
    }
  }

  std::string ProgramMappingEntry::getStreamTypeString() const{
    switch (getStreamType()){
      case 0x01:
      case 0x02:
      case 0x09:
      case 0x10:
      case 0x1B: return "video";
      case 0x03:
      case 0x04:
      case 0x11:
      case 0x81:
      case 0x0F: return "audio";
      default: return "data";
    }
  }
  
  int ProgramMappingEntry::getElementaryPid() const{
    return ((data[1] << 8) | data[2]) & 0x1FFF;
  }

  void ProgramMappingEntry::setElementaryPid(int newElementaryPid) {
    data[1] = newElementaryPid >> 8 & 0x1F;
    data[2] = newElementaryPid & 0xFF;
  }

  int ProgramMappingEntry::getESInfoLength() const{
    return ((data[3] << 8) | data[4]) & 0x0FFF;
  }

  const char * ProgramMappingEntry::getESInfo() const{
    return data + 5;
  }

  void ProgramMappingEntry::setESInfo(const std::string & newInfo){
    data[3] = (newInfo.size() >> 8) & 0x0F;
    data[4] = newInfo.size() & 0xFF;
    memcpy(data + 5, newInfo.data(), newInfo.size());
  }

  void ProgramMappingEntry::advance(){
    if (!(*this)) {
      return;
    }
    data += 5 + getESInfoLength();
  }
  
  ProgramMappingTable::ProgramMappingTable(){        
    strBuf[0] = 0x47;
    strBuf[1] = 0x50;
    strBuf[2] = 0x00;
    strBuf[3] = 0x10;
    pos=4;
  }

  ProgramMappingTable & ProgramMappingTable::operator = (const Packet & rhs) {
    memcpy(strBuf, rhs.checkAndGetBuffer(), 188);
    pos = 188;
    return *this;
  }

  char ProgramMappingTable::getOffset() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0);
    return strBuf[loc];
  }

  void ProgramMappingTable::setOffset(char newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0);
    strBuf[loc] = newVal;
  }  

  char ProgramMappingTable::getTableId() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    return strBuf[loc];
  }

  void ProgramMappingTable::setTableId(char newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 1;
    updPos(loc+1);    
    strBuf[loc] = newVal;
  }

  short ProgramMappingTable::getSectionLength() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    return (((short)strBuf[loc] & 0x0F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setSectionLength(short newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 2;
    updPos(loc+2);
    strBuf[loc] = (char)(newVal >> 8);
    strBuf[loc+1] = (char)newVal;
  }

  short ProgramMappingTable::getProgramNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    return (((short)strBuf[loc]) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setProgramNumber(short newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 4;
    updPos(loc+2);
    strBuf[loc] = (char)(newVal >> 8);
    strBuf[loc+1] = (char)newVal;
  }

  char ProgramMappingTable::getVersionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x1F;
  }

  void ProgramMappingTable::setVersionNumber(char newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    updPos(loc+1);
    strBuf[loc] = ((newVal & 0x1F) << 1) | 0xC1;
  }

  ///Retrieves the "current/next" indicator
  bool ProgramMappingTable::getCurrentNextIndicator() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    return (strBuf[loc] >> 1) & 0x01;
  }

  ///Sets the "current/next" indicator
  void ProgramMappingTable::setCurrentNextIndicator(bool newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 6;
    updPos(loc+1);
    strBuf[loc] = (((char)newVal) << 1) | (strBuf[loc] & 0xFD) | 0xC1;
  }

  ///Retrieves the section number
  char ProgramMappingTable::getSectionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    return strBuf[loc];
  }

  ///Sets the section number
  void ProgramMappingTable::setSectionNumber(char newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 7;
    updPos(loc+1);
    strBuf[loc] = newVal;
  }

  ///Retrieves the last section number
  char ProgramMappingTable::getLastSectionNumber() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    return strBuf[loc];
  }

  ///Sets the last section number
  void ProgramMappingTable::setLastSectionNumber(char newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 8;
    updPos(loc+1);
    strBuf[loc] = newVal;
  }

  short ProgramMappingTable::getPCRPID() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    return (((short)strBuf[loc] & 0x1F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setPCRPID(short newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 9;
    updPos(loc+2);
    strBuf[loc] = (char)((newVal >> 8) & 0x1F) | 0xE0;//Note: here we set reserved bits on 1
    strBuf[loc+1] = (char)newVal;
  }

  short ProgramMappingTable::getProgramInfoLength() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 11;
    return (((short)strBuf[loc] & 0x0F) << 8) | strBuf[loc + 1];
  }

  void ProgramMappingTable::setProgramInfoLength(short newVal) {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 11;
    updPos(loc+2);
    strBuf[loc] = (char)((newVal >> 8) & 0x0F) | 0xF0;//Note: here we set reserved bits on 1
    strBuf[loc+1] = (char)newVal;
  }

  ProgramMappingEntry ProgramMappingTable::getEntry(int index) const{
    int dataOffset = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset();
    ProgramMappingEntry res((char*)(strBuf + dataOffset + 13 + getProgramInfoLength()), (char*)(strBuf + dataOffset + getSectionLength()) );
    for (int i = 0; i < index; i++){
      res.advance();
    }
    return res;
  }

  int ProgramMappingTable::getCRC() const{
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + getSectionLength();
    return ((int)(strBuf[loc]) << 24) | ((int)(strBuf[loc + 1]) << 16) | ((int)(strBuf[loc + 2]) << 8) | strBuf[loc + 3];
  }
  
  void ProgramMappingTable::calcCRC() {
    unsigned int loc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + getSectionLength();
    unsigned int newVal;//this will hold the CRC32 value;
    unsigned int pidLoc = 4 + (getAdaptationField() > 1 ? getAdaptationFieldLen() + 1 : 0) + getOffset() + 1;//location of PCRPID
    newVal = checksum::crc32(-1, strBuf + pidLoc, loc - pidLoc);//calculating checksum over all the fields from table ID to the last stream element
    updPos(188);  
    strBuf[loc + 3] = (newVal >> 24) & 0xFF;
    strBuf[loc + 2] = (newVal >> 16) & 0xFF;
    strBuf[loc + 1] = (newVal >> 8) & 0xFF;
    strBuf[loc] = newVal & 0xFF;
    memset((void*)(strBuf + loc + 4), 0xFF, 184 - loc);
  }

///Print all PMT values in a human readable format
///\param indent The indentation of the string printed as wanted by the user
///\return The string with human readable data from a PMT table
  std::string ProgramMappingTable::toPrettyString(size_t indent) const{
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
    ProgramMappingEntry entry = getEntry(0);
    while (entry) {
      output << std::string(indent + 4, ' ');
      stream_pids[entry.getElementaryPid()] = entry.getCodec() + std::string(" ") + entry.getStreamTypeString();
      output << "Stream " << entry.getElementaryPid() << ": " << stream_pids[entry.getElementaryPid()] << " (" << entry.getStreamType() << "), InfoLen = " << entry.getESInfoLength() << std::endl;
      entry.advance();
    }
    output << std::string(indent + 2, ' ') << "CRC32: " << std::hex << std::setw(8) << std::setfill('0') << std::uppercase << getCRC() << std::dec << std::endl;
    return output.str();
  }
  
  
  /// Construct a PMT (special 188B ts packet) from a set of selected tracks and metadata.
  /// This function is not part of the packet class, but it is in the TS namespace.
  /// It uses an internal static TS packet for PMT storage.
  ///\param selectedTracks tracks to include in PMT creation
  ///\param myMeta 
  ///\returns character pointer to a static 188B TS packet
  const char * createPMT(std::set<unsigned long>& selectedTracks, DTSC::Meta& myMeta, int contCounter){
    static ProgramMappingTable PMT;
    PMT.setPID(4096);
    PMT.setTableId(2);
    //section length met 2 tracks: 0xB017
    int sectionLen = 0;
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      sectionLen += 5;
      if (myMeta.tracks[*it].codec == "ID3"){
        sectionLen += myMeta.tracks[*it].init.size();
      }
    }
    PMT.setSectionLength(0xB00D + sectionLen);
    PMT.setProgramNumber(1);
    PMT.setVersionNumber(0);
    PMT.setCurrentNextIndicator(0);
    PMT.setSectionNumber(0);
    PMT.setLastSectionNumber(0);
    PMT.setContinuityCounter(contCounter);
    int vidTrack = -1;
    for (std::set<unsigned long>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].type == "video"){
        vidTrack = *it;
        break;
      }
    }
    if (vidTrack == -1){
      vidTrack = *(selectedTracks.begin());
    }
    PMT.setPCRPID(256 + vidTrack);
    PMT.setProgramInfoLength(0);
    short id = 0;    
    ProgramMappingEntry entry = PMT.getEntry(0);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      entry.setElementaryPid(256 + *it);
      if (myMeta.tracks[*it].codec == "H264"){
        entry.setStreamType(0x1B);
      }else if (myMeta.tracks[*it].codec == "HEVC"){
        entry.setStreamType(0x24);
      }else if (myMeta.tracks[*it].codec == "AAC"){
        entry.setStreamType(0x0F);
      }else if (myMeta.tracks[*it].codec == "MP3"){
        entry.setStreamType(0x03);
      }else if (myMeta.tracks[*it].codec == "AC3"){
        entry.setStreamType(0x81);
      }else if (myMeta.tracks[*it].codec == "ID3"){
        entry.setStreamType(0x15);
        entry.setESInfo(myMeta.tracks[*it].init);
      }
      entry.advance();
    }
    PMT.calcCRC();
    return PMT.checkAndGetBuffer();
  }

}



