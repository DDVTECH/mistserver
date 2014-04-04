#include "mp4_encryption.h"

namespace MP4 {

  UUID_SampleEncryption::UUID_SampleEncryption(){
    setUUID((std::string)"a2394f52-5a9b-4f14-a244-6c427c648df4");
  }

  void UUID_SampleEncryption::setVersion(uint32_t newVersion){
    setInt8(newVersion, 16);
  }
  
  uint32_t UUID_SampleEncryption::getVersion(){
    return getInt8(16);
  }
  
  void UUID_SampleEncryption::setFlags(uint32_t newFlags){
    setInt24(newFlags, 17);
  }
  
  uint32_t UUID_SampleEncryption::getFlags(){
    return getInt24(17);
  }

  void UUID_SampleEncryption::setAlgorithmID(uint32_t newAlgorithmID){
    if (getFlags() & 0x01){
      setInt24(newAlgorithmID, 20);
    }
  }

  uint32_t UUID_SampleEncryption::getAlgorithmID(){
    if (getFlags() & 0x01){
      return getInt24(20);
    }
    return -1;
  }

  void UUID_SampleEncryption::setIVSize(uint32_t newIVSize){
    if (getFlags() & 0x01){
      setInt8(newIVSize, 23);
    }
  }

  uint32_t UUID_SampleEncryption::getIVSize(){
    if (getFlags() & 0x01){
      return getInt8(23);
    }
    return -1;
  }

  void UUID_SampleEncryption::setKID(std::string newKID){
    if (newKID==""){
      return;
    }
    if (getFlags() & 0x01){
      while (newKID.size() < 16){
        newKID += (char)0x00;
      }
      for (int i = 0; i < 16; i++){
        setInt8(newKID[i],24+i);
      }
    }
  }

  std::string UUID_SampleEncryption::getKID(){
    if (getFlags() & 0x01){
      std::string result;
      for (int i = 0; i < 16; i++){
        result += (char)getInt8(24+i);
      }
      return result;
    }
    return "";
  }

