#include "ogg.h"
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <arpa/inet.h>

namespace OGG{
  inline long long unsigned int get_64(char* data){
    long long unsigned int temp = 0;
    for (int i = 7; i>= 0; --i){
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }
  
  inline long unsigned int get_32(char* data){
    long unsigned int temp = 0;
    for (int i = 3; i>= 0; --i){
      temp <<= 8;
      temp += data[i];
    }
    return temp;
  }

  inline void set_64(char* data, long unsigned int val){
    for (int i = 0; i< 8; ++i){
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }


  inline void set_32(char* data, long unsigned int val){
    for (int i = 0; i< 4; ++i){
      data[i] = val & 0xFF;
      val >>= 8;
    }
  }

  
  Page::Page(){
    data = NULL;
    datasize = 0;
    dataSum = 0;
  }

  bool Page::read(std::string & newData){
    segmentTableDeque.clear();
    //datasize = 0;
    if (newData.size()<27){
      return false;
    }
    /*if (getMagicNumber() != 0x4f676753){
      return false;
    }*/
    dataSum = 0;
    if (!checkDataSize(27)){
      return false;
    }
    memcpy(data, newData.c_str(), 27);//copying the header, always 27 bytes
    
    if (newData.size() < 27 + getPageSegments()){//check input size
      return false;
    }
    if(!checkDataSize(27 + getPageSegments())){//check if size available in memory
      return false;
    }
    memcpy(data + 27, newData.c_str() + 27, getPageSegments()); 
    //copying the first part of the page into data, which tells the size of the page
    
    for(unsigned int i = 0; i < getPageSegments(); i++){
      dataSum += getSegmentTable()[i];
    }
    
    if (newData.size() < 27 + getPageSegments() + dataSum){//check input size
      return false;
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
      //return ntohl(((long unsigned*)(data+6))[1]) & ((long long unsigned)((long long unsigned)ntohl(((long unsigned*)(data+6))[0]) << 32));
      //long long unsigned int temp;
      //temp = ((long unsigned int)(data+6)[0]);
      //temp = temp << 32 + ((long unsigned int)(data+6)[1]);
      return get_64(data+6);
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
    return get_32(data+14);
  }

  void Page::setBitstreamSerialNumber(long unsigned int newVal){
    if(checkDataSize(18)){
      //((long unsigned *)(data+14))[0] = htonl(newVal);
      set_32(data+14, newVal);
    }
  }
  
  long unsigned int Page::getPageSequenceNumber(){
    return get_32(data+18);
  }

  void Page::setPageSequenceNumber(long unsigned int newVal){
    if(checkDataSize(22)){
      //((long unsigned *)(data+18))[0] = htonl(newVal);
      set_32(data+18, newVal);
    }
  }
  
  long unsigned int Page::getCRCChecksum(){
    //return ntohl(((long unsigned int*)(data+22))[0]);
    return get_32(data+22);
  }
  
  void Page::setCRCChecksum(long unsigned int newVal){
    if(checkDataSize(26)){
      set_32(data+22,newVal);
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
  
  std::deque<unsigned int> & Page::getSegmentTableDeque(){
    if ( !segmentTableDeque.size()){
      unsigned int temp = 0;
      for (unsigned int i = 0; i < getPageSegments(); i++){
        temp += getSegmentTable()[i];
        if (getSegmentTable()[i] < 255){
          segmentTableDeque.push_back(temp);
          temp = 0;
        }
      }
    }
    return segmentTableDeque;
  }

  static void STerrMSG(){
    std::cerr << "Segments too big, create a continue page" << std::endl;
  }

  bool Page::setSegmentTable(std::vector<unsigned int> layout){
    unsigned int place = 0;
    char table[255];
    for (unsigned int i = 0; i < layout.size(); i++){
      while (layout[i]>=255){
        if (place >= 255){ 
          STerrMSG();
          return false;
        }
        table[place] = 255;
        layout[i] -= 255;
        place++;
      }
      if (place >= 255){ 
        STerrMSG();
        return false;
      }
      table[place] = layout[i];
      place++;
    }
    setSegmentTable(table,place);
    dataSum=0;
    for (unsigned int i = 0; i < layout.size(); i++){
      dataSum += layout[i];
    }
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

  void Page::setInternalCodec(std::string myCodec){
    codec = myCodec;
  }
  
  std::string Page::toPrettyString(size_t indent){
    std::stringstream r;
    r << std::string(indent,' ') << "OGG Page (" << getPageSize() << ")" << std::endl;
    r << std::string(indent + 2,' ') << "Magic Number: " << std::string(data, 4) << std::endl;
    r << std::string(indent + 2,' ') << "Version: " << (int)getVersion() << std::endl;
    r << std::string(indent + 2,' ') << "Headertype: " << std::hex << (int)getHeaderType() << std::dec;
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
    r << std::string(indent + 2,' ') << "Granule Position: " << std::hex << getGranulePosition() << std::dec << std::endl;
    r << std::string(indent + 2,' ') << "Bitstream Number: " << getBitstreamSerialNumber() << std::endl;
    r << std::string(indent + 2,' ') << "Sequence Number: " << getPageSequenceNumber() << std::endl;
    r << std::string(indent + 2,' ') << "Checksum: " << std::hex << getCRCChecksum() << std::dec << std::endl;
    //r << "  Calced Checksum: " << std::hex << calcChecksum() << std::dec << std::endl;
    //r << "CRC_checksum write: " << std::hex << getCRCChecksum()<< std::dec << std::endl;
    r << std::string(indent + 2,' ') << "Segments: " << (int)getPageSegments() << std::endl;
    std::deque<unsigned int> temp = getSegmentTableDeque();
    for (std::deque<unsigned int>::iterator i = temp.begin(); i != temp.end(); i++){
      r << std::string(indent + 4,' ') << (*i) << std::endl;
    }
    r << std::string(indent + 2,' ') << "Payloadsize: " << dataSum << std::endl;
    if (codec == "theora"){
      int offset = 0;
      for (int i = 0; i < getSegmentTableDeque().size(); i++){
        theora::header tmpHeader;
        int len = getSegmentTableDeque()[i];
        if (tmpHeader.read(getFullPayload()+offset,len)){
          r << tmpHeader.toPrettyString(indent + 4);
        }
        theora::frame tmpFrame;
        if (tmpFrame.read(getFullPayload()+offset,len)){
          r << tmpFrame.toPrettyString(indent + 4);
        }
        offset += len;
      }
    }else if(codec == "vorbis"){
      r << "Vorbis Data" << std::endl;
      int offset = 0;
      for (int i = 0; i < getSegmentTableDeque().size(); i++){
        vorbis::header tmpHeader;
        int len = getSegmentTableDeque()[i];
        if (tmpHeader.read(getFullPayload()+offset,len)){
          r << tmpHeader.toPrettyString(indent + 4);
        }
        offset += len;
      }
    }
    return r.str();
  }
  
  inline unsigned int crc32(unsigned int crc, const char *data, size_t len){
    static const unsigned int table[256] = {
    0x00000000U,0x04C11DB7U,0x09823B6EU,0x0D4326D9U,
    0x130476DCU,0x17C56B6BU,0x1A864DB2U,0x1E475005U,
    0x2608EDB8U,0x22C9F00FU,0x2F8AD6D6U,0x2B4BCB61U,
    0x350C9B64U,0x31CD86D3U,0x3C8EA00AU,0x384FBDBDU,
    0x4C11DB70U,0x48D0C6C7U,0x4593E01EU,0x4152FDA9U,
    0x5F15ADACU,0x5BD4B01BU,0x569796C2U,0x52568B75U,
    0x6A1936C8U,0x6ED82B7FU,0x639B0DA6U,0x675A1011U,
    0x791D4014U,0x7DDC5DA3U,0x709F7B7AU,0x745E66CDU,
    0x9823B6E0U,0x9CE2AB57U,0x91A18D8EU,0x95609039U,
    0x8B27C03CU,0x8FE6DD8BU,0x82A5FB52U,0x8664E6E5U,
    0xBE2B5B58U,0xBAEA46EFU,0xB7A96036U,0xB3687D81U,
    0xAD2F2D84U,0xA9EE3033U,0xA4AD16EAU,0xA06C0B5DU,
    0xD4326D90U,0xD0F37027U,0xDDB056FEU,0xD9714B49U,
    0xC7361B4CU,0xC3F706FBU,0xCEB42022U,0xCA753D95U,
    0xF23A8028U,0xF6FB9D9FU,0xFBB8BB46U,0xFF79A6F1U,
    0xE13EF6F4U,0xE5FFEB43U,0xE8BCCD9AU,0xEC7DD02DU,
    0x34867077U,0x30476DC0U,0x3D044B19U,0x39C556AEU,
    0x278206ABU,0x23431B1CU,0x2E003DC5U,0x2AC12072U,
    0x128E9DCFU,0x164F8078U,0x1B0CA6A1U,0x1FCDBB16U,
    0x018AEB13U,0x054BF6A4U,0x0808D07DU,0x0CC9CDCAU,
    0x7897AB07U,0x7C56B6B0U,0x71159069U,0x75D48DDEU,
    0x6B93DDDBU,0x6F52C06CU,0x6211E6B5U,0x66D0FB02U,
    0x5E9F46BFU,0x5A5E5B08U,0x571D7DD1U,0x53DC6066U,
    0x4D9B3063U,0x495A2DD4U,0x44190B0DU,0x40D816BAU,
    0xACA5C697U,0xA864DB20U,0xA527FDF9U,0xA1E6E04EU,
    0xBFA1B04BU,0xBB60ADFCU,0xB6238B25U,0xB2E29692U,
    0x8AAD2B2FU,0x8E6C3698U,0x832F1041U,0x87EE0DF6U,
    0x99A95DF3U,0x9D684044U,0x902B669DU,0x94EA7B2AU,
    0xE0B41DE7U,0xE4750050U,0xE9362689U,0xEDF73B3EU,
    0xF3B06B3BU,0xF771768CU,0xFA325055U,0xFEF34DE2U,
    0xC6BCF05FU,0xC27DEDE8U,0xCF3ECB31U,0xCBFFD686U,
    0xD5B88683U,0xD1799B34U,0xDC3ABDEDU,0xD8FBA05AU,
    0x690CE0EEU,0x6DCDFD59U,0x608EDB80U,0x644FC637U,
    0x7A089632U,0x7EC98B85U,0x738AAD5CU,0x774BB0EBU,
    0x4F040D56U,0x4BC510E1U,0x46863638U,0x42472B8FU,
    0x5C007B8AU,0x58C1663DU,0x558240E4U,0x51435D53U,
    0x251D3B9EU,0x21DC2629U,0x2C9F00F0U,0x285E1D47U,
    0x36194D42U,0x32D850F5U,0x3F9B762CU,0x3B5A6B9BU,
    0x0315D626U,0x07D4CB91U,0x0A97ED48U,0x0E56F0FFU,
    0x1011A0FAU,0x14D0BD4DU,0x19939B94U,0x1D528623U,
    0xF12F560EU,0xF5EE4BB9U,0xF8AD6D60U,0xFC6C70D7U,
    0xE22B20D2U,0xE6EA3D65U,0xEBA91BBCU,0xEF68060BU,
    0xD727BBB6U,0xD3E6A601U,0xDEA580D8U,0xDA649D6FU,
    0xC423CD6AU,0xC0E2D0DDU,0xCDA1F604U,0xC960EBB3U,
    0xBD3E8D7EU,0xB9FF90C9U,0xB4BCB610U,0xB07DABA7U,
    0xAE3AFBA2U,0xAAFBE615U,0xA7B8C0CCU,0xA379DD7BU,
    0x9B3660C6U,0x9FF77D71U,0x92B45BA8U,0x9675461FU,
    0x8832161AU,0x8CF30BADU,0x81B02D74U,0x857130C3U,
    0x5D8A9099U,0x594B8D2EU,0x5408ABF7U,0x50C9B640U,
    0x4E8EE645U,0x4A4FFBF2U,0x470CDD2BU,0x43CDC09CU,
    0x7B827D21U,0x7F436096U,0x7200464FU,0x76C15BF8U,
    0x68860BFDU,0x6C47164AU,0x61043093U,0x65C52D24U,
    0x119B4BE9U,0x155A565EU,0x18197087U,0x1CD86D30U,
    0x029F3D35U,0x065E2082U,0x0B1D065BU,0x0FDC1BECU,
    0x3793A651U,0x3352BBE6U,0x3E119D3FU,0x3AD08088U,
    0x2497D08DU,0x2056CD3AU,0x2D15EBE3U,0x29D4F654U,
    0xC5A92679U,0xC1683BCEU,0xCC2B1D17U,0xC8EA00A0U,
    0xD6AD50A5U,0xD26C4D12U,0xDF2F6BCBU,0xDBEE767CU,
    0xE3A1CBC1U,0xE760D676U,0xEA23F0AFU,0xEEE2ED18U,
    0xF0A5BD1DU,0xF464A0AAU,0xF9278673U,0xFDE69BC4U,
    0x89B8FD09U,0x8D79E0BEU,0x803AC667U,0x84FBDBD0U,
    0x9ABC8BD5U,0x9E7D9662U,0x933EB0BBU,0x97FFAD0CU,
    0xAFB010B1U,0xAB710D06U,0xA6322BDFU,0xA2F33668U,
    0xBCB4666DU,0xB8757BDAU,0xB5365D03U,0xB1F740B4U,
    };
  
    while (len > 0)
    {
      crc = table[*data ^ ((crc >> 24) & 0xff)] ^ (crc << 8);
      data++;
      len--;
    }
    return crc;
  }

  long unsigned int Page::calcChecksum(){
    long unsigned int retVal = 0;
    long unsigned int oldChecksum = getCRCChecksum();
    setCRCChecksum (0);
    retVal = crc32(0, data, getPageSize());
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

  int Page::getPayloadSize(){
    return dataSum;
  }
  
  void Page::clear(){
    memset(data,0,27);
    datasize = 0;
    dataSum = 0;
    codec = "";
    segmentTableDeque.clear();
  }

  bool Page::setPayload(char* newData, unsigned int length){
     if(!checkDataSize(27 + getPageSegments() + length)){//check if size available in memory
      return false;
    }
    memcpy(data + 27 + getPageSegments(), newData, length);
    return true;
 }
}
