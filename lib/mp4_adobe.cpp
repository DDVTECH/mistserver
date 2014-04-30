#include "mp4_adobe.h"

namespace MP4 {
  ABST::ABST(){
    memcpy(data + 4, "abst", 4);
    setVersion(0);
    setFlags(0);
    setBootstrapinfoVersion(0);
    setProfile(0);
    setLive(1);
    setUpdate(0);
    setTimeScale(1000);
    setCurrentMediaTime(0);
    setSmpteTimeCodeOffset(0);
    std::string empty;
    setMovieIdentifier(empty);
    setInt8(0, 30); //set serverentrycount to 0
    setInt8(0, 31); //set qualityentrycount to 0
    setDrmData(empty);
    setMetaData(empty);
  }

  void ABST::setVersion(char newVersion){
    setInt8(newVersion, 0);
  }

  char ABST::getVersion(){
    return getInt8(0);
  }

  void ABST::setFlags(uint32_t newFlags){
    setInt24(newFlags, 1);
  }

  uint32_t ABST::getFlags(){
    return getInt24(1);
  }

  void ABST::setBootstrapinfoVersion(uint32_t newVersion){
    setInt32(newVersion, 4);
  }

  uint32_t ABST::getBootstrapinfoVersion(){
    return getInt32(4);
  }

  void ABST::setProfile(char newProfile){
    //profile = bit 1 and 2 of byte 8.
    setInt8((getInt8(8) & 0x3F) + ((newProfile & 0x03) << 6), 8);
  }

  char ABST::getProfile(){
    return (getInt8(8) & 0xC0);
  }
  ;

  void ABST::setLive(bool newLive){
    //live = bit 4 of byte 8.
    setInt8((getInt8(8) & 0xDF) + (newLive ? 0x10 : 0), 8);
  }

  bool ABST::getLive(){
    return (getInt8(8) & 0x10);
  }

  void ABST::setUpdate(bool newUpdate){
    //update = bit 5 of byte 8.
    setInt8((getInt8(8) & 0xEF) + (newUpdate ? 0x08 : 0), 8);
  }

  bool ABST::getUpdate(){
    return (getInt8(8) & 0x08);
  }

  void ABST::setTimeScale(uint32_t newScale){
    setInt32(newScale, 9);
  }

  uint32_t ABST::getTimeScale(){
    return getInt32(9);
  }

  void ABST::setCurrentMediaTime(uint64_t newTime){
    setInt64(newTime, 13);
  }

  uint64_t ABST::getCurrentMediaTime(){
    return getInt64(13);
  }

  void ABST::setSmpteTimeCodeOffset(uint64_t newTime){
    setInt64(newTime, 21);
  }

  uint64_t ABST::getSmpteTimeCodeOffset(){
    return getInt64(21);
  }

  void ABST::setMovieIdentifier(std::string & newIdentifier){
    setString(newIdentifier, 29);
  }

  char* ABST::getMovieIdentifier(){
    return getString(29);
  }

