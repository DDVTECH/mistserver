#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

#define Int64 long long int

/// Contains all MP4 format related code.
namespace MP4 {

  /// Creates a new box, optionally using the indicated pointer for storage.
  /// If manage is set to true, the pointer will be realloc'ed when the box needs to be resized.
  /// If the datapointer is NULL, manage is assumed to be true even if explicitly given as false.
  /// If managed, the pointer will be free'd upon destruction.
  Box::Box(char * datapointer, bool manage){
    data = datapointer;
    managed = manage;
    payloadOffset = 8;
    if (data == 0){
      clear();
    }else{
      data_size = ntohl(((int*)data)[0]);
    }
  }

  /// If managed, this will free the data pointer.
  Box::~Box(){
    if (managed && data != 0){
      free(data);
      data = 0;
    }
  }

  /// Returns the values at byte positions 4 through 7.
  std::string Box::getType(){
    return std::string(data + 4, 4);
  }

  /// Returns true if the given 4-byte boxtype is equal to the values at byte positions 4 through 7.
  bool Box::isType(const char* boxType){
    return !memcmp(boxType, data + 4, 4);
  }

  /// Reads out a whole box (if possible) from newData, copying to the internal data storage and removing from the input string.
  /// \returns True on success, false otherwise.
  bool Box::read(std::string & newData){
    if ( !managed){
      return false;
    }
    if (newData.size() > 4){
      payloadOffset = 8;
      long long int size = ntohl(((int*)newData.c_str())[0]);
      if (size == 1){
        if (newData.size() > 16){
          size = 0 + ntohl(((int*)newData.c_str())[2]);
          size <<= 32;
          size += ntohl(((int*)newData.c_str())[3]);
          payloadOffset = 16;
        }else{
          return false;
        }
      }
      if (newData.size() >= size){
        void * ret = malloc(size);
        if ( !ret){
          return false;
        }
        free(data);
        data = (char*)ret;
        memcpy(data, newData.c_str(), size);
        newData.erase(0, size);
        return true;
      }
    }
    return false;
  }

  /// Returns the total boxed size of this box, including the header.
  long long int Box::boxedSize(){
    if (payloadOffset == 16){
      return ((long long int)ntohl(((int*)data)[2]) << 32) + ntohl(((int*)data)[3]);
    }
    return ntohl(((int*)data)[0]);
  }

  /// Retruns the size of the payload of thix box, excluding the header.
  /// This value is defined as boxedSize() - 8.
  long long int Box::payloadSize(){
    return boxedSize() - payloadOffset;
  }

  /// Returns a copy of the data pointer.
  char * Box::asBox(){
    return data;
  }

  char * Box::payload(){
    return data + payloadOffset;
  }

  /// Makes this box managed if it wasn't already, resetting the internal storage to 8 bytes (the minimum).
  /// If this box wasn't managed, the original data is left intact - otherwise it is free'd.
  /// If it was somehow impossible to allocate 8 bytes (should never happen), this will cause segfaults later.
  void Box::clear(){
    if (data && managed){
      free(data);
    }
    managed = true;
    payloadOffset = 8;
    data = (char*)malloc(8);
    if (data){
      data_size = 8;
      ((int*)data)[0] = htonl(data_size);
    }else{
      data_size = 0;
    }
  }

  /// Attempts to typecast this Box to a more specific type and call the toPrettyString() function of that type.
  /// If this failed, it will print out a message saying pretty-printing is not implemented for <boxtype>.
  std::string Box::toPrettyString(int indent){
    switch (ntohl( *((int*)(data + 4)))){ //type is at this address
      case 0x6D666864:
        return ((MFHD*)this)->toPrettyString(indent);
        break;
      case 0x6D6F6F66:
        return ((MOOF*)this)->toPrettyString(indent);
        break;
      case 0x61627374:
        return ((ABST*)this)->toPrettyString(indent);
        break;
      case 0x61667274:
        return ((AFRT*)this)->toPrettyString(indent);
        break;
      case 0x61667261:
        return ((AFRA*)this)->toPrettyString(indent);
        break;
      case 0x61737274:
        return ((ASRT*)this)->toPrettyString(indent);
        break;
      case 0x7472756E:
        return ((TRUN*)this)->toPrettyString(indent);
        break;
      case 0x74726166:
        return ((TRAF*)this)->toPrettyString(indent);
        break;
      case 0x74666864:
        return ((TFHD*)this)->toPrettyString(indent);
        break;
      case 0x61766343:
        return ((AVCC*)this)->toPrettyString(indent);
        break;
      default:
        break;
    }
    return std::string(indent, ' ') + "Unimplemented pretty-printing for box " + std::string(data + 4, 4) + "\n";
  }

