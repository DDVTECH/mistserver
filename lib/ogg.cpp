#include "ogg.h"
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <arpa/inet.h>

#define lsb32(offset) data[offset] | data[offset+1] << 8 | data[offset+2] << 16 | data[offset+3] << 24

namespace OGG{
  Page::Page(){
    data = NULL;
    datasize = 0;
    dataSum = 0;
  }

  bool Page::read(std::string & newData){
    //datasize = 0;
    if (newData.size()<27){
      return false;
    }
    dataSum = 0;
    if (!checkDataSize(27)){
      return false;
    }
    memcpy(data, newData.c_str(), 27);//copying the header, always 27 bytes
    if(!checkDataSize(27 + getPageSegments())){
      return false;
    }
    memcpy(data + 27, newData.c_str() + 27, getPageSegments()); 
    //copying the first part of the page into data, which tells the size of the page
    
    for(unsigned int i = 0; i < getPageSegments(); i++){
      dataSum += getSegmentTable()[i];
    }
    if(!checkDataSize(27 + getPageSegments()+dataSum)){
      return false;
    }
    memcpy(data + 27 + getPageSegments(), newData.c_str() + 27 + getPageSegments(), dataSum);
    newData.erase(0, getPageSize());
    return true;
  }
  
  long unsigned int Page::getMagicNumber(){
    return ntohl(((long unsigned int*)(data))[0]);
  }

  void Page::setMagicNumber(){
    if(checkDataSize(4)){
      memcpy(data, "OggS", 4);
    }
  }
  
  char Page::getVersion(){
    return data[4];
  }

  void Page::setVersion(char newVal){
    if(checkDataSize(5)){
      data[4] = newVal;
    }
  }
  
  char Page::getHeaderType(){
    return data[5];
  }

  void Page::setHeaderType(char newVal){
    if(checkDataSize(6)){
      data[5] = newVal;
    }
  }
  
  long long unsigned int Page::getGranulePosition(){
    if(checkDataSize(14)){
      //switching bit order upon return
      return ntohl(((long unsigned*)(data+6))[1]) & ((long long unsigned)(ntohl(((long unsigned*)(data+6))[0]) << 32));
    }
    return 0;
  }

  void Page::setGranulePosition(long long unsigned int newVal){
    if(checkDataSize(14)){
      ((long unsigned*)(data+6))[1] = htonl(newVal & 0xFFFFFFFF);
      ((long unsigned*)(data+6))[0] = htonl((newVal >> 32) & 0xFFFFFFFF);
    }
  }
  
  long unsigned int Page::getBitstreamSerialNumber(){
    //return ntohl(((long unsigned int*)(data+14))[0]);
    return lsb32(14);
  }

  void Page::setBitstreamSerialNumber(long unsigned int newVal){
    if(checkDataSize(18)){
      ((long unsigned *)(data+14))[0] = htonl(newVal);
    }
  }
  
  long unsigned int Page::getPageSequenceNumber(){
    return lsb32(18);
  }

  void Page::setPageSequenceNumber(long unsigned int newVal){
    if(checkDataSize(22)){
      ((long unsigned *)(data+18))[0] = htonl(newVal);
    }
  }
  
  long unsigned int Page::getCRCChecksum(){
    return ntohl(((long unsigned int*)(data+22))[0]);
    //return lsb32(22);
  }
  
  void Page::setCRCChecksum(long unsigned int newVal){
    if(checkDataSize(26)){
      ((long unsigned *)(data+22))[0] = htonl(newVal);
    }
  }
  
  char Page::getPageSegments(){
    return data[26];  
  }
  
  inline void Page::setPageSegments(char newVal){
    data[26] = newVal;
  }
  
  char* Page::getSegmentTable(){
    return data+27;
  }
  
  std::deque<unsigned int> Page::getSegmentTableDeque(){
    std::deque<unsigned int> retVal;
    unsigned int temp = 0;
    char* segmentTable = getSegmentTable();
    for (unsigned int i = 0; i < getPageSegments(); i++){
      temp += segmentTable[i];
      if (segmentTable[i] < 255){
        retVal.push_back(temp);
        temp = 0;
      }
    }
    return retVal;
  }

