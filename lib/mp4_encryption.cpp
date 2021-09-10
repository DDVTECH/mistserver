#include "defines.h"
#include "mp4_encryption.h"

namespace MP4{

  PSSH::PSSH(){memcpy(data + 4, "pssh", 4);}

  std::string PSSH::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[pssh] Protection System Specific Header Box (" << boxedSize()
      << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "SystemID: " << getSystemIDHex() << std::endl;
    if (getVersion()){
      r << std::string(indent + 1, ' ') << "KID_count: " << getKIDCount() << std::endl;
    }
    r << std::string(indent + 1, ' ') << "DataSize: " << getDataSize() << std::endl;
    r << std::string(indent + 1, ' ') << "Data: ";
    size_t dataSize = getDataSize();
    char *data = getData();
    for (size_t i = 0; i < dataSize; ++i){
      r << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << std::dec << "";
    }
    r << std::endl;
    return r.str();
  }

  std::string PSSH::getSystemIDHex(){
    char *systemID = getString(4);
    std::stringstream res;
    for (size_t i = 0; i < 16; ++i){
      res << std::hex << std::setw(2) << std::setfill('0') << (int)systemID[i] << std::dec;
    }
    return "0x" + res.str();
  }

  void PSSH::setSystemIDHex(const std::string &systemID){setString(systemID, 4);}

  size_t PSSH::getKIDCount(){return getVersion() ? getInt32(20) : 0;}

  size_t PSSH::getDataSize(){
    if (getVersion()){
      size_t kidCount = getInt32(20);
      return getInt32(24 + (kidCount * 16));
    }
    return getInt32(20);
  }

  char *PSSH::getData(){
    if (getVersion()){
      size_t kidCount = getInt32(20);
      return getString(24 + (kidCount * 16) + 4);
    }
    return getString(24);
  }

  void PSSH::setData(const std::string &data){
    if (getVersion()){
      WARN_MSG("Not implemented yet!");
      return;
    }
    for (int i = 0; i < data.size(); i++){setInt8(data[i], 24 + i);}
    setInt32(data.size(), 20);
  }

  std::string TENC::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[tenc] Track Encryption Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "default_isEncrypted: " << getDefaultIsEncrypted() << std::endl;
    r << std::string(indent + 1, ' ') << "default_IV_size: " << getDefaultIVSize() << std::endl;
    r << std::string(indent + 1, ' ') << "default_KID: ";
    std::string defaultKID = getDefaultKID();
    for (int i = 0; i < 16; i++){
      r << std::hex << std::setw(2) << std::setfill('0') << (int)defaultKID[i] << std::dec << " ";
    }
    r << std::endl;
    return r.str();
  }

  TENC::TENC(){
    memcpy(data + 4, "tenc", 4);
    setDefaultIsEncrypted(1);
    setDefaultIVSize(8);
  }
  size_t TENC::getDefaultIsEncrypted(){return getInt24(4);}
  void TENC::setDefaultIsEncrypted(size_t isEncrypted){setInt24(isEncrypted, 4);}
  size_t TENC::getDefaultIVSize(){return getInt8(7);}
  void TENC::setDefaultIVSize(uint8_t ivSize){setInt8(ivSize, 7);}
  std::string TENC::getDefaultKID(){
    std::string result;
    for (int i = 8; i < 24; i++){result += getInt8(i);}
    return result;
  }
  void TENC::setDefaultKID(const std::string &kid){
    for (int i = 0; i < 16; i++){
      if (i < kid.size()){
        setInt8(kid[i], i + 8);
      }else{
        setInt8(0, i + 8);
      }
    }
  }

  SENC::SENC(){
    memcpy(data + 4, "senc", 4);
    setFlags(2);
  }
  uint32_t SENC::getSampleCount() const{return getInt32(4);}

