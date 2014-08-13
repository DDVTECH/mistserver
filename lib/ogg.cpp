#include "ogg.h"
#include "defines.h"
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <arpa/inet.h>

namespace OGG {
  inline long long unsigned int get_64(char * data) {
    long long unsigned int temp = 0;
    for (int i = 7; i >= 0; --i) {
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }

  inline long unsigned int get_32(char * data) {
    long unsigned int temp = 0;
    for (int i = 3; i >= 0; --i) {
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }

  inline void set_64(char * data, long unsigned int val) {
    for (int i = 0; i < 8; ++i) {
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }


  inline void set_32(char * data, long unsigned int val) {
    for (int i = 0; i < 4; ++i) {
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }


  Page::Page() {
    data = NULL;
    datasize = 0;
    dataSum = 0;
  }

  Page::~Page() {
    if (data) {
      free(data);
    }
  }

  bool Page::read(std::string & newData) {
    segmentTableDeque.clear();
    //datasize = 0;
    if (newData.size() < 27) {
      return false;
    }
    if (newData.substr(0, 4) != "OggS") {
      DEBUG_MSG(DLVL_FAIL, "Invalid Ogg page encountered - cannot continue");
      return false;
    }
    dataSum = 0;
    if (!checkDataSize(27)) {
      return false;
    }
    memcpy(data, newData.c_str(), 27);//copying the header, always 27 bytes

    if (newData.size() < 27u + getPageSegments()) { //check input size
      return false;
    }
    if (!checkDataSize(27 + getPageSegments())) { //check if size available in memory
      return false;
    }
    memcpy(data + 27, newData.c_str() + 27, getPageSegments());
    //copying the first part of the page into data, which tells the size of the page

    for (unsigned int i = 0; i < getPageSegments(); i++) {
      dataSum += getSegmentTable()[i];
    }

    if (newData.size() < 27 + getPageSegments() + dataSum) { //check input size
      dataSum = 0;
      return false;
    }
    if (!checkDataSize(27 + getPageSegments() + dataSum)) {
      dataSum = 0;
      return false;
    }
    memcpy(data + 27 + getPageSegments(), newData.c_str() + 27 + getPageSegments(), dataSum);
    newData.erase(0, getPageSize());
    return true;
  }


  bool Page::read(FILE * inFile) {
    segmentTableDeque.clear();
    int oriPos = ftell(inFile);
    dataSum = 0;
    if (!checkDataSize(27)) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: memory allocation");
      return false;
    }
    if (!fread(data, 27, 1, inFile)) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: fread");
      fseek(inFile, oriPos, SEEK_SET);
      return false;
    }
    if (!checkDataSize(27 + getPageSegments())) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: memory allocation1");
      return false;
    }
    if (!fread(data + 27, getPageSegments(), 1, inFile)) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: fread1");
      fseek(inFile, oriPos, SEEK_SET);
      return false;
    }
    for (unsigned int i = 0; i < getPageSegments(); i++) {
      dataSum += data[27 + i];
    }
    if (!checkDataSize(27 + getPageSegments() + dataSum)) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: memory allocation2");
      dataSum = 0;
      return false;
    }
    if (!fread(data + 27 + getPageSegments(), dataSum, 1, inFile)) {
      DEBUG_MSG(DLVL_WARN, "Unable to read a page: fread2");
      fseek(inFile, oriPos, SEEK_SET);
      dataSum = 0;
      return false;
    }
    return true;
  }

  bool Page::getSegment(unsigned int index, char * ret, unsigned int & len) {
    if (index > segmentTableDeque.size()) {
      ret = NULL;
      len = 0;
      return false;
    }
    ret = getFullPayload();
    for (unsigned int i = 0; i < index; i++) {
      ret += segmentTableDeque[i];
    }
    len = segmentTableDeque[index];
    return true;
  }

  void Page::setMagicNumber() {
    if (checkDataSize(4)) {
      memcpy(data, "OggS", 4);
    }
  }

  char Page::getVersion() {
    return data[4];
  }

  void Page::setVersion(char newVal) {
    if (checkDataSize(5)) {
      data[4] = newVal;
    }
  }

  char Page::getHeaderType() {
    return data[5];
  }

  void Page::setHeaderType(char newVal) {
    if (checkDataSize(6)) {
      data[5] = newVal;
    }
  }

  long long unsigned int Page::getGranulePosition() {
    if (checkDataSize(14)) {
      //switching bit order upon return
      //return ntohl(((long unsigned*)(data+6))[1]) & ((long long unsigned)((long long unsigned)ntohl(((long unsigned*)(data+6))[0]) << 32));
      //long long unsigned int temp;
      //temp = ((long unsigned int)(data+6)[0]);
      //temp = temp << 32 + ((long unsigned int)(data+6)[1]);
      return get_64(data + 6);
    }
    return 0;
  }

  void Page::setGranulePosition(long long unsigned int newVal) {
    if (checkDataSize(14)) {
      set_64(data + 6, newVal);
    }
  }

  long unsigned int Page::getBitstreamSerialNumber() {
    //return ntohl(((long unsigned int*)(data+14))[0]);
    return get_32(data + 14);
  }

  void Page::setBitstreamSerialNumber(long unsigned int newVal) {
    if (checkDataSize(18)) {
      //((long unsigned *)(data+14))[0] = htonl(newVal);
      set_32(data + 14, newVal);
    }
  }

  long unsigned int Page::getPageSequenceNumber() {
    return get_32(data + 18);
  }

  void Page::setPageSequenceNumber(long unsigned int newVal) {
    if (checkDataSize(22)) {
      //((long unsigned *)(data+18))[0] = htonl(newVal);
      set_32(data + 18, newVal);
    }
  }

  long unsigned int Page::getCRCChecksum() {
    //return ntohl(((long unsigned int*)(data+22))[0]);
    return get_32(data + 22);
  }

  void Page::setCRCChecksum(long unsigned int newVal) {
    if (checkDataSize(26)) {
      set_32(data + 22, newVal);
    }
  }

  char Page::getPageSegments() {
    return data[26];
  }

  inline void Page::setPageSegments(char newVal) {
    if (checkDataSize(26)) {
      data[26] = newVal;
    }
  }

  char * Page::getSegmentTable() {
    return data + 27;
  }

  std::deque<unsigned int> & Page::getSegmentTableDeque() {
    if (!segmentTableDeque.size()) {
      unsigned int temp = 0;
      for (unsigned int i = 0; i < getPageSegments(); i++) {
        temp += getSegmentTable()[i];
        if (getSegmentTable()[i] < 255) {
          segmentTableDeque.push_back(temp);
          temp = 0;
        }
      }
      if (temp != 0) {
        segmentTableDeque.push_back(temp);
      }
    }
    return segmentTableDeque;
  }

  static void STerrMSG() {
    DEBUG_MSG(DLVL_ERROR, "Segment too big, create a continue page");
  }

  bool Page::setSegmentTable(std::vector<unsigned int> layout) {
    dataSum = 0;
    for (unsigned int i = 0; i < layout.size(); i++) {
      dataSum += layout[i];
    }
    unsigned int place = 0;
    char table[256];
    for (unsigned int i = 0; i < layout.size(); i++) {
      int amount = (layout[i] / 255) + 1;
      if (i == layout.size() - 1 && place + amount > (255 + (layout[i] % 255 == 0))) {
        STerrMSG();
        return false;
      }
      memset(table + place, 255, amount - 1);
      table[place + amount - 1] = layout[i] % 255;
      place += amount;
    }
    //Don't send element 256, even if it was filled.
    if (place > 255) {
      place = 255;
    }
    setPageSegments(place);
    setSegmentTable(table, place);
    return true;
  }

  void Page::setSegmentTable(char * newVal, unsigned int length) {
    if (checkDataSize(27 + length)) {
      memcpy(data + 27, newVal, length);
    }
  }

  unsigned long int Page::getPageSize() {
    return 27 + getPageSegments() + dataSum;
  }

  char * Page::getPage() {
    return data;
  }

  char * Page::getFullPayload() {
    return data + 27 + getPageSegments();
  }

  void Page::setInternalCodec(std::string myCodec) {
    codec = myCodec;
  }

  std::string Page::toPrettyString(size_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "Ogg page (" << getPageSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << (int)getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Header type:";
    if (!getHeaderType()) {
      r << " Normal";
    } else {
      if (getHeaderType() & Continued) {
        r << " Continued";
      }
      if (getHeaderType() & BeginOfStream) {
        r << " BeginOfStream";
      }
      if (getHeaderType() & EndOfStream) {
        r << " EndOfStream";
      }
    }
    r << " (" << (int)getHeaderType() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Granule position: " << getGranulePosition() << std::endl;
    r << std::string(indent + 2, ' ') << "Bitstream number: " << getBitstreamSerialNumber() << std::endl;
    r << std::string(indent + 2, ' ') << "Sequence number: " << getPageSequenceNumber() << std::endl;
    r << std::string(indent + 2, ' ') << "Checksum: " << std::hex << getCRCChecksum() << std::dec << std::endl;
    //r << "  Calced Checksum: " << std::hex << calcChecksum() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "Payloadsize: " << dataSum << std::endl;
    r << std::string(indent + 2, ' ') << (int)getPageSegments() << " segments:" << std::endl;
    r << std::string(indent + 3, ' ');
    std::deque<unsigned int> temp = getSegmentTableDeque();
    for (std::deque<unsigned int>::iterator i = temp.begin(); i != temp.end(); i++) {
      r << " " << (*i);
    }
    r << std::endl;
    return r.str();
  }


  long unsigned int Page::calcChecksum() {
    long unsigned int retVal = 0;
    long unsigned int oldChecksum = getCRCChecksum();
    setCRCChecksum(0);
    retVal = checksum::crc32(0, data, getPageSize());
    setCRCChecksum(oldChecksum);
    return retVal;
  }

  inline bool Page::checkDataSize(unsigned int size) {
    if (size > datasize) {
      void * tmp = realloc(data, size);
      if (tmp) {
        data = (char *)tmp;
        datasize = size;
        return true;
      } else {
        return false;
      }
    } else {
      return true;
    }
  }

  int Page::getPayloadSize() {
    return dataSum;
  }

  bool Page::clear() {
    if (!checkDataSize(27)) { //check if size available in memory
      return false;
    }
    memset(data, 0, 27);
    dataSum = 0;
    codec = "";
    setMagicNumber();
    segmentTableDeque.clear();
    return true;
  }

  bool Page::setPayload(char * newData, unsigned int length) {
    if (!checkDataSize(27 + getPageSegments() + length)) { //check if size available in memory
      return false;
    }
    memcpy(data + 27 + getPageSegments(), newData, length);
    return true;
  }

  void Page::readDTSCVector(std::vector <JSON::Value> DTSCVec, unsigned int serial, unsigned int sequence) {
    clear();
    setVersion();
    if (DTSCVec[0]["OggCont"]) {//if it is a continue page, also for granule=0xFFFFFFFF
      setHeaderType(1);//headertype 1 = Continue Page
    } else if (DTSCVec[0]["OggEOS"]) {
      setHeaderType(4);//headertype 4 = end of stream
    } else {
      setHeaderType(0);//headertype 0 = normal
    }
    setGranulePosition(DTSCVec[0]["granule"].asInt());
    for (unsigned int i = 1; i < DTSCVec.size(); i++) {
      if (DTSCVec[0]["granule"].asInt() != DTSCVec[i]["granule"].asInt()) {
        DEBUG_MSG(DLVL_WARN, "Granule inconcistency!! %u != %u", (unsigned int)DTSCVec[0]["granule"].asInt(), (unsigned int)DTSCVec[i]["granule"].asInt());
      }
      if (DTSCVec[0]["trackid"].asInt() != DTSCVec[i]["trackid"].asInt()) {
        DEBUG_MSG(DLVL_WARN, "Track ID inconcistency!! %u != %u", (unsigned int)DTSCVec[0]["trackid"].asInt(), (unsigned int)DTSCVec[i]["trackid"].asInt());
      }
    }
    setBitstreamSerialNumber(serial);
    setPageSequenceNumber(sequence);

    std::vector<unsigned int> curSegTable;
    std::string pageBuffer;

    for (unsigned int i = 0; i < DTSCVec.size(); i++) {
      curSegTable.push_back(DTSCVec[i]["data"].asString().size());
      pageBuffer += DTSCVec[i]["data"].asString();
    }
    setSegmentTable(curSegTable);
    setPayload((char *)pageBuffer.c_str(), pageBuffer.size());
    setCRCChecksum(calcChecksum());
  }

  void headerPages::readDTSCHeader(DTSC::Meta & meta) {
    //pages.clear();
    parsedPages = "";
    Page curOggPage;
    srand(Util::getMS()); //randomising with milliseconds from boot
    std::vector<unsigned int> curSegTable;
    DTSCID2OGGSerial.clear();
    DTSCID2seqNum.clear();
    //Creating ID headers for theora and vorbis
    for (std::map<int, DTSC::Track>::iterator it = meta.tracks.begin(); it != meta.tracks.end(); it ++) {
      curOggPage.clear();
      curOggPage.setVersion();
      curOggPage.setHeaderType(2);//headertype 2 = Begin of Stream
      curOggPage.setGranulePosition(0);
      DTSCID2OGGSerial[it->second.trackID] = rand() % 0xFFFFFFFE + 1; //initialising on a random not 0 number
      curOggPage.setBitstreamSerialNumber(DTSCID2OGGSerial[it->second.trackID]);
      DTSCID2seqNum[it->second.trackID] = 0;
      curOggPage.setPageSequenceNumber(DTSCID2seqNum[it->second.trackID]++);
      curSegTable.clear();
      curSegTable.push_back(it->second.idHeader.size());
      curOggPage.setSegmentTable(curSegTable);
      curOggPage.setPayload((char *)it->second.idHeader.c_str(), it->second.idHeader.size());
      curOggPage.setCRCChecksum(curOggPage.calcChecksum());
      //std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
      //pages.push_back(curOggPage);
      parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
    }
    //Creating remaining headers for theora and vorbis
    //for tracks in header
    //create standard page with comment (empty) en setup header(init)
    for (std::map<int, DTSC::Track>::iterator it = meta.tracks.begin(); it != meta.tracks.end(); it ++) {
      curOggPage.clear();
      curOggPage.setVersion();
      curOggPage.setHeaderType(0);//headertype 0 = normal
      curOggPage.setGranulePosition(0);
      curOggPage.setBitstreamSerialNumber(DTSCID2OGGSerial[it->second.trackID]);
      curOggPage.setPageSequenceNumber(DTSCID2seqNum[it->second.trackID]++);
      curSegTable.clear();
      curSegTable.push_back(it->second.commentHeader.size());
      curSegTable.push_back(it->second.init.size());
      curOggPage.setSegmentTable(curSegTable);
      std::string fullHeader = it->second.commentHeader + it->second.init;
      curOggPage.setPayload((char *)fullHeader.c_str(), fullHeader.size());
      curOggPage.setCRCChecksum(curOggPage.calcChecksum());
      //std::cout << std::string(curOggPage.getPage(), curOggPage.getPageSize());
      //pages.push_back(curOggPage);
      parsedPages += std::string(curOggPage.getPage(), curOggPage.getPageSize());
    }
  }
}