  uint32_t ABST::getServerEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1;
    return getInt8(countLoc);
  }

  void ABST::setServerEntry(std::string & newEntry, uint32_t no){
    int countLoc = 29 + getStringLen(29) + 1;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getInt8(countLoc) && i < no; ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getInt8(countLoc)){
      int amount = no + 1 - getInt8(countLoc);
      if ( !reserve(payloadOffset + tempLoc, 0, amount)){
        return;
      };
      memset(data + payloadOffset + tempLoc, 0, amount);
      setInt8(no + 1, countLoc); //set new qualityEntryCount
      tempLoc += no - i;
    }
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setString(newEntry, tempLoc);
  }

  ///\return Empty string if no > serverEntryCount(), serverEntry[no] otherwise.
  const char* ABST::getServerEntry(uint32_t no){
    if (no + 1 > getServerEntryCount()){
      return "";
    }
    int tempLoc = 29 + getStringLen(29) + 1 + 1; //position of first entry
    for (unsigned int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t ABST::getQualityEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      countLoc += getStringLen(countLoc) + 1;
    }
    return getInt8(countLoc);
  }

  void ABST::setQualityEntry(std::string & newEntry, uint32_t no){
    int countLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      countLoc += getStringLen(countLoc) + 1;
    }
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getInt8(countLoc) && i < no; ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getInt8(countLoc)){
      int amount = no + 1 - getInt8(countLoc);
      if ( !reserve(payloadOffset + tempLoc, 0, amount)){
        return;
      };
      memset(data + payloadOffset + tempLoc, 0, amount);
      setInt8(no + 1, countLoc); //set new qualityEntryCount
      tempLoc += no - i;
    }
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setString(newEntry, tempLoc);
  }

  const char* ABST::getQualityEntry(uint32_t no){
    if (no > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 29 + getStringLen(29) + 1 + 1; //position of serverentries;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += 1; //first qualityentry
    for (unsigned int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  void ABST::setDrmData(std::string newDrm){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    setString(newDrm, tempLoc);
  }

  char* ABST::getDrmData(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  void ABST::setMetaData(std::string newMetaData){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1;
    setString(newMetaData, tempLoc);
  }

  char* ABST::getMetaData(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1;
    return getString(tempLoc);
  }

  uint32_t ABST::getSegmentRunTableCount(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    return getInt8(tempLoc);
  }

  void ABST::setSegmentRunTable(ASRT & newSegment, uint32_t no){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    int countLoc = tempLoc;
    tempLoc++; //skip segmentRuntableCount
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getInt8(countLoc) && i < no; ++i){
      tempLoc += getBoxLen(tempLoc);
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getInt8(countLoc)){
      int amount = no + 1 - getInt8(countLoc);
      if ( !reserve(payloadOffset + tempLoc, 0, amount * 8)){
        return;
      };
      //set empty erro boxes as contents
      for (int j = 0; j < amount; ++j){
        memcpy(data + payloadOffset + tempLoc + j * 8, "\000\000\000\010erro", 8);
      }
      setInt8(no + 1, countLoc); //set new count
      tempLoc += (no - i) * 8;
    }
    //now, tempLoc is at position for string number no, and we have at least an erro box reserved.
    setBox(newSegment, tempLoc);
  }

  ASRT & ABST::getSegmentRunTable(uint32_t no){
    static Box result;
    if (no > getSegmentRunTableCount()){
      static Box res;
      return (ASRT&)res;
    }
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    tempLoc++; //segmentRuntableCount
    for (unsigned int i = 0; i < no; ++i){
      tempLoc += getBoxLen(tempLoc);
    }
    return (ASRT&)getBox(tempLoc);
  }

  uint32_t ABST::getFragmentRunTableCount(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (unsigned int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    return getInt8(tempLoc);
  }

  void ABST::setFragmentRunTable(AFRT & newFragment, uint32_t no){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (unsigned int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    int countLoc = tempLoc;
    tempLoc++;
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getInt8(countLoc) && i < no; ++i){
      tempLoc += getBoxLen(tempLoc);
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getInt8(countLoc)){
      unsigned int amount = no + 1 - getInt8(countLoc);
      if ( !reserve(payloadOffset + tempLoc, 0, amount * 8)){
        return;
      };
      //set empty erro boxes as contents
      for (unsigned int j = 0; j < amount; ++j){
        memcpy(data + payloadOffset + tempLoc + j * 8, "\000\000\000\010erro", 8);
      }
      setInt8(no + 1, countLoc); //set new count
      tempLoc += (no - i) * 8;
    }
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setBox(newFragment, tempLoc);
  }

  AFRT & ABST::getFragmentRunTable(uint32_t no){
    static Box result;
    if (no >= getFragmentRunTableCount()){
      static Box res;
      return (AFRT&)res;
    }
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (unsigned int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    tempLoc++;
    for (unsigned int i = 0; i < no; i++){
      tempLoc += getBoxLen(tempLoc);
    }
    return (AFRT&)getBox(tempLoc);
  }

  std::string ABST::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[abst] Bootstrap Info (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "BootstrapinfoVersion " << getBootstrapinfoVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Profile " << (int)getProfile() << std::endl;
    if (getLive()){
      r << std::string(indent + 1, ' ') << "Live" << std::endl;
    }else{
      r << std::string(indent + 1, ' ') << "Recorded" << std::endl;
    }
    if (getUpdate()){
      r << std::string(indent + 1, ' ') << "Update" << std::endl;
    }else{
      r << std::string(indent + 1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent + 1, ' ') << "Timescale " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "CurrMediaTime " << getCurrentMediaTime() << std::endl;
    r << std::string(indent + 1, ' ') << "SmpteTimeCodeOffset " << getSmpteTimeCodeOffset() << std::endl;
    r << std::string(indent + 1, ' ') << "MovieIdentifier " << getMovieIdentifier() << std::endl;
    r << std::string(indent + 1, ' ') << "ServerEntryTable (" << getServerEntryCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getServerEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getServerEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "DrmData " << getDrmData() << std::endl;
    r << std::string(indent + 1, ' ') << "MetaData " << getMetaData() << std::endl;
    r << std::string(indent + 1, ' ') << "SegmentRunTableEntries (" << getSegmentRunTableCount() << ")" << std::endl;
    for (uint32_t i = 0; i < getSegmentRunTableCount(); i++){
      r << ((Box)getSegmentRunTable(i)).toPrettyString(indent + 2);
    }
    r << std::string(indent + 1, ' ') + "FragmentRunTableEntries (" << getFragmentRunTableCount() << ")" << std::endl;
    for (uint32_t i = 0; i < getFragmentRunTableCount(); i++){
      r << ((Box)getFragmentRunTable(i)).toPrettyString(indent + 2);
    }
    return r.str();
  }

  AFRT::AFRT(){
    memcpy(data + 4, "afrt", 4);
    setVersion(0);
    setUpdate(0);
    setTimeScale(1000);
  }

  void AFRT::setVersion(char newVersion){
    setInt8(newVersion, 0);
  }

  uint32_t AFRT::getVersion(){
    return getInt8(0);
  }

  void AFRT::setUpdate(uint32_t newUpdate){
    setInt24(newUpdate, 1);
  }

  uint32_t AFRT::getUpdate(){
    return getInt24(1);
  }

  void AFRT::setTimeScale(uint32_t newScale){
    setInt32(newScale, 4);
  }

  uint32_t AFRT::getTimeScale(){
    return getInt32(4);
  }

  uint32_t AFRT::getQualityEntryCount(){
    return getInt8(8);
  }

  void AFRT::setQualityEntry(std::string & newEntry, uint32_t no){
    int countLoc = 8;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getQualityEntryCount() && i < no; ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getQualityEntryCount()){
      int amount = no + 1 - getQualityEntryCount();
      if ( !reserve(payloadOffset + tempLoc, 0, amount)){
        return;
      };
      memset(data + payloadOffset + tempLoc, 0, amount);
      setInt8(no + 1, countLoc); //set new qualityEntryCount
      tempLoc += no - i;
    }
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setString(newEntry, tempLoc);
  }

  const char* AFRT::getQualityEntry(uint32_t no){
    if (no + 1 > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 9; //position of first quality entry
    for (unsigned int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t AFRT::getFragmentRunCount(){
    int tempLoc = 9;
    for (unsigned int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void AFRT::setFragmentRun(afrt_runtable newRun, uint32_t no){
    int tempLoc = 9;
    for (unsigned int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    int countLoc = tempLoc;
    unsigned int count = getInt32(countLoc);
    tempLoc += 4;
    for (unsigned int i = 0; i < no; i++){
      if (i + 1 > count){
        setInt32(0, tempLoc);
        setInt64(0, tempLoc + 4);
        setInt32(1, tempLoc + 12);
      }
      if (getInt32(tempLoc + 12) == 0){
        tempLoc += 17;
      }else{
        tempLoc += 16;
      }
    }
    setInt32(newRun.firstFragment, tempLoc);
    setInt64(newRun.firstTimestamp, tempLoc + 4);
    setInt32(newRun.duration, tempLoc + 12);
    if (newRun.duration == 0){
      setInt8(newRun.discontinuity, tempLoc + 16);
    }
    if (count < no + 1){
      setInt32(no + 1, countLoc);
    }
  }

  afrt_runtable AFRT::getFragmentRun(uint32_t no){
    afrt_runtable res;
    if (no > getFragmentRunCount()){
      return res;
    }
    int tempLoc = 9;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += 4;
    for (unsigned int i = 0; i < no; i++){
      if (getInt32(tempLoc + 12) == 0){
        tempLoc += 17;
      }else{
        tempLoc += 16;
      }
    }
    res.firstFragment = getInt32(tempLoc);
    res.firstTimestamp = getInt64(tempLoc + 4);
    res.duration = getInt32(tempLoc + 12);
    if (res.duration){
      res.discontinuity = getInt8(tempLoc + 16);
    }else{
      res.discontinuity = 0;
    }
    return res;
  }

  std::string AFRT::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[afrt] Fragment Run Table (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getVersion() << std::endl;
    if (getUpdate()){
      r << std::string(indent + 1, ' ') << "Update" << std::endl;
    }else{
      r << std::string(indent + 1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent + 1, ' ') << "Timescale " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "QualitySegmentUrlModifiers (" << getQualityEntryCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "FragmentRunEntryTable (" << getFragmentRunCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getFragmentRunCount(); i++){
      afrt_runtable myRun = getFragmentRun(i);
      if (myRun.duration){
        r << std::string(indent + 2, ' ') << i << ": " << myRun.firstFragment << " is at " << ((double)myRun.firstTimestamp / (double)getTimeScale())
            << "s, " << ((double)myRun.duration / (double)getTimeScale()) << "s per fragment." << std::endl;
      }else{
        r << std::string(indent + 2, ' ') << i << ": " << myRun.firstFragment << " is at " << ((double)myRun.firstTimestamp / (double)getTimeScale())
            << "s, discontinuity type " << myRun.discontinuity << std::endl;
      }
    }
    return r.str();
  }

  ASRT::ASRT(){
    memcpy(data + 4, "asrt", 4);
    setVersion(0);
    setUpdate(0);
  }

  void ASRT::setVersion(char newVersion){
    setInt8(newVersion, 0);
  }

  uint32_t ASRT::getVersion(){
    return getInt8(0);
  }

  void ASRT::setUpdate(uint32_t newUpdate){
    setInt24(newUpdate, 1);
  }

  uint32_t ASRT::getUpdate(){
    return getInt24(1);
  }

  uint32_t ASRT::getQualityEntryCount(){
    return getInt8(4);
  }

  void ASRT::setQualityEntry(std::string & newEntry, uint32_t no){
    int countLoc = 4;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    unsigned int i;
    for (i = 0; i < getQualityEntryCount() && i < no; ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    //we are now either at the end, or at the right position
    //let's reserve any unreserved space...
    if (no + 1 > getQualityEntryCount()){
      int amount = no + 1 - getQualityEntryCount();
      if ( !reserve(payloadOffset + tempLoc, 0, amount)){
        return;
      };
      memset(data + payloadOffset + tempLoc, 0, amount);
      setInt8(no + 1, countLoc); //set new qualityEntryCount
      tempLoc += no - i;
    }
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setString(newEntry, tempLoc);
  }

  const char* ASRT::getQualityEntry(uint32_t no){
    if (no > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 5; //position of qualityentry count;
    for (unsigned int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t ASRT::getSegmentRunEntryCount(){
    int tempLoc = 5; //position of qualityentry count;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void ASRT::setSegmentRun(uint32_t firstSegment, uint32_t fragmentsPerSegment, uint32_t no){
    int tempLoc = 5; //position of qualityentry count;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    int countLoc = tempLoc;
    tempLoc += 4 + no * 8;
    if (no + 1 > getInt32(countLoc)){
      setInt32(no + 1, countLoc); //set new qualityEntryCount
    }
    setInt32(firstSegment, tempLoc);
    setInt32(fragmentsPerSegment, tempLoc + 4);
  }

  asrt_runtable ASRT::getSegmentRun(uint32_t no){
    asrt_runtable res;
    if (no >= getSegmentRunEntryCount()){
      return res;
    }
    int tempLoc = 5; //position of qualityentry count;
    for (unsigned int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += 4 + 8 * no;
    res.firstSegment = getInt32(tempLoc);
    res.fragmentsPerSegment = getInt32(tempLoc + 4);
    return res;
  }

  std::string ASRT::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[asrt] Segment Run Table (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << getVersion() << std::endl;
    if (getUpdate()){
      r << std::string(indent + 1, ' ') << "Update" << std::endl;
    }else{
      r << std::string(indent + 1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent + 1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getQualityEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "SegmentRunEntryTable (" << getSegmentRunEntryCount() << ")" << std::endl;
    for (unsigned int i = 0; i < getSegmentRunEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": First=" << getSegmentRun(i).firstSegment << ", FragmentsPerSegment="
          << getSegmentRun(i).fragmentsPerSegment << std::endl;
    }
    return r.str();
  }
  AFRA::AFRA(){
    memcpy(data + 4, "afra", 4);
    setInt32(0, 9); //entrycount = 0
    setFlags(0);
  }

  void AFRA::setVersion(uint32_t newVersion){
    setInt8(newVersion, 0);
  }

  uint32_t AFRA::getVersion(){
    return getInt8(0);
  }

  void AFRA::setFlags(uint32_t newFlags){
    setInt24(newFlags, 1);
  }

  uint32_t AFRA::getFlags(){
    return getInt24(1);
  }

  void AFRA::setLongIDs(bool newVal){
    if (newVal){
      setInt8((getInt8(4) & 0x7F) + 0x80, 4);
    }else{
      setInt8((getInt8(4) & 0x7F), 4);
    }
  }

  bool AFRA::getLongIDs(){
    return getInt8(4) & 0x80;
  }

  void AFRA::setLongOffsets(bool newVal){
    if (newVal){
      setInt8((getInt8(4) & 0xBF) + 0x40, 4);
    }else{
      setInt8((getInt8(4) & 0xBF), 4);
    }
  }

  bool AFRA::getLongOffsets(){
    return getInt8(4) & 0x40;
  }

  void AFRA::setGlobalEntries(bool newVal){
    if (newVal){
      setInt8((getInt8(4) & 0xDF) + 0x20, 4);
    }else{
      setInt8((getInt8(4) & 0xDF), 4);
    }
  }

  bool AFRA::getGlobalEntries(){
    return getInt8(4) & 0x20;
  }

  void AFRA::setTimeScale(uint32_t newVal){
    setInt32(newVal, 5);
  }

  uint32_t AFRA::getTimeScale(){
    return getInt32(5);
  }

  uint32_t AFRA::getEntryCount(){
    return getInt32(9);
  }

  void AFRA::setEntry(afraentry newEntry, uint32_t no){
    int entrysize = 12;
    if (getLongOffsets()){
      entrysize = 16;
    }
    setInt64(newEntry.time, 13 + entrysize * no);
    if (getLongOffsets()){
      setInt64(newEntry.offset, 21 + entrysize * no);
    }else{
      setInt32(newEntry.offset, 21 + entrysize * no);
    }
    if (no + 1 > getEntryCount()){
      setInt32(no + 1, 9);
    }
  }

  afraentry AFRA::getEntry(uint32_t no){
    afraentry ret;
    int entrysize = 12;
    if (getLongOffsets()){
      entrysize = 16;
    }
    ret.time = getInt64(13 + entrysize * no);
    if (getLongOffsets()){
      ret.offset = getInt64(21 + entrysize * no);
    }else{
      ret.offset = getInt32(21 + entrysize * no);
    }
    return ret;
  }

  uint32_t AFRA::getGlobalEntryCount(){
    if ( !getGlobalEntries()){
      return 0;
    }
    int entrysize = 12;
    if (getLongOffsets()){
      entrysize = 16;
    }
    return getInt32(13 + entrysize * getEntryCount());
  }

  void AFRA::setGlobalEntry(globalafraentry newEntry, uint32_t no){
    int offset = 13 + 12 * getEntryCount() + 4;
    if (getLongOffsets()){
      offset = 13 + 16 * getEntryCount() + 4;
    }
    int entrysize = 20;
    if (getLongIDs()){
      entrysize += 4;
    }
    if (getLongOffsets()){
      entrysize += 8;
    }

    setInt64(newEntry.time, offset + entrysize * no);
    if (getLongIDs()){
      setInt32(newEntry.segment, offset + entrysize * no + 8);
      setInt32(newEntry.fragment, offset + entrysize * no + 12);
    }else{
      setInt16(newEntry.segment, offset + entrysize * no + 8);
      setInt16(newEntry.fragment, offset + entrysize * no + 10);
    }
    if (getLongOffsets()){
      setInt64(newEntry.afraoffset, offset + entrysize * no + entrysize - 16);
      setInt64(newEntry.offsetfromafra, offset + entrysize * no + entrysize - 8);
    }else{
      setInt32(newEntry.afraoffset, offset + entrysize * no + entrysize - 8);
      setInt32(newEntry.offsetfromafra, offset + entrysize * no + entrysize - 4);
    }

    if (getInt32(offset - 4) < no + 1){
      setInt32(no + 1, offset - 4);
    }
  }

  globalafraentry AFRA::getGlobalEntry(uint32_t no){
    globalafraentry ret;
    int offset = 13 + 12 * getEntryCount() + 4;
    if (getLongOffsets()){
      offset = 13 + 16 * getEntryCount() + 4;
    }
    int entrysize = 20;
    if (getLongIDs()){
      entrysize += 4;
    }
    if (getLongOffsets()){
      entrysize += 8;
    }

    ret.time = getInt64(offset + entrysize * no);
    if (getLongIDs()){
      ret.segment = getInt32(offset + entrysize * no + 8);
      ret.fragment = getInt32(offset + entrysize * no + 12);
    }else{
      ret.segment = getInt16(offset + entrysize * no + 8);
      ret.fragment = getInt16(offset + entrysize * no + 10);
    }
    if (getLongOffsets()){
      ret.afraoffset = getInt64(offset + entrysize * no + entrysize - 16);
      ret.offsetfromafra = getInt64(offset + entrysize * no + entrysize - 8);
    }else{
      ret.afraoffset = getInt32(offset + entrysize * no + entrysize - 8);
      ret.offsetfromafra = getInt32(offset + entrysize * no + entrysize - 4);
    }
    return ret;
  }

  std::string AFRA::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[afra] Fragment Random Access (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Flags " << getFlags() << std::endl;
    r << std::string(indent + 1, ' ') << "Long IDs " << getLongIDs() << std::endl;
    r << std::string(indent + 1, ' ') << "Long Offsets " << getLongOffsets() << std::endl;
    r << std::string(indent + 1, ' ') << "Global Entries " << getGlobalEntries() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale " << getTimeScale() << std::endl;

    uint32_t count = getEntryCount();
    r << std::string(indent + 1, ' ') << "Entries (" << count << ") " << std::endl;
    for (uint32_t i = 0; i < count; ++i){
      afraentry tmpent = getEntry(i);
      r << std::string(indent + 1, ' ') << i << ": Time " << tmpent.time << ", Offset " << tmpent.offset << std::endl;
    }

    if (getGlobalEntries()){
      count = getGlobalEntryCount();
      r << std::string(indent + 1, ' ') << "Global Entries (" << count << ") " << std::endl;
      for (uint32_t i = 0; i < count; ++i){
        globalafraentry tmpent = getGlobalEntry(i);
        r << std::string(indent + 1, ' ') << i << ": T " << tmpent.time << ", S" << tmpent.segment << "F" << tmpent.fragment << ", "
            << tmpent.afraoffset << "/" << tmpent.offsetfromafra << std::endl;
      }
    }
    return r.str();
  }
}