  bool Page::setSegmentTable(std::vector<unsigned int> layout){
    unsigned int place = 0;
    char table[255];
    for (unsigned int i = 0; i < layout.size(); i++){
      while (layout[i]>=255){
        if (place >= 255) return false;
        table[place] = 255;
        layout[i] -= 255;
        place++;
      }
      if (place >= 255) return false;
      table[place] = layout[i];
      place++;
    }
    setSegmentTable(table,place);
    return true;
  }
  
  void Page::setSegmentTable(char* newVal, unsigned int length){
    if(checkDataSize(27 + length)){
      memcpy(data + 27, newVal, length);
    }
  }
  
  unsigned long int Page::getPageSize(){
    return 27 + getPageSegments()+dataSum;
  }

  char* Page::getFullPayload(){
    return data + 27 + getPageSegments();
  }
  
  bool Page::typeBOS(){
    if (getHeaderType() & 0x02){
      return true;
    }
    return false;
  }
  
  bool Page::typeEOS(){
    if (getHeaderType() & 0x04){
      return true;
    }
    return false;
  }
  
  bool Page::typeContinue(){
    if (getHeaderType() & 0x01){
      return true;
    }
    return false;
  }
  
  bool Page::typeNone(){
    if (getHeaderType() & 0x07 == 0x00){
      return true;
    }
    return false;
  }
  
  std::string Page::toPrettyString(){
    std::stringstream r;
    r << "Size(" << getPageSize() << ")(" << dataSum << ")" << std::endl;
    r << "Magic_Number: " << std::string(data, 4) << std::endl;
    r << "Version: " << (int)getVersion() << std::endl;
    r << "Header_type: " << std::hex << (int)getHeaderType() << std::dec;
    if (typeContinue()){
      r << " continued";
    }
    if (typeBOS()){
      r << " bos";
    }
    if (typeEOS()){
      r << " eos";
    }
    r << std::endl;
    r << "Granule_position: " << getGranulePosition() << std::endl;
    r << "Bitstream_SN: " << getBitstreamSerialNumber() << std::endl;
    r << "Page_sequence_number: " << getPageSequenceNumber() << std::endl;
    r << "CRC_checksum: " << std::hex << getCRCChecksum()<< std::dec << std::endl;
    r << "  Calced Checksum: " << std::hex << calcChecksum() << std::dec << std::endl;
    r << "CRC_checksum write: " << std::hex << getCRCChecksum()<< std::dec << std::endl;
    r << "Page_segments: " << (int)getPageSegments() << std::endl;
    r << "SegmentTable: ";
    std::deque<unsigned int> temp = getSegmentTableDeque();
    for (std::deque<unsigned int>::iterator i = temp.begin(); i != temp.end(); i++){
      r << (*i) << " ";
    }
    r << std::endl;
    return r.str();
  }
  
  long unsigned int Compute(char* buffer, unsigned int count){
    long unsigned int m_crc = ~0u;
    //const unsigned char* ptr = (const unsigned char *) buffer;
    for (unsigned int i = 0; i < count; i++) {
      buffer++;
      m_crc ^= ((unsigned long int)buffer << 24);
      for (int i = 0; i < 8; i++) {
        if (m_crc & 0x80000000) {
          m_crc = (m_crc << 1) ^ 0x04C11DB7;
        }else {
          m_crc <<= 1;
        }
      }
    }
    return m_crc;
  }

  long unsigned int Page::calcChecksum(){
    long unsigned int retVal = 0;
    long unsigned int oldChecksum = getCRCChecksum();
    setCRCChecksum (0);
    retVal = Compute(data, getPageSize());
    setCRCChecksum (oldChecksum);
    return retVal;
  }
  
  bool Page::checkDataSize(unsigned int size){
    if (size > datasize){
      void* tmp = realloc(data,size);
      if (tmp){
        data = (char*)tmp;
        datasize = size;
        return true;
      }else{
        return false;
      }
    }else{
      return true;
    }
  }
}