  uint32_t UUID_SampleEncryption::getSampleCount(){
    int myOffset = 20;
    if (getFlags() & 0x01){
      myOffset += 20;
    }
    return getInt32(myOffset);
  }

#define IV_SIZE 8
  void UUID_SampleEncryption::setSample(UUID_SampleEncryption_Sample newSample, size_t index){
    int myOffset = 20;
    myOffset += 20 * (getFlags() & 0x01);
    myOffset += 4;//sampleCount is here;
    for (unsigned int i = 0; i < std::min(index, (size_t)getSampleCount()); i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2 + (entryCount * 6);
      }
    }
    if (index >= getSampleCount()){
      //we are now at the end of currently reserved space, reserve more and adapt offset accordingly.
      int reserveSize = ((index - getSampleCount())) * (IV_SIZE + (getFlags() & 0x02));
      reserveSize += IV_SIZE;
      if (getFlags() & 0x02){
        reserveSize += 2 + newSample.Entries.size();
      }
      if (!reserve(myOffset, 0, reserveSize)){
        return;//Memory errors...
      }
      myOffset += (index - getSampleCount()) * (IV_SIZE + (getFlags() & 0x02));
    }
    //write box.
    for (int i = 0; i < IV_SIZE; i++){
      setInt8(newSample.InitializationVector[i], myOffset ++);//set and skip
    }
    if (getFlags() & 0x02){
      setInt16(newSample.Entries.size(), myOffset);
      myOffset += 2;
      for (std::vector<UUID_SampleEncryption_Sample_Entry>::iterator it = newSample.Entries.begin(); it != newSample.Entries.end(); it++){
        setInt16(it->BytesClear, myOffset);
        myOffset += 2;
        setInt32(it->BytesEncrypted, myOffset);
        myOffset += 4;
      }
    }
    if (index >= getSampleCount()){
      setInt32(index + 1, 20 + (20 * (getFlags() & 0x01)));
    }
  }

  UUID_SampleEncryption_Sample UUID_SampleEncryption::getSample(size_t index){
    if (index >= getSampleCount()){
      return UUID_SampleEncryption_Sample();
    }
    int myOffset = 20;
    myOffset += 20 * (getFlags() & 0x01);
    myOffset += 4;//sampleCount is here
    for (unsigned int i = 0; i < index; i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2;//skip over entrycount
        myOffset += entryCount * 6;//skip entryCount sample entries
      }
    }
    UUID_SampleEncryption_Sample result;
    for (int i = 0; i < IV_SIZE; i++){
      result.InitializationVector += getInt8(myOffset++);//read and skip
    }
    if (getFlags() & 0x02){
      result.NumberOfEntries = getInt16(myOffset);
      myOffset += 2;
      for (unsigned int i = 0; i < result.NumberOfEntries; i++){
        result.Entries.push_back(UUID_SampleEncryption_Sample_Entry());
        result.Entries[i].BytesClear = getInt16(myOffset);
        myOffset += 2;
        result.Entries[i].BytesEncrypted = getInt32(myOffset);
        myOffset += 4;
      }
    }
    return result;
  }

  std::string UUID_SampleEncryption::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[a2394f52-5a9b-4f14-a244-6c427c648df4] Sample Encryption Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Flags: " << getFlags() << std::endl;
    if (getFlags() & 0x01){
      r << std::string(indent + 1, ' ') << "Algorithm ID: " << getAlgorithmID() << std::endl;
      r << std::string(indent + 1, ' ') << "IV Size: " << getIVSize() << std::endl;
      r << std::string(indent + 1, ' ') << "Key ID: " << getKID() << std::endl;
    }
    r << std::string(indent + 1, ' ') << "Sample Count: " << getSampleCount() << std::endl;
    for (unsigned int i = 0; i < getSampleCount(); i++){
      UUID_SampleEncryption_Sample tmpSample = getSample(i);
      r << std::string(indent + 1, ' ') << "[" << i << "]" << std::endl;
      r << std::string(indent + 3, ' ') << "Initialization Vector: 0x";
      for (unsigned int j = 0; j < tmpSample.InitializationVector.size(); j++){
        r << std::hex << std::setw(2) << std::setfill('0') << (int)tmpSample.InitializationVector[j] << std::dec;
      }
      r << std::endl;
      if (getFlags() & 0x02){
        r << std::string(indent + 3, ' ') << "Number of entries: " << tmpSample.NumberOfEntries << std::endl;
        for (unsigned int j = 0; j < tmpSample.NumberOfEntries; j++){
          r << std::string(indent + 3, ' ') << "[" << j << "]" << std::endl;
          r << std::string(indent + 5, ' ') << "Bytes clear: " << tmpSample.Entries[j].BytesClear << std::endl;
          r << std::string(indent + 5, ' ') << "Bytes encrypted: " << tmpSample.Entries[j].BytesEncrypted << std::endl;
        }
      }
    }
    return r.str();
  }

  UUID_TrackEncryption::UUID_TrackEncryption(){
   setUUID((std::string)"8974dbce-7be7-4c51-84f9-7148f9882554");
  }

  void UUID_TrackEncryption::setVersion(uint32_t newVersion){
    setInt8(newVersion, 16);
  }

  uint32_t UUID_TrackEncryption::getVersion(){
    return getInt8(16);
  }

  void UUID_TrackEncryption::setFlags(uint32_t newFlags){
    setInt24(newFlags, 17);
  }
  
  uint32_t UUID_TrackEncryption::getFlags(){
    return getInt24(17);
  }

  void UUID_TrackEncryption::setDefaultAlgorithmID(uint32_t newID){
    setInt24(newID, 20);
  }

  uint32_t UUID_TrackEncryption::getDefaultAlgorithmID(){
    return getInt24(20);
  }

  void UUID_TrackEncryption::setDefaultIVSize(uint8_t newIVSize){
    setInt8(newIVSize, 23);
  }

  uint8_t UUID_TrackEncryption::getDefaultIVSize(){
    return getInt8(23);
  }

  void UUID_TrackEncryption::setDefaultKID(std::string newKID){
    for (unsigned int i = 0; i < 16; i++){
      if (i < newKID.size()){
        setInt8(newKID[i], 24 + i);
      }else{
        setInt8(0x00, 24 + i);
      }
    }
  }

  std::string UUID_TrackEncryption::getDefaultKID(){
    std::string result;
    for (int i = 0; i < 16; i++){
      result += getInt8(24 + i);
    }
    return result;
  }

  std::string UUID_TrackEncryption::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[8974dbce-7be7-4c51-84f9-7148f9882554] Track Encryption Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Flags: " << getFlags() << std::endl;
    r << std::string(indent + 2, ' ') << "Default Algorithm ID: " << std::hex << getDefaultAlgorithmID() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "Default IV Size: " << (int)getDefaultIVSize() << std::endl;
    r << std::string(indent + 2, ' ') << "Default KID: 16 bytes of binary data." << std::endl;
    return r.str();
  }

  UUID_ProtectionSystemSpecificHeader::UUID_ProtectionSystemSpecificHeader(){
    setUUID((std::string)"d08a4f18-10f3-4a82-b6c8-32d8aba183d3");
  }

  void UUID_ProtectionSystemSpecificHeader::setVersion(uint32_t newVersion){
    setInt8(newVersion, 16);
  }

  uint32_t UUID_ProtectionSystemSpecificHeader::getVersion(){
    return getInt8(16);
  }

  void UUID_ProtectionSystemSpecificHeader::setFlags(uint32_t newFlags){
    setInt24(newFlags, 17);
  }
  
  uint32_t UUID_ProtectionSystemSpecificHeader::getFlags(){
    return getInt24(17);
  }

  void UUID_ProtectionSystemSpecificHeader::setSystemID(std::string newID){
    for (unsigned int i = 0; i < 16; i++){
      if (i < newID.size()){
        setInt8(newID[i], 20 + i);
      }else{
        setInt8(0x00, 20+i);
      }
    }
  }

  std::string UUID_ProtectionSystemSpecificHeader::getSystemID(){
    std::string result;
    for (int i = 0; i < 16; i++){
      result += getInt8(20 + i);
    }
    return result;
  }

  uint32_t UUID_ProtectionSystemSpecificHeader::getDataSize(){
    return getInt32(36);
  }

  void UUID_ProtectionSystemSpecificHeader::setData(std::string newData){
    setInt32(newData.size(), 36);
    for (unsigned int i = 0; i < newData.size(); i++){
      setInt8(newData[i], 40 + i);
    }
  }

  std::string UUID_ProtectionSystemSpecificHeader::getData(){
    std::string result;
    for (unsigned int i = 0; i < getDataSize(); i++){
      result += getInt8(40 + i);
    }
    return result;
  }
  
  std::string UUID_ProtectionSystemSpecificHeader::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[d08a4f18-10f3-4a82-b6c8-32d8aba183d3] Protection System Specific Header Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Flags: " << getFlags() << std::endl;
    r << std::string(indent + 2, ' ') << "System ID: " << getSystemID() << std::endl;
    r << std::string(indent + 2, ' ') << "Data Size: " << getDataSize() << std::endl;
    r << std::string(indent + 2, ' ') << "Data: " << getData().size() << " bytes of data." << std::endl;
    return r.str();
  }

  SINF::SINF(){
    memcpy(data + 4, "sinf", 4);
  }

  void SINF::setEntry(Box & newEntry, uint32_t no){
    if (no > 4){
      return;
    }
    int tempLoc = 0;
    for (unsigned int i = 0; i < no; i++){
      tempLoc += Box(getBox(tempLoc).asBox(), false).boxedSize();
    }
    setBox(newEntry, tempLoc);
  }

  Box & SINF::getEntry(uint32_t no){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if (no > 4){
      ret = Box((char*)"\000\000\000\010erro", false);
      return ret;
    }
    int tempLoc = 0;
    for (unsigned int i = 0; i < no; i++ ){
      tempLoc += Box(getBox(tempLoc).asBox(), false).boxedSize();
    }
    ret = Box(getBox(tempLoc).asBox(), false);
    return ret;
  }

  std::string SINF::toPrettyString(uint32_t indent){
    std::stringstream r;
    std::cerr << payloadOffset << std::endl;
    r << std::string(indent, ' ') << "[sinf] Protection Scheme Info Box (" << boxedSize() << ")" << std::endl;
    for (int i = 0; i < 4; i++){
      if (!getEntry(i).isType("erro")){
        r << getEntry(i).toPrettyString(indent + 2);
      }
    }
    r << std::endl;
    return r.str();
  }

  FRMA::FRMA(){
    memcpy(data + 4, "frma", 4);
  }

  void FRMA::setOriginalFormat(std::string newFormat){
    for (unsigned int i = 0; i < 4; i++){
      if (i < newFormat.size()){
        setInt8(newFormat[i], i);
      }else{
        setInt8(0x00, i);
      }
    }
  }

  std::string FRMA::getOriginalFormat(){
    return std::string(payload(),4);
  }

  std::string FRMA::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[frma] Original Format Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Original Format: " << getOriginalFormat() << std::endl;
    return r.str();
  }

  SCHM::SCHM(){
    memcpy(data + 4, "schm", 4);
  }

  void SCHM::setSchemeType(uint32_t newType){
    setInt32(htonl(newType), 4);
  }

  uint32_t SCHM::getSchemeType(){
    return ntohl(getInt32(4));
  }

  void SCHM::setSchemeVersion(uint32_t newVersion){
    setInt32(htonl(newVersion), 8);
  }

  uint32_t SCHM::getSchemeVersion(){
    return ntohl(getInt32(8));
  }

  void SCHM::setSchemeURI(std::string newURI){
    setFlags(getFlags() | 0x01);
    setString(newURI, 12);
  }

  std::string SCHM::getSchemeURI(){
    if (getFlags() & 0x01){
      return getString(12);
    }
    return "";
  }

  std::string SCHM::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[schm] Scheme Type Box (" << boxedSize() << ")" << std::endl;
    char tmpStr[10];
    int tmpInt = getSchemeType();
    sprintf(tmpStr, "%.4s", (char*)&tmpInt);
    r << std::string(indent + 2, ' ') << "SchemeType: " << std::string(tmpStr,4) << std::endl;
    r << std::string(indent + 2, ' ') << "SchemeVersion: 0x" << std::hex << std::setw(8) << std::setfill('0') << getSchemeVersion() << std::dec << std::endl;
    if (getFlags() & 0x01){
      r << std::string(indent + 2, ' ') << "SchemeURI: " << getSchemeURI() << std::endl;
    }
    return r.str();
  }

  SCHI::SCHI(){
    memcpy(data +4, "schi", 4);
  }

  void SCHI::setContent(Box & newContent){
    setBox(newContent, 0);
  }

  Box & SCHI::getContent(){
    return getBox(0);
  }

  std::string SCHI::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[schi] Scheme Information Box (" << boxedSize() << ")" << std::endl;
    r << getContent().toPrettyString(indent + 2);
    return r.str();
  }

}
