#include "ogg.h"
#include "defines.h"
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <arpa/inet.h>
#include <iomanip>
#include <mist/bitstream.h>

namespace OGG {

  oggSegment::oggSegment(){
    isKeyframe = 0;
    frameNumber = 0;
    timeStamp = 0;
    framesSinceKeyFrame = 0;
  }

  std::deque<unsigned int> decodeXiphSize(char * data, size_t len){
    std::deque<unsigned int> res;
    res.push_back(0);
    for (int i = 0; i < len; i++){
      *res.rbegin() += data[i];
      if (data[i] != 0xFF){
        res.push_back(0);
      }
    }
    if (*res.rbegin() == 0){
      res.resize(res.size() - 1);
    }
    return res;
  }
  inline long long unsigned int get_64(char * data){
    long long unsigned int temp = 0;
    for (int i = 7; i >= 0; --i){
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }

  inline long unsigned int get_32(char * data){
    long unsigned int temp = 0;
    for (int i = 3; i >= 0; --i){
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }

  inline void set_64(char * data, long unsigned int val){
    for (int i = 0; i < 8; ++i){
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }


  inline void set_32(char * data, long unsigned int val){
    for (int i = 0; i < 4; ++i){
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }


  Page::Page(){
    framesSeen = 0;
    lastKeyFrame = 0;
    sampleRate = 0;
    firstSample = 0;
    pageSequenceNumber = 0;
    totalFrames = 0;
    memset(data, 0, 282);
  }

  Page::Page(const Page & rhs){
    framesSeen=rhs.framesSeen;
    pageSequenceNumber = rhs.pageSequenceNumber;    
    lastKeyFrame = rhs.lastKeyFrame;
    sampleRate = rhs.sampleRate;
    firstSample = rhs.firstSample;
    totalFrames= rhs.totalFrames;
    memcpy(data, rhs.data, 282);
    segments = rhs.segments;
  }

  void Page::operator = (const Page & rhs){
    framesSeen=rhs.framesSeen;
    pageSequenceNumber = rhs.pageSequenceNumber;    
    lastKeyFrame = rhs.lastKeyFrame;
    firstSample = rhs.firstSample;
    sampleRate = rhs.sampleRate;
    totalFrames= rhs.totalFrames;
    memcpy(data, rhs.data, 282);
    segments = rhs.segments;
  }

  unsigned int Page::calcPayloadSize(){
    unsigned int retVal = 0;
    for (int i = 0; i < segments.size(); i++){
      retVal += segments[i].size();
    }
    return retVal;
  }

  /// Reads an OGG Page from the source and if valid, removes it from source.
  bool Page::read(std::string & newData){
    int len = newData.size();
    segments.clear();
    if (newData.size() < 27){
      return false;
    }
    if (newData.substr(0, 4) != "OggS"){
      DEBUG_MSG(DLVL_FAIL, "Invalid Ogg page encountered (magic number wrong: %s) - cannot continue", newData.c_str());
      return false;
    }
    memcpy(data, newData.c_str(), 27);//copying the header, always 27 bytes
    if (newData.size() < 27u + getPageSegments()){ //check input size
      return false;
    }
    newData.erase(0, 27);
    memcpy(data + 27, newData.c_str(), getPageSegments());
    newData.erase(0, getPageSegments());
    std::deque<unsigned int> segSizes = decodeXiphSize(data + 27, getPageSegments());
    for (std::deque<unsigned int>::iterator it = segSizes.begin(); it != segSizes.end(); it++){
      segments.push_back(std::string(newData.data(), *it));
      newData.erase(0, *it);
    }
    INFO_MSG("Erased %lu bytes from the input", len - newData.size());
    return true;
  }


  bool Page::read(FILE * inFile){
    segments.clear();
    int oriPos = ftell(inFile);
    //INFO_MSG("pos: %d", oriPos);
    if (!fread(data, 27, 1, inFile)){
      fseek(inFile, oriPos, SEEK_SET);
      FAIL_MSG("failed to fread(data, 27, 1, inFile) @ pos %d", oriPos);
      return false;
    }
    if (std::string(data, 4) != "OggS"){
      DEBUG_MSG(DLVL_FAIL, "Invalid Ogg page encountered (magic number wrong: %s) - cannot continue bytePos %d", data, oriPos);
      return false;
    }
    if (!fread(data + 27, getPageSegments(), 1, inFile)){
      fseek(inFile, oriPos, SEEK_SET);
      FAIL_MSG("failed to fread(data + 27, getPageSegments() %d, 1, inFile) @ pos %d", getPageSegments(), oriPos);
      return false;
    }
    std::deque<unsigned int> segSizes = decodeXiphSize(data + 27, getPageSegments());
    for (std::deque<unsigned int>::iterator it = segSizes.begin(); it != segSizes.end(); it++){
      if (*it){
        char * thisSeg = (char *)malloc(*it * sizeof(char));
        if (!thisSeg){
          DEBUG_MSG(DLVL_WARN, "malloc failed");
        }
        if (!fread(thisSeg, *it, 1, inFile)){
          DEBUG_MSG(DLVL_WARN, "Unable to read a segment @ pos %d segment size: %d getPageSegments: %d", oriPos, *it, getPageSegments());
          fseek(inFile, oriPos, SEEK_SET);
          return false;
        }
        segments.push_back(std::string(thisSeg, *it));
        free(thisSeg);
      }else{
        segments.push_back(std::string("", 0));
      }

    }

    return true;
  }

  bool Page::getSegment(unsigned int index, std::string & ret){
    if (index >= segments.size()){
      ret.clear();
      return false;
    }
    ret = segments[index];
    return true;
  }

  const char * Page::getSegment(unsigned int index){
    if (index >= segments.size()){
      return 0;
    }
    return segments[index].data();
  }

  unsigned long Page::getSegmentLen(unsigned int index){
    if (index >= segments.size()){
      return 0;
    }
    return segments[index].size();
  }

  void Page::setMagicNumber(){
    memcpy(data, "OggS", 4);
  }

  char Page::getVersion(){
    return data[4];
  }

  void Page::setVersion(char newVal){
    data[4] = newVal;
  }

  char Page::getHeaderType(){
    return data[5];
  }

  void Page::setHeaderType(char newVal){
    data[5] = newVal;
  }

  long long unsigned int Page::getGranulePosition(){
    return get_64(data + 6);
  }

  void Page::setGranulePosition(long long unsigned int newVal){
    set_64(data + 6, newVal);
  }

  long unsigned int Page::getBitstreamSerialNumber(){
    //return ntohl(((long unsigned int*)(data+14))[0]);
    return get_32(data + 14);
  }

  void Page::setBitstreamSerialNumber(long unsigned int newVal){
    set_32(data + 14, newVal);
  }

  long unsigned int Page::getPageSequenceNumber(){
    return get_32(data + 18);
  }

  void Page::setPageSequenceNumber(long unsigned int newVal){
    set_32(data + 18, newVal);
  }

  long unsigned int Page::getCRCChecksum(){
    return get_32(data + 22);
  }

  void Page::setCRCChecksum(long unsigned int newVal){
    set_32(data + 22, newVal);
  }

  char Page::getPageSegments(){
    return data[26];
  }

  inline void Page::setPageSegments(char newVal){
    data[26] = newVal;
  }

  bool Page::verifyChecksum(){
    if (getCRCChecksum() == calcChecksum()){ //NOTE: calcChecksum is currently not functional (it will always return 0)
      return true;
    } else {
      return false;
    }
  }

  bool Page::possiblyContinued(){
    if (getPageSegments() == 255){
      if (data[281] == 255){
        return true;
      }
    }
    return false;
  }

  std::string Page::toPrettyString(size_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "Ogg page" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << (int)getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Header type:";
    if (!getHeaderType()){
      r << " Normal";
    } else {
      if (getHeaderType() & Continued){
        r << " Continued";
      }
      if (getHeaderType() & BeginOfStream){
        r << " BeginOfStream";
      }
      if (getHeaderType() & EndOfStream){
        r << " EndOfStream";
      }
    }
    r << " (" << (int)getHeaderType() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Granule position: " << std::hex << std::setw(16) << std::setfill('0') << getGranulePosition() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "Bitstream number: " << getBitstreamSerialNumber() << std::endl;
    r << std::string(indent + 2, ' ') << "Sequence number: " << getPageSequenceNumber() << std::endl;
    r << std::string(indent + 2, ' ') << "Checksum Correct: " <<  verifyChecksum() << std::endl;
    //r << "  Calced Checksum: " << std::hex << calcChecksum() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "Checksum: " << getCRCChecksum() << std::endl;
    r << std::string(indent + 2, ' ') << (int)getPageSegments() << " segments:" << std::endl;
    r << std::string(indent + 3, ' ');
    for (int i = 0; i < segments.size(); i++){
      r << " " << segments[i].size();
    }
    r << std::endl;
    return r.str();
  }

  inline unsigned int crc32(unsigned int crc, const char * data, size_t len){
    static const unsigned int table[256] = {
      0x00000000U, 0x04C11DB7U, 0x09823B6EU, 0x0D4326D9U,
      0x130476DCU, 0x17C56B6BU, 0x1A864DB2U, 0x1E475005U,
      0x2608EDB8U, 0x22C9F00FU, 0x2F8AD6D6U, 0x2B4BCB61U,
      0x350C9B64U, 0x31CD86D3U, 0x3C8EA00AU, 0x384FBDBDU,
      0x4C11DB70U, 0x48D0C6C7U, 0x4593E01EU, 0x4152FDA9U,
      0x5F15ADACU, 0x5BD4B01BU, 0x569796C2U, 0x52568B75U,
      0x6A1936C8U, 0x6ED82B7FU, 0x639B0DA6U, 0x675A1011U,
      0x791D4014U, 0x7DDC5DA3U, 0x709F7B7AU, 0x745E66CDU,
      0x9823B6E0U, 0x9CE2AB57U, 0x91A18D8EU, 0x95609039U,
      0x8B27C03CU, 0x8FE6DD8BU, 0x82A5FB52U, 0x8664E6E5U,
      0xBE2B5B58U, 0xBAEA46EFU, 0xB7A96036U, 0xB3687D81U,
      0xAD2F2D84U, 0xA9EE3033U, 0xA4AD16EAU, 0xA06C0B5DU,
      0xD4326D90U, 0xD0F37027U, 0xDDB056FEU, 0xD9714B49U,
      0xC7361B4CU, 0xC3F706FBU, 0xCEB42022U, 0xCA753D95U,
      0xF23A8028U, 0xF6FB9D9FU, 0xFBB8BB46U, 0xFF79A6F1U,
      0xE13EF6F4U, 0xE5FFEB43U, 0xE8BCCD9AU, 0xEC7DD02DU,
      0x34867077U, 0x30476DC0U, 0x3D044B19U, 0x39C556AEU,
      0x278206ABU, 0x23431B1CU, 0x2E003DC5U, 0x2AC12072U,
      0x128E9DCFU, 0x164F8078U, 0x1B0CA6A1U, 0x1FCDBB16U,
      0x018AEB13U, 0x054BF6A4U, 0x0808D07DU, 0x0CC9CDCAU,
      0x7897AB07U, 0x7C56B6B0U, 0x71159069U, 0x75D48DDEU,
      0x6B93DDDBU, 0x6F52C06CU, 0x6211E6B5U, 0x66D0FB02U,
      0x5E9F46BFU, 0x5A5E5B08U, 0x571D7DD1U, 0x53DC6066U,
      0x4D9B3063U, 0x495A2DD4U, 0x44190B0DU, 0x40D816BAU,
      0xACA5C697U, 0xA864DB20U, 0xA527FDF9U, 0xA1E6E04EU,
      0xBFA1B04BU, 0xBB60ADFCU, 0xB6238B25U, 0xB2E29692U,
      0x8AAD2B2FU, 0x8E6C3698U, 0x832F1041U, 0x87EE0DF6U,
      0x99A95DF3U, 0x9D684044U, 0x902B669DU, 0x94EA7B2AU,
      0xE0B41DE7U, 0xE4750050U, 0xE9362689U, 0xEDF73B3EU,
      0xF3B06B3BU, 0xF771768CU, 0xFA325055U, 0xFEF34DE2U,
      0xC6BCF05FU, 0xC27DEDE8U, 0xCF3ECB31U, 0xCBFFD686U,
      0xD5B88683U, 0xD1799B34U, 0xDC3ABDEDU, 0xD8FBA05AU,
      0x690CE0EEU, 0x6DCDFD59U, 0x608EDB80U, 0x644FC637U,
      0x7A089632U, 0x7EC98B85U, 0x738AAD5CU, 0x774BB0EBU,
      0x4F040D56U, 0x4BC510E1U, 0x46863638U, 0x42472B8FU,
      0x5C007B8AU, 0x58C1663DU, 0x558240E4U, 0x51435D53U,
      0x251D3B9EU, 0x21DC2629U, 0x2C9F00F0U, 0x285E1D47U,
      0x36194D42U, 0x32D850F5U, 0x3F9B762CU, 0x3B5A6B9BU,
      0x0315D626U, 0x07D4CB91U, 0x0A97ED48U, 0x0E56F0FFU,
      0x1011A0FAU, 0x14D0BD4DU, 0x19939B94U, 0x1D528623U,
      0xF12F560EU, 0xF5EE4BB9U, 0xF8AD6D60U, 0xFC6C70D7U,
      0xE22B20D2U, 0xE6EA3D65U, 0xEBA91BBCU, 0xEF68060BU,
      0xD727BBB6U, 0xD3E6A601U, 0xDEA580D8U, 0xDA649D6FU,
      0xC423CD6AU, 0xC0E2D0DDU, 0xCDA1F604U, 0xC960EBB3U,
      0xBD3E8D7EU, 0xB9FF90C9U, 0xB4BCB610U, 0xB07DABA7U,
      0xAE3AFBA2U, 0xAAFBE615U, 0xA7B8C0CCU, 0xA379DD7BU,
      0x9B3660C6U, 0x9FF77D71U, 0x92B45BA8U, 0x9675461FU,
      0x8832161AU, 0x8CF30BADU, 0x81B02D74U, 0x857130C3U,
      0x5D8A9099U, 0x594B8D2EU, 0x5408ABF7U, 0x50C9B640U,
      0x4E8EE645U, 0x4A4FFBF2U, 0x470CDD2BU, 0x43CDC09CU,
      0x7B827D21U, 0x7F436096U, 0x7200464FU, 0x76C15BF8U,
      0x68860BFDU, 0x6C47164AU, 0x61043093U, 0x65C52D24U,
      0x119B4BE9U, 0x155A565EU, 0x18197087U, 0x1CD86D30U,
      0x029F3D35U, 0x065E2082U, 0x0B1D065BU, 0x0FDC1BECU,
      0x3793A651U, 0x3352BBE6U, 0x3E119D3FU, 0x3AD08088U,
      0x2497D08DU, 0x2056CD3AU, 0x2D15EBE3U, 0x29D4F654U,
      0xC5A92679U, 0xC1683BCEU, 0xCC2B1D17U, 0xC8EA00A0U,
      0xD6AD50A5U, 0xD26C4D12U, 0xDF2F6BCBU, 0xDBEE767CU,
      0xE3A1CBC1U, 0xE760D676U, 0xEA23F0AFU, 0xEEE2ED18U,
      0xF0A5BD1DU, 0xF464A0AAU, 0xF9278673U, 0xFDE69BC4U,
      0x89B8FD09U, 0x8D79E0BEU, 0x803AC667U, 0x84FBDBD0U,
      0x9ABC8BD5U, 0x9E7D9662U, 0x933EB0BBU, 0x97FFAD0CU,
      0xAFB010B1U, 0xAB710D06U, 0xA6322BDFU, 0xA2F33668U,
      0xBCB4666DU, 0xB8757BDAU, 0xB5365D03U, 0xB1F740B4U,
    };

    while (len > 0){
      crc = table[*data ^ ((crc >> 24) & 0xff)] ^ (crc << 8);
      data++;
      len--;
    }
    return crc;
  }

  long unsigned int Page::calcChecksum(){ //implement in sending out page, probably delete this -- probably don't delete this because this function appears to be in use
    long unsigned int retVal = 0;
    /*
    long unsigned int oldChecksum = getCRCChecksum();
    std::string fullPayload;
    for (int i = 0; i < segments.size(); i++){
      fullPayload += segments[i];
    }
    setCRCChecksum (0);
    retVal = crc32(
      crc32(0, data, 27 + getPageSegments()),//checksum over pageheader
      fullPayload.data(),
      fullPayload.size()
    );//checksum over content
    setCRCChecksum (oldChecksum);
    */
    return retVal;
  }

  int Page::getPayloadSize(){
    size_t res = 0;
    for (int i = 0; i < segments.size(); i++){
      res += segments[i].size();
    }
    return res;
  }

  const std::deque<std::string> & Page::getAllSegments(){
    return segments;
  }

  void Page::prepareNext(bool continueMe){
    clear(0, -1, getBitstreamSerialNumber(), getPageSequenceNumber() + 1);
    if (continueMe){ //noting that the page will be continued
      setHeaderType(OGG::Continued);
    }
  }

  void Page::clear(char HeaderType, long long unsigned int GranPos, long unsigned int BSN, long unsigned int PSN){
    memset(data, 0, 27);
    setMagicNumber();
    setVersion();
    setHeaderType(HeaderType);
    setGranulePosition(GranPos);
    setBitstreamSerialNumber(BSN);
    setPageSequenceNumber(PSN);
  }


  unsigned int Page::addSegment(const std::string & payload){ //returns added bytes
    segments.push_back(payload);
    return payload.size();
  }

  unsigned int Page::addSegment(const char * content, unsigned int length){
    segments.push_back(std::string(content, length));
    return length;
  }

  unsigned int Page::overFlow(){ //returns the amount of bytes that don't fit in this page from the segments;
    unsigned int retVal = 0;
    unsigned int curSegNum = 0;//the current segment number we are looking at
    unsigned int segAmount = 0;
    for (unsigned int i = 0; i < segments.size(); i++){
      segAmount = (segments[i].size() / 255) + 1;
      if (segAmount + curSegNum > 255){
        retVal += ((segAmount - (255 - curSegNum + 1)) * 255) + (segments[i].size() % 255);//calculate the extra bytes that overflow
        curSegNum = 255;//making sure the segment numbers are at maximum
      } else {
        curSegNum += segAmount;
      }
    }
    return retVal;
  }

  void Page::vorbisStuff(){
    Utils::bitstreamLSBF packet;    
    packet.append(oggSegments.rbegin()->dataString);//this is a heavy operation, it needs to be optimised //todo?
    int curPCMSamples = 0;
    long long unsigned int packetType = packet.get(1);
    if (packetType == 0){
      int tempModes = vorbis::ilog(vorbisModes.size() - 1);
      int tempPacket = packet.get(tempModes);
      int curBlockFlag = vorbisModes[tempPacket].blockFlag;
      curPCMSamples = (1 << blockSize[curBlockFlag]);
      if (prevBlockFlag != -1){
        if (curBlockFlag == prevBlockFlag){
          curPCMSamples /= 2;
        } else {
          curPCMSamples -= (1 << blockSize[0]) / 4 + (1 << blockSize[1]) / 4;
        }
      }
      prevBlockFlag = curBlockFlag;
    } else {
      ERROR_MSG("Error, Vorbis packet type !=0 actual type: %llu",packetType );
    }
    //add to granule position
    lastKeyFrame += curPCMSamples;
    oggSegments.rbegin()->lastKeyFrameSeen = lastKeyFrame;
  }

  ///this calculates the granule position for a DTSC packet
  long long unsigned int Page::calculateGranule(oggSegment & currentSegment){
    long long unsigned int tempGranule = 0;
    if (codec == OGG::THEORA){
      tempGranule = (currentSegment.lastKeyFrameSeen << split) | currentSegment.framesSinceKeyFrame;
    } else if (codec == OGG::VORBIS){
      tempGranule = currentSegment.lastKeyFrameSeen;
    }
    return tempGranule;
  }


  bool Page::shouldSend(){
    unsigned int totalSegmentSize = 0;
    if (!oggSegments.size()){
      return false;
    }
    if (oggSegments.rbegin()->isKeyframe){
      return true;
    }
    if (codec == OGG::VORBIS){
      if (lastKeyFrame - firstSample >= sampleRate){
        return true;
      }
    }

    for (unsigned int i = 0; i < oggSegments.size(); i++){
      totalSegmentSize += (oggSegments[i].dataString.size() / 255) + 1;
    }
    if (totalSegmentSize >= 255) return true;

    return false;
  }

  ///\todo Rewrite this
  void Page::sendTo(Socket::Connection & destination, int calcGranule){ //combines all data and sends it to socket
    if (!oggSegments.size()){
      DEBUG_MSG(DLVL_HIGH, "!segments.size()");
      return;
    }
    if (codec == OGG::VORBIS){
      firstSample = lastKeyFrame;
    }
    int temp = 0;
    long unsigned int checksum = 0; //reset checksum
    setCRCChecksum(0);
    unsigned int numSegments = oggSegments.size();
    int tableIndex = 0;
    char tableSize = 0;
    //char table[255];
    char * table = (char *)malloc(255);
    int bytesLeft = 0;
    for (unsigned int i = 0; i < numSegments; i++){
      //calculate amount of 255 bytes needed to store size (remainder not counted)
      temp = (oggSegments[i].dataString.size() / 255);
      //if everything still fits in the table
      if ((temp + tableIndex + 1) <= 255){
        //set the 255 bytes
        memset(table + tableIndex, 255, temp);
        //increment tableIndex with 255 byte count
        tableIndex += temp;
        //set the last table entry to the remainder, and increase tableIndex with one
        table[tableIndex++] = (oggSegments[i].dataString.size() % 255);
        //update tableSize to be equal to tableIndex
        tableSize = tableIndex;
        bytesLeft = 0;
      } else {
        //stuff doesn't fit
        //set the rest of the table to 255s
        memset(table + tableIndex, 255, (255 - tableIndex));
        //table size is full table in use
        tableSize = 255;
        //space left on current page, for this segment: (255-tableIndex)*255
        bytesLeft = (255 - tableIndex) * 255;
        if (oggSegments[i].dataString.size() == bytesLeft){
          bytesLeft = 0; //segment barely fits.
        }
        break;
      }
    }

    if (calcGranule < -1){
      if (numSegments == 1 && bytesLeft){ //no segment ends on this page.
        granules = -1;
      } else {
        unsigned int tempIndex = numSegments - 1;
        if (bytesLeft != 0){
          tempIndex = numSegments - 2;
        }
        granules = calculateGranule(oggSegments[tempIndex]);
      }
    } else {
      granules = calcGranule; //force granule
    }
    setGranulePosition(granules);

    checksum = crc32(checksum, data, 22);//calculating the checksum over the first part of the page
    checksum = crc32(checksum, &tableSize, 1); //calculating the checksum over the segment Table Size
    checksum = crc32(checksum, table, tableSize);//calculating the checksum over the segment Table

    DEBUG_MSG(DLVL_DONTEVEN, "numSegments: %d", numSegments);

    for (unsigned int i = 0; i < numSegments; i++){
      //INFO_MSG("checksum, i: %d", i);
      if (bytesLeft != 0 && ((i + 1) == numSegments)){
        checksum = crc32(checksum, oggSegments[i].dataString.data(), bytesLeft);
        //take only part of this segment
      } else { //take the entire segment
        checksum = crc32(checksum, oggSegments[i].dataString.data(), oggSegments[i].dataString.size());
      }
    }

    setCRCChecksum(checksum);

    destination.SendNow(data, 26);
    destination.SendNow(&tableSize, 1);
    destination.SendNow(table, tableSize);


    for (unsigned int i = 0; i < numSegments; i++){
      if (bytesLeft != 0 && ((i + 1) == numSegments)){
        destination.SendNow(oggSegments.begin()->dataString.data(), bytesLeft);
        oggSegments.begin()->dataString.erase(0, bytesLeft);
        setHeaderType(OGG::Continued);//set continuation flag
        break;
        //for loop WILL exit after this.
      } else {
        destination.SendNow(oggSegments.begin()->dataString.data(), oggSegments.begin()->dataString.size());
        //this segment WILL be deleted
        oggSegments.erase(oggSegments.begin());
        setHeaderType(OGG::Plain);//not a continuation
      }

    }

    //done sending, assume start of new page.
    //inc. page num, write to header
    pageSequenceNumber++;
    setPageSequenceNumber(pageSequenceNumber);
    //granule still requires setting!
    free(table);
    return;


  }
}