#define IV_SIZE 8
  void SENC::setSample(UUID_SampleEncryption_Sample newSample, size_t index){
    int myOffset = 8;
    for (unsigned int i = 0; i < std::min(index, (size_t)getSampleCount()); i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2 + (entryCount * 6);
      }
    }
    if (index > getSampleCount()){
      ERROR_MSG("First fill intermediate entries!");
      return;
      /*
      //we are now at the end of currently reserved space, reserve more and adapt offset
      accordingly. int reserveSize = ((index - getSampleCount())) * (IV_SIZE + (getFlags() & 0x02));
      reserveSize += IV_SIZE;
      if (getFlags() & 0x02){
        reserveSize += 2 + newSample.Entries.size();
      }
      if (!reserve(myOffset, 0, reserveSize)){
        return;//Memory errors...
      }
      myOffset += (index - getSampleCount()) * (IV_SIZE + (getFlags() & 0x02));
      */
    }
    // write box.
    for (int i = 0; i < IV_SIZE; i++){
      setInt8(newSample.InitializationVector[i], myOffset++); // set and skip
    }
    if (getFlags() & 0x02){
      setInt16(newSample.Entries.size(), myOffset);
      myOffset += 2;
      for (std::vector<UUID_SampleEncryption_Sample_Entry>::iterator it = newSample.Entries.begin();
           it != newSample.Entries.end(); it++){
        setInt16(it->BytesClear, myOffset);
        myOffset += 2;
        setInt32(it->BytesEncrypted, myOffset);
        myOffset += 4;
      }
    }
    if (index >= getSampleCount()){setInt32(index + 1, 4);}
  }

  UUID_SampleEncryption_Sample SENC::getSample(size_t index) const{
    if (index >= getSampleCount()){return UUID_SampleEncryption_Sample();}
    int myOffset = 8;
    for (unsigned int i = 0; i < index; i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2;              // skip over entrycount
        myOffset += entryCount * 6; // skip entryCount sample entries
      }
    }
    UUID_SampleEncryption_Sample result;
    for (int i = 0; i < IV_SIZE; i++){
      result.InitializationVector += (char)getInt8(myOffset++); // read and skip
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

  std::string SENC::toPrettyString(uint32_t indent) const{
    std::stringstream r;
    r << std::string(indent, ' ') << "[senc] Sample Encryption Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Sample Count: " << getSampleCount() << std::endl;
    for (unsigned int i = 0; i < getSampleCount(); i++){
      UUID_SampleEncryption_Sample tmpSample = getSample(i);
      r << std::string(indent + 1, ' ') << "[" << i << "]" << std::endl;
      r << std::string(indent + 3, ' ') << "Initialization Vector: 0x";
      for (unsigned int j = 0; j < tmpSample.InitializationVector.size(); j++){
        r << std::hex << std::setw(2) << std::setfill('0') << (int)tmpSample.InitializationVector[j]
          << std::dec;
      }
      r << std::endl;
      if (getFlags() & 0x02){
        r << std::string(indent + 3, ' ') << "Number of entries: " << tmpSample.NumberOfEntries << std::endl;
        for (unsigned int j = 0; j < tmpSample.NumberOfEntries; j++){
          r << std::string(indent + 3, ' ') << "[" << j << "]" << std::endl;
          r << std::string(indent + 5, ' ') << "Bytes clear: " << tmpSample.Entries[j].BytesClear << std::endl;
          r << std::string(indent + 5, ' ')
            << "Bytes encrypted: " << tmpSample.Entries[j].BytesEncrypted << std::endl;
        }
      }
    }
    return r.str();
  }

  SAIZ::SAIZ(size_t entryCount){
    memcpy(data + 4, "saiz", 4);
    setFlags(0);
    setVersion(0);
    setInt24(0, 4);          // Default sample size
    setInt16(entryCount, 7); // EntryCount Samples
    for (int i = 0; i < entryCount; i++){
      setInt8(16, i + 9); // 16 bytes IV's
    }
  }

  size_t SAIZ::getDefaultSampleSize(){return getInt24(4);}

  size_t SAIZ::getEntryCount(){return getInt16(7);}

  size_t SAIZ::getEntrySize(size_t entryNo){
    if (entryNo >= getEntryCount()){return -1;}
    return getInt8(9 + entryNo);
  }

  std::string SAIZ::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[saiz] Sample Auxiliary Information Size Box (" << boxedSize()
      << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Default Sample Size: " << getDefaultSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "Entry Count: " << getEntryCount() << std::endl;
    for (size_t i = 0; i < getEntryCount(); ++i){
      r << std::string(indent + 2, ' ') << "[" << i << "]: " << getEntrySize(i) << std::endl;
    }
    return r.str();
  }

  SAIO::SAIO(size_t offset){
    memcpy(data + 4, "saio", 4);
    setInt32(1, 4);
    setInt32(offset, 8);
  }

  size_t SAIO::getEntryCount(){return getInt32(4);}

  size_t SAIO::getEntrySize(size_t entryNo){
    if (entryNo >= getEntryCount()){return -1;}
    return getInt32(8 + (entryNo * 4));
  }

  std::string SAIO::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[saio] Sample Auxiliary Information Offset Box ("
      << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Entry Count: " << getEntryCount() << std::endl;
    for (size_t i = 0; i < getEntryCount(); ++i){
      r << std::string(indent + 2, ' ') << "[" << i << "]: " << getEntrySize(i) << std::endl;
    }
    return r.str();
  }

  UUID_SampleEncryption::UUID_SampleEncryption(){
    setUUID((std::string) "a2394f52-5a9b-4f14-a244-6c427c648df4");
  }

  UUID_SampleEncryption::UUID_SampleEncryption(const SENC &senc){
    setUUID((std::string) "a2394f52-5a9b-4f14-a244-6c427c648df4");
    setVersion(0);
    setFlags(2);
    size_t sampleCount = senc.getSampleCount();
    for (size_t i = 0; i < sampleCount; ++i){setSample(senc.getSample(i), i);}
  }

  void UUID_SampleEncryption::setVersion(uint32_t newVersion){setInt8(newVersion, 16);}

  uint32_t UUID_SampleEncryption::getVersion(){return getInt8(16);}

  void UUID_SampleEncryption::setFlags(uint32_t newFlags){setInt24(newFlags, 17);}

  uint32_t UUID_SampleEncryption::getFlags(){return getInt24(17);}

  void UUID_SampleEncryption::setAlgorithmID(uint32_t newAlgorithmID){
    if (getFlags() & 0x01){setInt24(newAlgorithmID, 20);}
  }

  uint32_t UUID_SampleEncryption::getAlgorithmID(){
    if (getFlags() & 0x01){return getInt24(20);}
    return -1;
  }

  void UUID_SampleEncryption::setIVSize(uint32_t newIVSize){
    if (getFlags() & 0x01){setInt8(newIVSize, 23);}
  }

  uint32_t UUID_SampleEncryption::getIVSize(){
    if (getFlags() & 0x01){return getInt8(23);}
    return -1;
  }

  void UUID_SampleEncryption::setKID(std::string newKID){
    if (newKID == ""){return;}
    if (getFlags() & 0x01){
      while (newKID.size() < 16){newKID += (char)0x00;}
      for (int i = 0; i < 16; i++){setInt8(newKID[i], 24 + i);}
    }
  }

  std::string UUID_SampleEncryption::getKID(){
    if (getFlags() & 0x01){
      std::string result;
      for (int i = 0; i < 16; i++){result += (char)getInt8(24 + i);}
      return result;
    }
    return "";
  }

  uint32_t UUID_SampleEncryption::getSampleCount(){
    int myOffset = 20;
    if (getFlags() & 0x01){myOffset += 20;}
    return getInt32(myOffset);
  }

#define IV_SIZE 8
  void UUID_SampleEncryption::setSample(UUID_SampleEncryption_Sample newSample, size_t index){
    int myOffset = 20;
    myOffset += 20 * (getFlags() & 0x01);
    myOffset += 4; // sampleCount is here;
    for (unsigned int i = 0; i < std::min(index, (size_t)getSampleCount()); i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2 + (entryCount * 6);
      }
    }
    if (index > getSampleCount()){
      ERROR_MSG("First fill intermediate entries!");
      return;
    }
    // write box.
    for (int i = 0; i < IV_SIZE; i++){
      setInt8(newSample.InitializationVector[i], myOffset++); // set and skip
    }
    if (getFlags() & 0x02){
      setInt16(newSample.Entries.size(), myOffset);
      myOffset += 2;
      for (std::vector<UUID_SampleEncryption_Sample_Entry>::iterator it = newSample.Entries.begin();
           it != newSample.Entries.end(); it++){
        setInt16(it->BytesClear, myOffset);
        myOffset += 2;
        setInt32(it->BytesEncrypted, myOffset);
        myOffset += 4;
      }
    }
    if (index >= getSampleCount()){setInt32(index + 1, 20 + (20 * (getFlags() & 0x01)));}
  }

  UUID_SampleEncryption_Sample UUID_SampleEncryption::getSample(size_t index){
    if (index >= getSampleCount()){return UUID_SampleEncryption_Sample();}
    int myOffset = 20;
    myOffset += 20 * (getFlags() & 0x01);
    myOffset += 4; // sampleCount is here
    for (unsigned int i = 0; i < index; i++){
      myOffset += IV_SIZE;
      if (getFlags() & 0x02){
        int entryCount = getInt16(myOffset);
        myOffset += 2;              // skip over entrycount
        myOffset += entryCount * 6; // skip entryCount sample entries
      }
    }
    UUID_SampleEncryption_Sample result;
    for (int i = 0; i < IV_SIZE; i++){
      result.InitializationVector += getInt8(myOffset++); // read and skip
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
    r << std::string(indent, ' ') << "[a2394f52-5a9b-4f14-a244-6c427c648df4] Sample Encryption Box ("
      << boxedSize() << ")" << std::endl;
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
        r << std::hex << std::setw(2) << std::setfill('0') << (int)tmpSample.InitializationVector[j]
          << std::dec;
      }
      r << std::endl;
      if (getFlags() & 0x02){
        r << std::string(indent + 3, ' ') << "Number of entries: " << tmpSample.NumberOfEntries << std::endl;
        for (unsigned int j = 0; j < tmpSample.NumberOfEntries; j++){
          r << std::string(indent + 3, ' ') << "[" << j << "]" << std::endl;
          r << std::string(indent + 5, ' ') << "Bytes clear: " << tmpSample.Entries[j].BytesClear << std::endl;
          r << std::string(indent + 5, ' ')
            << "Bytes encrypted: " << tmpSample.Entries[j].BytesEncrypted << std::endl;
        }
      }
    }
    return r.str();
  }

  UUID_TrackEncryption::UUID_TrackEncryption(){
    setUUID((std::string) "8974dbce-7be7-4c51-84f9-7148f9882554");
  }

  void UUID_TrackEncryption::setVersion(uint32_t newVersion){setInt8(newVersion, 16);}

  uint32_t UUID_TrackEncryption::getVersion(){return getInt8(16);}

  void UUID_TrackEncryption::setFlags(uint32_t newFlags){setInt24(newFlags, 17);}

  uint32_t UUID_TrackEncryption::getFlags(){return getInt24(17);}

  void UUID_TrackEncryption::setDefaultAlgorithmID(uint32_t newID){setInt24(newID, 20);}

  uint32_t UUID_TrackEncryption::getDefaultAlgorithmID(){return getInt24(20);}

  void UUID_TrackEncryption::setDefaultIVSize(uint8_t newIVSize){setInt8(newIVSize, 23);}

  uint8_t UUID_TrackEncryption::getDefaultIVSize(){return getInt8(23);}

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
    for (int i = 0; i < 16; i++){result += getInt8(24 + i);}
    return result;
  }

  std::string UUID_TrackEncryption::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[8974dbce-7be7-4c51-84f9-7148f9882554] Track Encryption Box ("
      << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Flags: " << getFlags() << std::endl;
    r << std::string(indent + 2, ' ') << "Default Algorithm ID: " << std::hex
      << getDefaultAlgorithmID() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "Default IV Size: " << (int)getDefaultIVSize() << std::endl;
    r << std::string(indent + 2, ' ') << "Default KID: 16 bytes of binary data." << std::endl;
    return r.str();
  }

  UUID_ProtectionSystemSpecificHeader::UUID_ProtectionSystemSpecificHeader(){
    setUUID((std::string) "d08a4f18-10f3-4a82-b6c8-32d8aba183d3");
  }

  void UUID_ProtectionSystemSpecificHeader::setVersion(uint32_t newVersion){
    setInt8(newVersion, 16);
  }

  uint32_t UUID_ProtectionSystemSpecificHeader::getVersion(){return getInt8(16);}

  void UUID_ProtectionSystemSpecificHeader::setFlags(uint32_t newFlags){setInt24(newFlags, 17);}

  uint32_t UUID_ProtectionSystemSpecificHeader::getFlags(){return getInt24(17);}

  void UUID_ProtectionSystemSpecificHeader::setSystemID(std::string newID){
    for (unsigned int i = 0; i < 16; i++){
      if (i < newID.size()){
        setInt8(newID[i], 20 + i);
      }else{
        setInt8(0x00, 20 + i);
      }
    }
  }

  std::string UUID_ProtectionSystemSpecificHeader::getSystemID(){
    std::string result;
    for (int i = 0; i < 16; i++){result += getInt8(20 + i);}
    return result;
  }

  uint32_t UUID_ProtectionSystemSpecificHeader::getDataSize(){return getInt32(36);}

  void UUID_ProtectionSystemSpecificHeader::setData(std::string newData){
    setInt32(newData.size(), 36);
    for (unsigned int i = 0; i < newData.size(); i++){setInt8(newData[i], 40 + i);}
  }

  std::string UUID_ProtectionSystemSpecificHeader::getData(){
    std::string result;
    for (unsigned int i = 0; i < getDataSize(); i++){result += getInt8(40 + i);}
    return result;
  }

  std::string UUID_ProtectionSystemSpecificHeader::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[d08a4f18-10f3-4a82-b6c8-32d8aba183d3] Protection System Specific Header Box ("
      << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 2, ' ') << "Flags: " << getFlags() << std::endl;
    r << std::string(indent + 2, ' ') << "System ID: " << getSystemID() << std::endl;
    r << std::string(indent + 2, ' ') << "Data Size: " << getDataSize() << std::endl;
    r << std::string(indent + 2, ' ') << "Data: " << getData().size() << " bytes of data." << std::endl;
    return r.str();
  }

  SINF::SINF(){memcpy(data + 4, "sinf", 4);}

  void SINF::setEntry(Box &newEntry, uint32_t no){
    if (no > 4){return;}
    int tempLoc = 0;
    for (unsigned int i = 0; i < no; i++){
      tempLoc += Box(getBox(tempLoc).asBox(), false).boxedSize();
    }
    setBox(newEntry, tempLoc);
  }

  Box &SINF::getEntry(uint32_t no){
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (no > 4){return ret;}
    int tempLoc = 0;
    for (unsigned int i = 0; i < no; i++){tempLoc += getBoxLen(tempLoc);}
    return getBox(tempLoc);
  }

  std::string SINF::toPrettyString(uint32_t indent){
    std::stringstream r;
    std::cerr << payloadOffset << std::endl;
    r << std::string(indent, ' ') << "[sinf] Protection Scheme Info Box (" << boxedSize() << ")" << std::endl;
    for (int i = 0; i < 4; i++){
      if (!getEntry(i).isType("erro")){r << getEntry(i).toPrettyString(indent + 2);}
    }
    r << std::endl;
    return r.str();
  }

  FRMA::FRMA(const std::string &originalFormat){
    memcpy(data + 4, "frma", 4);
    setOriginalFormat(originalFormat);
  }

  void FRMA::setOriginalFormat(const std::string &newFormat){
    for (unsigned int i = 0; i < 4; i++){
      if (i < newFormat.size()){
        setInt8(newFormat[i], i);
      }else{
        setInt8(0x00, i);
      }
    }
  }

  std::string FRMA::getOriginalFormat(){return std::string(payload(), 4);}

  std::string FRMA::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[frma] Original Format Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 2, ' ') << "Original Format: " << getOriginalFormat() << std::endl;
    return r.str();
  }

  SCHM::SCHM(uint32_t schemeType, uint32_t schemeVersion){
    memcpy(data + 4, "schm", 4);
    setSchemeType(schemeType);
    setSchemeVersion(schemeVersion);
  }

  void SCHM::setSchemeType(uint32_t newType){setInt32(htonl(newType), 4);}

  uint32_t SCHM::getSchemeType(){return ntohl(getInt32(4));}

  void SCHM::setSchemeVersion(uint32_t newVersion){setInt32(htonl(newVersion), 8);}

  uint32_t SCHM::getSchemeVersion(){return ntohl(getInt32(8));}

  void SCHM::setSchemeURI(std::string newURI){
    setFlags(getFlags() | 0x01);
    setString(newURI, 12);
  }

  std::string SCHM::getSchemeURI(){
    if (getFlags() & 0x01){return getString(12);}
    return "";
  }

  std::string SCHM::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[schm] Scheme Type Box (" << boxedSize() << ")" << std::endl;
    char tmpStr[10];
    int tmpInt = getSchemeType();
    sprintf(tmpStr, "%.4s", (char *)&tmpInt);
    r << std::string(indent + 2, ' ') << "SchemeType: " << std::string(tmpStr, 4) << std::endl;
    r << std::string(indent + 2, ' ') << "SchemeVersion: 0x" << std::hex << std::setw(8)
      << std::setfill('0') << getSchemeVersion() << std::dec << std::endl;
    if (getFlags() & 0x01){
      r << std::string(indent + 2, ' ') << "SchemeURI: " << getSchemeURI() << std::endl;
    }
    return r.str();
  }

  SCHI::SCHI(){memcpy(data + 4, "schi", 4);}

  void SCHI::setContent(Box &newContent){setBox(newContent, 0);}

  Box &SCHI::getContent(){return getBox(0);}

  std::string SCHI::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[schi] Scheme Information Box (" << boxedSize() << ")" << std::endl;
    r << getContent().toPrettyString(indent + 2);
    return r.str();
  }

}// namespace MP4