  /// Sets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt8(char newData, size_t index){
    index += payloadOffset;
    if (index >= boxedSize()){
      if ( !reserve(index, 0, 1)){
        return;
      }
    }
    data[index] = newData;
  }

  /// Gets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  char Box::getInt8(size_t index){
    index += payloadOffset;
    if (index >= boxedSize()){
      if ( !reserve(index, 0, 1)){
        return 0;
      }
      setInt8(0, index - payloadOffset);
    }
    return data[index];
  }

  /// Sets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt16(short newData, size_t index){
    index += payloadOffset;
    if (index + 1 >= boxedSize()){
      if ( !reserve(index, 0, 2)){
        return;
      }
    }
    newData = htons(newData);
    memcpy(data + index, (char*) &newData, 2);
  }

  /// Gets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  short Box::getInt16(size_t index){
    index += payloadOffset;
    if (index + 1 >= boxedSize()){
      if ( !reserve(index, 0, 2)){
        return 0;
      }
      setInt16(0, index - payloadOffset);
    }
    short result;
    memcpy((char*) &result, data + index, 2);
    return ntohs(result);
  }

  /// Sets the 24 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt24(long newData, size_t index){
    index += payloadOffset;
    if (index + 2 >= boxedSize()){
      if ( !reserve(index, 0, 3)){
        return;
      }
    }
    data[index] = (newData & 0x00FF0000) >> 16;
    data[index + 1] = (newData & 0x0000FF00) >> 8;
    data[index + 2] = (newData & 0x000000FF);
  }

  /// Gets the 24 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  long Box::getInt24(size_t index){
    index += payloadOffset;
    if (index + 2 >= boxedSize()){
      if ( !reserve(index, 0, 3)){
        return 0;
      }
      setInt24(0, index - payloadOffset);
    }
    long result = data[index];
    result <<= 8;
    result += data[index + 1];
    result <<= 8;
    result += data[index + 2];
    return result;
  }

  /// Sets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt32(long newData, size_t index){
    index += payloadOffset;
    if (index + 3 >= boxedSize()){
      if ( !reserve(index, 0, 4)){
        return;
      }
    }
    newData = htonl(newData);
    memcpy(data + index, (char*) &newData, 4);
  }

  /// Gets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  long Box::getInt32(size_t index){
    index += payloadOffset;
    if (index + 3 >= boxedSize()){
      if ( !reserve(index, 0, 4)){
        return 0;
      }
      setInt32(0, index - payloadOffset);
    }
    long result;
    memcpy((char*) &result, data + index, 4);
    return ntohl(result);
  }

  /// Sets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt64(Int64 newData, size_t index){
    index += payloadOffset;
    if (index + 7 >= boxedSize()){
      if ( !reserve(index, 0, 8)){
        return;
      }
    }
    ((int*)(data + index))[0] = htonl((int)(newData >> 32));
    ((int*)(data + index))[1] = htonl((int)(newData & 0xFFFFFFFF));
  }

  /// Gets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  Int64 Box::getInt64(size_t index){
    index += payloadOffset;
    if (index + 7 >= boxedSize()){
      if ( !reserve(index, 0, 8)){
        return 0;
      }
      setInt64(0, index - payloadOffset);
    }
    Int64 result = ntohl(((int*)(data + index))[0]);
    result <<= 32;
    result += ntohl(((int*)(data + index))[1]);
    return result;
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(std::string newData, size_t index){
    setString((char*)newData.c_str(), newData.size(), index);
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(char* newData, size_t size, size_t index){
    index += payloadOffset;
    if (index >= boxedSize()){
      if ( !reserve(index, 0, 1)){
        return;
      }
      data[index] = 0;
    }
    if (getStringLen(index) != size){
      if ( !reserve(index, getStringLen(index) + 1, size + 1)){
        return;
      }
    }
    memcpy(data + index, newData, size + 1);
  }

  /// Gets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string is out of range.
  /// Returns null if resizing failed.
  char * Box::getString(size_t index){
    index += payloadOffset;
    if (index >= boxedSize()){
      if ( !reserve(index, 0, 1)){
        return 0;
      }
      data[index] = 0;
    }
    return data + index;
  }

  /// Returns the length of the NULL-terminated string at the given index.
  /// Returns 0 if out of range.
  size_t Box::getStringLen(size_t index){
    index += payloadOffset;
    if (index >= boxedSize()){
      return 0;
    }
    return strlen(data + index);
  }

  /// Gets a reference to the box at the given index.
  /// Do not store or copy this reference, for there will be raptors.
  /// Will attempt to resize if out of range.
  /// Returns an 8-byte error box if resizing failed.
  Box & Box::getBox(size_t index){
    static Box retbox;
    index += payloadOffset;
    if (index + 8 > boxedSize()){
      if ( !reserve(index, 0, 8)){
        retbox = Box((char*)"\000\000\000\010erro", false);
        return retbox;
      }
      memcpy(data + index, "\000\000\000\010erro", 8);
    }
    retbox = Box(data + index, false);
    return retbox;
  }

  /// Returns the size of the box at the given position.
  /// Returns undefined values if there is no box at the given position.
  /// Returns 0 if out of range.
  size_t Box::getBoxLen(size_t index){
    if (index + payloadOffset + 8 > boxedSize()){
      return 0;
    }
    return getBox(index).boxedSize();
  }

  /// Replaces the existing box at the given index by the new box newEntry.
  /// Will resize if needed, will reserve new space if out of range.
  void Box::setBox(Box & newEntry, size_t index){
    int oldlen = getBoxLen(index);
    int newlen = newEntry.boxedSize();
    if (oldlen != newlen && !reserve(index + payloadOffset, oldlen, newlen)){
      return;
    }
    memcpy(data + index + payloadOffset, newEntry.asBox(), newlen);
  }

  /// Attempts to reserve enough space for wanted bytes of data at given position, where current bytes of data is now reserved.
  /// This will move any existing data behind the currently reserved space to the proper location after reserving.
  /// \returns True on success, false otherwise.
  bool Box::reserve(size_t position, size_t current, size_t wanted){
    if (current == wanted){
      return true;
    }
    if (position > boxedSize()){
      wanted += position - boxedSize();
    }
    if (current < wanted){
      //make bigger
      if (boxedSize() + (wanted - current) > data_size){
        //realloc if managed, otherwise fail
        if ( !managed){
          return false;
        }
        void * ret = realloc(data, boxedSize() + (wanted - current));
        if ( !ret){
          return false;
        }
        data = (char*)ret;
        memset(data + boxedSize(), 0, wanted - current); //initialize to 0
        data_size = boxedSize() + (wanted - current);
      }
    }
    //move data behind, if any
    if (boxedSize() > (position + current)){
      memmove(data + position + wanted, data + position + current, boxedSize() - (position + current));
    }
    //calculate and set new size
    if (payloadOffset != 16){
      int newSize = boxedSize() + (wanted - current);
      ((int*)data)[0] = htonl(newSize);
    }
    return true;
  }

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

  void ABST::setFlags(long newFlags){
    setInt24(newFlags, 1);
  }

  long ABST::getFlags(){
    return getInt24(1);
  }

  void ABST::setBootstrapinfoVersion(long newVersion){
    setInt32(newVersion, 4);
  }

  long ABST::getBootstrapinfoVersion(){
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

  void ABST::setTimeScale(long newScale){
    setInt32(newScale, 9);
  }

  long ABST::getTimeScale(){
    return getInt32(9);
  }

  void ABST::setCurrentMediaTime(Int64 newTime){
    setInt64(newTime, 13);
  }

  Int64 ABST::getCurrentMediaTime(){
    return getInt64(13);
  }

  void ABST::setSmpteTimeCodeOffset(Int64 newTime){
    setInt64(newTime, 21);
  }

  Int64 ABST::getSmpteTimeCodeOffset(){
    return getInt64(21);
  }

  void ABST::setMovieIdentifier(std::string & newIdentifier){
    setString(newIdentifier, 29);
  }

  char* ABST::getMovieIdentifier(){
    return getString(29);
  }

  long ABST::getServerEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1;
    return getInt8(countLoc);
  }

  void ABST::setServerEntry(std::string & newEntry, long no){
    int countLoc = 29 + getStringLen(29) + 1;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    int i;
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
  const char* ABST::getServerEntry(long no){
    if (no + 1 > getServerEntryCount()){
      return "";
    }
    int tempLoc = 29 + getStringLen(29) + 1 + 1; //position of first entry
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  long ABST::getQualityEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      countLoc += getStringLen(countLoc) + 1;
    }
    return getInt8(countLoc);
  }

  void ABST::setQualityEntry(std::string & newEntry, long no){
    int countLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      countLoc += getStringLen(countLoc) + 1;
    }
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    int i;
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

  const char* ABST::getQualityEntry(long no){
    if (no > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 29 + getStringLen(29) + 1 + 1; //position of serverentries;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += 1; //first qualityentry
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  void ABST::setDrmData(std::string newDrm){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    setString(newDrm, tempLoc);
  }

  char* ABST::getDrmData(){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  void ABST::setMetaData(std::string newMetaData){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1;
    setString(newMetaData, tempLoc);
  }

  char* ABST::getMetaData(){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1;
    return getString(tempLoc);
  }

  long ABST::getSegmentRunTableCount(){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    return getInt8(tempLoc);
  }

  void ABST::setSegmentRunTable(ASRT & newSegment, long no){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    int countLoc = tempLoc;
    tempLoc++; //skip segmentRuntableCount
    //attempt to reach the wanted position
    int i;
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

  ASRT & ABST::getSegmentRunTable(long no){
    static Box result;
    if (no > getSegmentRunTableCount()){
      static Box res;
      return (ASRT&)res;
    }
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    int countLoc = tempLoc;
    tempLoc++; //segmentRuntableCount
    for (int i = 0; i < no; ++i){
      tempLoc += getBoxLen(tempLoc);
    }
    return (ASRT&)getBox(tempLoc);
  }

  long ABST::getFragmentRunTableCount(){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    return getInt8(tempLoc);
  }

  void ABST::setFragmentRunTable(AFRT & newFragment, long no){
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    int countLoc = tempLoc;
    tempLoc++;
    //attempt to reach the wanted position
    int i;
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
    //now, tempLoc is at position for string number no, and we have at least 1 byte reserved.
    setBox(newFragment, tempLoc);
  }

  AFRT & ABST::getFragmentRunTable(long no){
    static Box result;
    if (no >= getFragmentRunTableCount()){
      static Box res;
      return (AFRT&)res;
    }
    long tempLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc++;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    tempLoc += getStringLen(tempLoc) + 1; //DrmData
    tempLoc += getStringLen(tempLoc) + 1; //MetaData
    for (int i = getInt8(tempLoc++); i != 0; --i){
      tempLoc += getBoxLen(tempLoc);
    }
    int countLoc = tempLoc;
    tempLoc++;
    for (int i = 0; i < no; i++){
      tempLoc += getBoxLen(tempLoc);
    }
    return (AFRT&)getBox(tempLoc);
  }

  std::string ABST::toPrettyString(long indent){
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
    for (int i = 0; i < getServerEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getServerEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for (int i = 0; i < getQualityEntryCount(); i++){
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

  long AFRT::getVersion(){
    return getInt8(0);
  }

  void AFRT::setUpdate(long newUpdate){
    setInt24(newUpdate, 1);
  }

  long AFRT::getUpdate(){
    return getInt24(1);
  }

  void AFRT::setTimeScale(long newScale){
    setInt32(newScale, 4);
  }

  long AFRT::getTimeScale(){
    return getInt32(4);
  }

  long AFRT::getQualityEntryCount(){
    return getInt8(8);
  }

  void AFRT::setQualityEntry(std::string & newEntry, long no){
    int countLoc = 8;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    int i;
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

  const char* AFRT::getQualityEntry(long no){
    if (no + 1 > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 9; //position of first quality entry
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  long AFRT::getFragmentRunCount(){
    int tempLoc = 9;
    for (int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void AFRT::setFragmentRun(afrt_runtable newRun, long no){
    int tempLoc = 9;
    for (int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    int countLoc = tempLoc;
    tempLoc += 4;
    for (int i = 0; i < no; i++){
      if (i + 1 > getInt32(countLoc)){
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
    if (getInt32(countLoc) < no + 1){
      setInt32(no + 1, countLoc);
    }
  }

  afrt_runtable AFRT::getFragmentRun(long no){
    afrt_runtable res;
    if (no > getFragmentRunCount()){
      return res;
    }
    int tempLoc = 9;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    int countLoc = tempLoc;
    tempLoc += 4;
    for (int i = 0; i < no; i++){
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

  std::string AFRT::toPrettyString(int indent){
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
    for (int i = 0; i < getQualityEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "FragmentRunEntryTable (" << getFragmentRunCount() << ")" << std::endl;
    for (int i = 0; i < getFragmentRunCount(); i++){
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

  long ASRT::getVersion(){
    return getInt8(0);
  }

  void ASRT::setUpdate(long newUpdate){
    setInt24(newUpdate, 1);
  }

  long ASRT::getUpdate(){
    return getInt24(1);
  }

  long ASRT::getQualityEntryCount(){
    return getInt8(4);
  }

  void ASRT::setQualityEntry(std::string & newEntry, long no){
    int countLoc = 4;
    int tempLoc = countLoc + 1;
    //attempt to reach the wanted position
    int i;
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

  const char* ASRT::getQualityEntry(long no){
    if (no > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  long ASRT::getSegmentRunEntryCount(){
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void ASRT::setSegmentRun(long firstSegment, long fragmentsPerSegment, long no){
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < getQualityEntryCount(); i++){
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

  asrt_runtable ASRT::getSegmentRun(long no){
    asrt_runtable res;
    if (no >= getSegmentRunEntryCount()){
      return res;
    }
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    int countLoc = tempLoc;
    tempLoc += 4 + 8 * no;
    res.firstSegment = getInt32(tempLoc);
    res.fragmentsPerSegment = getInt32(tempLoc + 4);
    return res;
  }

  std::string ASRT::toPrettyString(int indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[asrt] Segment Run Table (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << getVersion() << std::endl;
    if (getUpdate()){
      r << std::string(indent + 1, ' ') << "Update" << std::endl;
    }else{
      r << std::string(indent + 1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent + 1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for (int i = 0; i < getQualityEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": " << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent + 1, ' ') << "SegmentRunEntryTable (" << getSegmentRunEntryCount() << ")" << std::endl;
    for (int i = 0; i < getSegmentRunEntryCount(); i++){
      r << std::string(indent + 2, ' ') << i << ": First=" << getSegmentRun(i).firstSegment << ", FragmentsPerSegment="
          << getSegmentRun(i).fragmentsPerSegment << std::endl;
    }
    return r.str();
  }

  MFHD::MFHD(){
    memcpy(data + 4, "mfhd", 4);
    setInt32(0, 0);
  }

  void MFHD::setSequenceNumber(long newSequenceNumber){
    setInt32(newSequenceNumber, 4);
  }

  long MFHD::getSequenceNumber(){
    return getInt32(4);
  }

  std::string MFHD::toPrettyString(int indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mfhd] Movie Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "SequenceNumber " << getSequenceNumber() << std::endl;
    return r.str();
  }

  MOOF::MOOF(){
    memcpy(data + 4, "moof", 4);
  }

  long MOOF::getContentCount(){
    int res = 0;
    int tempLoc = 0;
    while (tempLoc < boxedSize() - 8){
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  void MOOF::setContent(Box & newContent, long no){
    int tempLoc = 0;
    int contentCount = getContentCount();
    for (int i = 0; i < no; i++){
      if (i < contentCount){
        tempLoc += getBoxLen(tempLoc);
      }else{
        if ( !reserve(tempLoc, 0, (no - contentCount) * 8)){
          return;
        };
        memset(data + tempLoc, 0, (no - contentCount) * 8);
        tempLoc += (no - contentCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
  }

  Box & MOOF::getContent(long no){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if (no > getContentCount()){
      return ret;
    }
    int i = 0;
    int tempLoc = 0;
    while (i < no){
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string MOOF::toPrettyString(int indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[moof] Movie Fragment Box (" << boxedSize() << ")" << std::endl;
    Box curBox;
    int tempLoc = 0;
    int contentCount = getContentCount();
    for (int i = 0; i < contentCount; i++){
      curBox = getContent(i);
      r << curBox.toPrettyString(indent + 1);
      tempLoc += getBoxLen(tempLoc);
    }
    return r.str();
  }

  TRAF::TRAF(){
    memcpy(data + 4, "traf", 4);
  }

  long TRAF::getContentCount(){
    int res = 0;
    int tempLoc = 0;
    while (tempLoc < boxedSize() - 8){
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  void TRAF::setContent(Box & newContent, long no){
    int tempLoc = 0;
    int contentCount = getContentCount();
    for (int i = 0; i < no; i++){
      if (i < contentCount){
        tempLoc += getBoxLen(tempLoc);
      }else{
        if ( !reserve(tempLoc, 0, (no - contentCount) * 8)){
          return;
        };
        memset(data + tempLoc, 0, (no - contentCount) * 8);
        tempLoc += (no - contentCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
  }

  Box & TRAF::getContent(long no){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if (no > getContentCount()){
      return ret;
    }
    int i = 0;
    int tempLoc = 0;
    while (i < no){
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string TRAF::toPrettyString(int indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[traf] Track Fragment Box (" << boxedSize() << ")" << std::endl;
    Box curBox;
    int tempLoc = 0;
    int contentCount = getContentCount();
    for (int i = 0; i < contentCount; i++){
      curBox = getContent(i);
      r << curBox.toPrettyString(indent + 1);
      tempLoc += curBox.boxedSize();
    }
    return r.str();
  }

  TRUN::TRUN(){
    memcpy(data + 4, "trun", 4);
  }

  void TRUN::setFlags(long newFlags){
    setInt24(newFlags, 1);
  }

  long TRUN::getFlags(){
    return getInt24(1);
  }

  void TRUN::setDataOffset(long newOffset){
    if (getFlags() & trundataOffset){
      setInt32(newOffset, 8);
    }
  }

  long TRUN::getDataOffset(){
    if (getFlags() & trundataOffset){
      return getInt32(8);
    }else{
      return 0;
    }
  }

  void TRUN::setFirstSampleFlags(long newSampleFlags){
    if ( !(getFlags() & trunfirstSampleFlags)){
      return;
    }
    if (getFlags() & trundataOffset){
      setInt32(newSampleFlags, 12);
    }else{
      setInt32(newSampleFlags, 8);
    }
  }

  long TRUN::getFirstSampleFlags(){
    if ( !(getFlags() & trunfirstSampleFlags)){
      return 0;
    }
    if (getFlags() & trundataOffset){
      return getInt32(12);
    }else{
      return getInt32(8);
    }
  }

  long TRUN::getSampleInformationCount(){
    return getInt32(4);
  }

  void TRUN::setSampleInformation(trunSampleInformation newSample, long no){
    long flags = getFlags();
    long sampInfoSize = 0;
    if (flags & trunsampleDuration){
      sampInfoSize += 4;
    }
    if (flags & trunsampleSize){
      sampInfoSize += 4;
    }
    if (flags & trunsampleFlags){
      sampInfoSize += 4;
    }
    if (flags & trunsampleOffsets){
      sampInfoSize += 4;
    }
    long offset = 8;
    if (flags & trundataOffset){
      offset += 4;
    }
    if (flags & trunfirstSampleFlags){
      offset += 4;
    }
    long innerOffset = 0;
    if (flags & trunsampleDuration){
      setInt32(newSample.sampleDuration, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleSize){
      setInt32(newSample.sampleSize, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleFlags){
      setInt32(newSample.sampleFlags, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleOffsets){
      setInt32(newSample.sampleOffset, offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (getSampleInformationCount() < no + 1){
      setInt32(no + 1, 4);
    }
  }

  trunSampleInformation TRUN::getSampleInformation(long no){
    trunSampleInformation ret;
    ret.sampleDuration = 0;
    ret.sampleSize = 0;
    ret.sampleFlags = 0;
    ret.sampleOffset = 0;
    if (getSampleInformationCount() < no + 1){
      return ret;
    }
    long flags = getFlags();
    long sampInfoSize = 0;
    if (flags & trunsampleDuration){
      sampInfoSize += 4;
    }
    if (flags & trunsampleSize){
      sampInfoSize += 4;
    }
    if (flags & trunsampleFlags){
      sampInfoSize += 4;
    }
    if (flags & trunsampleOffsets){
      sampInfoSize += 4;
    }
    long offset = 8;
    if (flags & trundataOffset){
      offset += 4;
    }
    if (flags & trunfirstSampleFlags){
      offset += 4;
    }
    long innerOffset = 0;
    if (flags & trunsampleDuration){
      ret.sampleDuration = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleSize){
      ret.sampleSize = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleFlags){
      ret.sampleFlags = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    if (flags & trunsampleOffsets){
      ret.sampleOffset = getInt32(offset + no * sampInfoSize + innerOffset);
      innerOffset += 4;
    }
    return ret;
  }

  std::string TRUN::toPrettyString(long indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[trun] Track Fragment Run (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    long flags = getFlags();
    r << std::string(indent + 1, ' ') << "Flags";
    if (flags & trundataOffset){
      r << " dataOffset";
    }
    if (flags & trunfirstSampleFlags){
      r << " firstSampleFlags";
    }
    if (flags & trunsampleDuration){
      r << " sampleDuration";
    }
    if (flags & trunsampleSize){
      r << " sampleSize";
    }
    if (flags & trunsampleFlags){
      r << " sampleFlags";
    }
    if (flags & trunsampleOffsets){
      r << " sampleOffsets";
    }
    r << std::endl;

    if (flags & trundataOffset){
      r << std::string(indent + 1, ' ') << "Data Offset " << getDataOffset() << std::endl;
    }
    if (flags & trundataOffset){
      r << std::string(indent + 1, ' ') << "Sample Flags" << prettySampleFlags(getFirstSampleFlags()) << std::endl;
    }

    r << std::string(indent + 1, ' ') << "SampleInformation (" << getSampleInformationCount() << "):" << std::endl;
    for (int i = 0; i < getSampleInformationCount(); ++i){
      r << std::string(indent + 2, ' ') << "[" << i << "]" << std::endl;
      trunSampleInformation samp = getSampleInformation(i);
      if (flags & trunsampleDuration){
        r << std::string(indent + 2, ' ') << "Duration " << samp.sampleDuration << std::endl;
      }
      if (flags & trunsampleSize){
        r << std::string(indent + 2, ' ') << "Size " << samp.sampleSize << std::endl;
      }
      if (flags & trunsampleFlags){
        r << std::string(indent + 2, ' ') << "Flags " << prettySampleFlags(samp.sampleFlags) << std::endl;
      }
      if (flags & trunsampleOffsets){
        r << std::string(indent + 2, ' ') << "Offset " << samp.sampleOffset << std::endl;
      }
    }

    return r.str();
  }

  std::string prettySampleFlags(long flag){
    std::stringstream r;
    if (flag & noIPicture){
      r << " noIPicture";
    }
    if (flag & isIPicture){
      r << " isIPicture";
    }
    if (flag & noDisposable){
      r << " noDisposable";
    }
    if (flag & isDisposable){
      r << " isDisposable";
    }
    if (flag & isRedundant){
      r << " isRedundant";
    }
    if (flag & noRedundant){
      r << " noRedundant";
    }
    if (flag & noKeySample){
      r << " noKeySample";
    }else{
      r << " isKeySample";
    }
    return r.str();
  }

  TFHD::TFHD(){
    memcpy(data + 4, "tfhd", 4);
  }

  void TFHD::setFlags(long newFlags){
    setInt24(newFlags, 1);
  }

  long TFHD::getFlags(){
    return getInt24(1);
  }

  void TFHD::setTrackID(long newID){
    setInt32(newID, 4);
  }

  long TFHD::getTrackID(){
    return getInt32(4);
  }

  void TFHD::setBaseDataOffset(long long newOffset){
    if (getFlags() & tfhdBaseOffset){
      setInt64(newOffset, 8);
    }
  }

  long long TFHD::getBaseDataOffset(){
    if (getFlags() & tfhdBaseOffset){
      return getInt64(8);
    }else{
      return 0;
    }
  }

  void TFHD::setSampleDescriptionIndex(long newIndex){
    if ( !(getFlags() & tfhdSampleDesc)){
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    setInt32(newIndex, offset);
  }

  long TFHD::getSampleDescriptionIndex(){
    if ( !(getFlags() & tfhdSampleDesc)){
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleDuration(long newDuration){
    if ( !(getFlags() & tfhdSampleDura)){
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    setInt32(newDuration, offset);
  }

  long TFHD::getDefaultSampleDuration(){
    if ( !(getFlags() & tfhdSampleDura)){
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleSize(long newSize){
    if ( !(getFlags() & tfhdSampleSize)){
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura){
      offset += 4;
    }
    setInt32(newSize, offset);
  }

  long TFHD::getDefaultSampleSize(){
    if ( !(getFlags() & tfhdSampleSize)){
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura){
      offset += 4;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleFlags(long newFlags){
    if ( !(getFlags() & tfhdSampleFlag)){
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura){
      offset += 4;
    }
    if (getFlags() & tfhdSampleSize){
      offset += 4;
    }
    setInt32(newFlags, offset);
  }

  long TFHD::getDefaultSampleFlags(){
    if ( !(getFlags() & tfhdSampleFlag)){
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    if (getFlags() & tfhdSampleDesc){
      offset += 4;
    }
    if (getFlags() & tfhdSampleDura){
      offset += 4;
    }
    if (getFlags() & tfhdSampleSize){
      offset += 4;
    }
    return getInt32(offset);
  }

  std::string TFHD::toPrettyString(long indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfhd] Track Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    long flags = getFlags();
    r << std::string(indent + 1, ' ') << "Flags";
    if (flags & tfhdBaseOffset){
      r << " BaseOffset";
    }
    if (flags & tfhdSampleDesc){
      r << " SampleDesc";
    }
    if (flags & tfhdSampleDura){
      r << " SampleDura";
    }
    if (flags & tfhdSampleSize){
      r << " SampleSize";
    }
    if (flags & tfhdSampleFlag){
      r << " SampleFlag";
    }
    if (flags & tfhdNoDuration){
      r << " NoDuration";
    }
    r << std::endl;

    r << std::string(indent + 1, ' ') << "TrackID " << getTrackID() << std::endl;

    if (flags & tfhdBaseOffset){
      r << std::string(indent + 1, ' ') << "Base Offset " << getBaseDataOffset() << std::endl;
    }
    if (flags & tfhdSampleDesc){
      r << std::string(indent + 1, ' ') << "Sample Description Index " << getSampleDescriptionIndex() << std::endl;
    }
    if (flags & tfhdSampleDura){
      r << std::string(indent + 1, ' ') << "Default Sample Duration " << getDefaultSampleDuration() << std::endl;
    }
    if (flags & tfhdSampleSize){
      r << std::string(indent + 1, ' ') << "Default Same Size " << getDefaultSampleSize() << std::endl;
    }
    if (flags & tfhdSampleFlag){
      r << std::string(indent + 1, ' ') << "Default Sample Flags " << prettySampleFlags(getDefaultSampleFlags()) << std::endl;
    }

    return r.str();
  }

  AFRA::AFRA(){
    memcpy(data + 4, "afra", 4);
    setInt32(0, 9); //entrycount = 0
    setFlags(0);
  }

  void AFRA::setVersion(long newVersion){
    setInt8(newVersion, 0);
  }

  long AFRA::getVersion(){
    return getInt8(0);
  }

  void AFRA::setFlags(long newFlags){
    setInt24(newFlags, 1);
  }

  long AFRA::getFlags(){
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

  void AFRA::setTimeScale(long newVal){
    setInt32(newVal, 5);
  }

  long AFRA::getTimeScale(){
    return getInt32(5);
  }

  long AFRA::getEntryCount(){
    return getInt32(9);
  }

  void AFRA::setEntry(afraentry newEntry, long no){
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

  afraentry AFRA::getEntry(long no){
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

  long AFRA::getGlobalEntryCount(){
    if ( !getGlobalEntries()){
      return 0;
    }
    int entrysize = 12;
    if (getLongOffsets()){
      entrysize = 16;
    }
    return getInt32(13 + entrysize * getEntryCount());
  }

  void AFRA::setGlobalEntry(globalafraentry newEntry, long no){
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

  globalafraentry AFRA::getGlobalEntry(long no){
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

  std::string AFRA::toPrettyString(long indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[afra] Fragment Random Access (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Flags " << getFlags() << std::endl;
    r << std::string(indent + 1, ' ') << "Long IDs " << getLongIDs() << std::endl;
    r << std::string(indent + 1, ' ') << "Long Offsets " << getLongOffsets() << std::endl;
    r << std::string(indent + 1, ' ') << "Global Entries " << getGlobalEntries() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale " << getTimeScale() << std::endl;

    long count = getEntryCount();
    r << std::string(indent + 1, ' ') << "Entries (" << count << ") " << std::endl;
    for (long i = 0; i < count; ++i){
      afraentry tmpent = getEntry(i);
      r << std::string(indent + 1, ' ') << i << ": Time " << tmpent.time << ", Offset " << tmpent.offset << std::endl;
    }

    if (getGlobalEntries()){
      count = getGlobalEntryCount();
      r << std::string(indent + 1, ' ') << "Global Entries (" << count << ") " << std::endl;
      for (long i = 0; i < count; ++i){
        globalafraentry tmpent = getGlobalEntry(i);
        r << std::string(indent + 1, ' ') << i << ": T " << tmpent.time << ", S" << tmpent.segment << "F" << tmpent.fragment << ", "
            << tmpent.afraoffset << "/" << tmpent.offsetfromafra << std::endl;
      }
    }

    return r.str();
  }

  AVCC::AVCC(){
    memcpy(data + 4, "avcC", 4);
    setInt8(0xFF, 4); //reserved + 4-bytes NAL length
  }

  void AVCC::setVersion(long newVersion){
    setInt8(newVersion, 0);
  }

  long AVCC::getVersion(){
    return getInt8(0);
  }

  void AVCC::setProfile(long newProfile){
    setInt8(newProfile, 1);
  }

  long AVCC::getProfile(){
    return getInt8(1);
  }

  void AVCC::setCompatibleProfiles(long newCompatibleProfiles){
    setInt8(newCompatibleProfiles, 2);
  }

  long AVCC::getCompatibleProfiles(){
    return getInt8(2);
  }

  void AVCC::setLevel(long newLevel){
    setInt8(newLevel, 3);
  }

  long AVCC::getLevel(){
    return getInt8(3);
  }

  void AVCC::setSPSNumber(long newSPSNumber){
    setInt8(newSPSNumber, 5);
  }

  long AVCC::getSPSNumber(){
    return getInt8(5);
  }

  void AVCC::setSPS(std::string newSPS){
    setInt16(newSPS.size(), 6);
    for (int i = 0; i < newSPS.size(); i++){
      setInt8(newSPS[i], 8 + i);
    } //not null-terminated
  }

  long AVCC::getSPSLen(){
    return getInt16(6);
  }

  char* AVCC::getSPS(){
    return payload() + 8;
  }

  void AVCC::setPPSNumber(long newPPSNumber){
    int offset = 8 + getSPSLen();
    setInt8(newPPSNumber, offset);
  }

  long AVCC::getPPSNumber(){
    int offset = 8 + getSPSLen();
    return getInt8(offset);
  }

  void AVCC::setPPS(std::string newPPS){
    int offset = 8 + getSPSLen() + 1;
    setInt16(newPPS.size(), offset);
    for (int i = 0; i < newPPS.size(); i++){
      setInt8(newPPS[i], offset + 2 + i);
    } //not null-terminated
  }

  long AVCC::getPPSLen(){
    int offset = 8 + getSPSLen() + 1;
    return getInt16(offset);
  }

  char* AVCC::getPPS(){
    int offset = 8 + getSPSLen() + 3;
    return payload() + offset;
  }

  std::string AVCC::toPrettyString(long indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[avcC] H.264 Init Data (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Profile: " << getProfile() << std::endl;
    r << std::string(indent + 1, ' ') << "Compatible Profiles: " << getCompatibleProfiles() << std::endl;
    r << std::string(indent + 1, ' ') << "Level: " << getLevel() << std::endl;
    r << std::string(indent + 1, ' ') << "SPS Number: " << getSPSNumber() << std::endl;
    r << std::string(indent + 2, ' ') << getSPSLen() << " of SPS data" << std::endl;
    r << std::string(indent + 1, ' ') << "PPS Number: " << getPPSNumber() << std::endl;
    r << std::string(indent + 2, ' ') << getPPSLen() << " of PPS data" << std::endl;
    return r.str();
  }

  std::string AVCC::asAnnexB(){
    std::stringstream r;
    r << (char)0x00 << (char)0x00 << (char)0x00 << (char)0x01;
    r.write(getSPS(), getSPSLen());
    r << (char)0x00 << (char)0x00 << (char)0x00 << (char)0x01;
    r.write(getPPS(), getPPSLen());
    return r.str();
  }

  void AVCC::setPayload(std::string newPayload){
    if ( !reserve(0, payloadSize(), newPayload.size())){
      std::cerr << "Cannot allocate enough memory for payload" << std::endl;
      return;
    }
    memcpy((char*)payload(), (char*)newPayload.c_str(), newPayload.size());
  }

  SDTP::SDTP(){
    memcpy(data + 4, "sdtp", 4);
  }

  void SDTP::setVersion(long newVersion){
    setInt8(newVersion, 0);
  }

  long SDTP::getVersion(){
    return getInt8(0);
  }

  void SDTP::setValue(long newValue, size_t index){
    setInt8(newValue, index);
  }

  long SDTP::getValue(size_t index){
    getInt8(index);
  }
}
