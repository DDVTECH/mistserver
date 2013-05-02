#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

#define Int64 uint64_t

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
      uint64_t size = ntohl(((int*)newData.c_str())[0]);
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
  uint64_t Box::boxedSize(){
    if (payloadOffset == 16){
      return ((uint64_t)ntohl(((int*)data)[2]) << 32) + ntohl(((int*)data)[3]);
    }
    return ntohl(((int*)data)[0]);
  }

  /// Retruns the size of the payload of thix box, excluding the header.
  /// This value is defined as boxedSize() - 8.
  uint64_t Box::payloadSize(){
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
  std::string Box::toPrettyString(uint32_t indent){
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
      case 0x73647470:
        return ((SDTP*)this)->toPrettyString(indent);
        break;
      case 0x66747970:
        return ((FTYP*)this)->toPrettyString(indent);
        break;
      case 0x6D6F6F76:
        return ((MOOV*)this)->toPrettyString(indent);
        break;
      case 0x6D766578:
        return ((MVEX*)this)->toPrettyString(indent);
        break;
      case 0x74726578:
        return ((TREX*)this)->toPrettyString(indent);
        break;
      case 0x6D667261:
        return ((MFRA*)this)->toPrettyString(indent);
        break;
      case 0x7472616B:
        return ((TRAK*)this)->toPrettyString(indent);
        break;
      case 0x6D646961:
        return ((MDIA*)this)->toPrettyString(indent);
        break;
      case 0x6D696E66:
        return ((MINF*)this)->toPrettyString(indent);
        break;
      case 0x64696E66:
        return ((DINF*)this)->toPrettyString(indent);
        break;
      case 0x6D66726F:
        return ((MFRO*)this)->toPrettyString(indent);
        break;
      case 0x68646C72:
        return ((HDLR*)this)->toPrettyString(indent);
        break;
      case 0x766D6864:
        return ((VMHD*)this)->toPrettyString(indent);
        break;
      case 0x736D6864:
        return ((SMHD*)this)->toPrettyString(indent);
        break;
      case 0x686D6864:
        return ((HMHD*)this)->toPrettyString(indent);
        break;
      case 0x6E6D6864:
        return ((NMHD*)this)->toPrettyString(indent);
        break;
      case 0x6D656864:
        return ((MEHD*)this)->toPrettyString(indent);
        break;
      case 0x7374626C:
        return ((STBL*)this)->toPrettyString(indent);
        break;
      case 0x64726566:
        return ((DREF*)this)->toPrettyString(indent);
        break;
      case 0x75726C20:
        return ((URL*)this)->toPrettyString(indent);
        break;
      case 0x75726E20:
        return ((URN*)this)->toPrettyString(indent);
        break;
      case 0x6D766864:
        return ((MVHD*)this)->toPrettyString(indent);
        break;
      case 0x74667261:
        return ((TFRA*)this)->toPrettyString(indent);
        break;
      case 0x746B6864:
        return ((TKHD*)this)->toPrettyString(indent);
        break;
      case 0x6D646864:
        return ((MDHD*)this)->toPrettyString(indent);
        break;
      case 0x73747473:
        return ((STTS*)this)->toPrettyString(indent);
        break;
      case 0x63747473:
        return ((CTTS*)this)->toPrettyString(indent);
        break;
      case 0x73747363:
        return ((STSC*)this)->toPrettyString(indent);
        break;
      case 0x7374636F:
        return ((STCO*)this)->toPrettyString(indent);
        break;
      case 0x7374737A:
        return ((STSZ*)this)->toPrettyString(indent);
        break;
      case 0x73747364:
        return ((STSD*)this)->toPrettyString(indent);
        break;
      case 0x6D703461:
        return ((MP4A*)this)->toPrettyString(indent);
        break;
      case 0x61766331:
        return ((AVC1*)this)->toPrettyString(indent);
        break;
      case 0x75756964:
        return ((UUID*)this)->toPrettyString(indent);
        break;
      default:
        break;
    }
    std::string retval = std::string(indent, ' ') + "Unimplemented pretty-printing for box " + std::string(data + 4, 4) + "\n";
    /// \todo Implement hexdump for unimplemented boxes?
    //retval += 
    return retval;
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
  void Box::setInt24(uint32_t newData, size_t index){
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
  uint32_t Box::getInt24(size_t index){
    index += payloadOffset;
    if (index + 2 >= boxedSize()){
      if ( !reserve(index, 0, 3)){
        return 0;
      }
      setInt24(0, index - payloadOffset);
    }
    uint32_t result = data[index];
    result <<= 8;
    result += data[index + 1];
    result <<= 8;
    result += data[index + 2];
    return result;
  }

  /// Sets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt32(uint32_t newData, size_t index){
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
  uint32_t Box::getInt32(size_t index){
    index += payloadOffset;
    if (index + 3 >= boxedSize()){
      if ( !reserve(index, 0, 4)){
        return 0;
      }
      setInt32(0, index - payloadOffset);
    }
    uint32_t result;
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
  
  void fullBox::setVersion(char newVersion){
    setInt8(newVersion, 0);
  }

  char fullBox::getVersion(){
    return getInt8(0);
  }

  void fullBox::setFlags(uint32_t newFlags){
    setInt24(newFlags, 1);
  }

  uint32_t fullBox::getFlags(){
    return getInt24(1);
  }
  uint32_t containerBox::getContentCount(){
    int res = 0;
    int tempLoc = 0;
    while (tempLoc < boxedSize() - 8){
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  std::string fullBox::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent + 1, ' ') << "Version: " << (int)getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Flags: " << getFlags() << std::endl;
    return r.str();
  }

  void containerBox::setContent(Box & newContent, uint32_t no){
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

  Box & containerBox::getContent(uint32_t no){
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
  
  std::string containerBox::toPrettyContainerString(uint32_t indent, std::string boxName){
    std::stringstream r;
    r << std::string(indent, ' ') << boxName <<" (" << boxedSize() << ")" << std::endl;
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

  uint32_t ABST::getServerEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1;
    return getInt8(countLoc);
  }

  void ABST::setServerEntry(std::string & newEntry, uint32_t no){
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
  const char* ABST::getServerEntry(uint32_t no){
    if (no + 1 > getServerEntryCount()){
      return "";
    }
    int tempLoc = 29 + getStringLen(29) + 1 + 1; //position of first entry
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t ABST::getQualityEntryCount(){
    int countLoc = 29 + getStringLen(29) + 1 + 1;
    for (int i = 0; i < getServerEntryCount(); i++){
      countLoc += getStringLen(countLoc) + 1;
    }
    return getInt8(countLoc);
  }

  void ABST::setQualityEntry(std::string & newEntry, uint32_t no){
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

  const char* ABST::getQualityEntry(uint32_t no){
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
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  uint32_t ABST::getSegmentRunTableCount(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  void ABST::setSegmentRunTable(ASRT & newSegment, uint32_t no){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  ASRT & ABST::getSegmentRunTable(uint32_t no){
    static Box result;
    if (no > getSegmentRunTableCount()){
      static Box res;
      return (ASRT&)res;
    }
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  uint32_t ABST::getFragmentRunTableCount(){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  void ABST::setFragmentRunTable(AFRT & newFragment, uint32_t no){
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  AFRT & ABST::getFragmentRunTable(uint32_t no){
    static Box result;
    if (no >= getFragmentRunTableCount()){
      static Box res;
      return (AFRT&)res;
    }
    uint32_t tempLoc = 29 + getStringLen(29) + 1 + 1;
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

  const char* AFRT::getQualityEntry(uint32_t no){
    if (no + 1 > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 9; //position of first quality entry
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t AFRT::getFragmentRunCount(){
    int tempLoc = 9;
    for (int i = 0; i < getQualityEntryCount(); ++i){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void AFRT::setFragmentRun(afrt_runtable newRun, uint32_t no){
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

  afrt_runtable AFRT::getFragmentRun(uint32_t no){
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

  const char* ASRT::getQualityEntry(uint32_t no){
    if (no > getQualityEntryCount()){
      return "";
    }
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < no; i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getString(tempLoc);
  }

  uint32_t ASRT::getSegmentRunEntryCount(){
    int tempLoc = 5; //position of qualityentry count;
    for (int i = 0; i < getQualityEntryCount(); i++){
      tempLoc += getStringLen(tempLoc) + 1;
    }
    return getInt32(tempLoc);
  }

  void ASRT::setSegmentRun(uint32_t firstSegment, uint32_t fragmentsPerSegment, uint32_t no){
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

  asrt_runtable ASRT::getSegmentRun(uint32_t no){
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

  void MFHD::setSequenceNumber(uint32_t newSequenceNumber){
    setInt32(newSequenceNumber, 4);
  }

  uint32_t MFHD::getSequenceNumber(){
    return getInt32(4);
  }

  std::string MFHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mfhd] Movie Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "SequenceNumber " << getSequenceNumber() << std::endl;
    return r.str();
  }

  MOOF::MOOF(){
    memcpy(data + 4, "moof", 4);
  }

  std::string MOOF::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[moof] Movie Fragment Box"));
  }

  TRAF::TRAF(){
    memcpy(data + 4, "traf", 4);
  }

  uint32_t TRAF::getContentCount(){
    int res = 0;
    int tempLoc = 0;
    while (tempLoc < boxedSize() - 8){
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  void TRAF::setContent(Box & newContent, uint32_t no){
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

  Box & TRAF::getContent(uint32_t no){
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

  std::string TRAF::toPrettyString(uint32_t indent){
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

  void TRUN::setFlags(uint32_t newFlags){
    setInt24(newFlags, 1);
  }

  uint32_t TRUN::getFlags(){
    return getInt24(1);
  }

  void TRUN::setDataOffset(uint32_t newOffset){
    if (getFlags() & trundataOffset){
      setInt32(newOffset, 8);
    }
  }

  uint32_t TRUN::getDataOffset(){
    if (getFlags() & trundataOffset){
      return getInt32(8);
    }else{
      return 0;
    }
  }

  void TRUN::setFirstSampleFlags(uint32_t newSampleFlags){
    if ( !(getFlags() & trunfirstSampleFlags)){
      return;
    }
    if (getFlags() & trundataOffset){
      setInt32(newSampleFlags, 12);
    }else{
      setInt32(newSampleFlags, 8);
    }
  }

  uint32_t TRUN::getFirstSampleFlags(){
    if ( !(getFlags() & trunfirstSampleFlags)){
      return 0;
    }
    if (getFlags() & trundataOffset){
      return getInt32(12);
    }else{
      return getInt32(8);
    }
  }

  uint32_t TRUN::getSampleInformationCount(){
    return getInt32(4);
  }

  void TRUN::setSampleInformation(trunSampleInformation newSample, uint32_t no){
    uint32_t flags = getFlags();
    uint32_t sampInfoSize = 0;
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
    uint32_t offset = 8;
    if (flags & trundataOffset){
      offset += 4;
    }
    if (flags & trunfirstSampleFlags){
      offset += 4;
    }
    uint32_t innerOffset = 0;
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

  trunSampleInformation TRUN::getSampleInformation(uint32_t no){
    trunSampleInformation ret;
    ret.sampleDuration = 0;
    ret.sampleSize = 0;
    ret.sampleFlags = 0;
    ret.sampleOffset = 0;
    if (getSampleInformationCount() < no + 1){
      return ret;
    }
    uint32_t flags = getFlags();
    uint32_t sampInfoSize = 0;
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
    uint32_t offset = 8;
    if (flags & trundataOffset){
      offset += 4;
    }
    if (flags & trunfirstSampleFlags){
      offset += 4;
    }
    uint32_t innerOffset = 0;
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

  std::string TRUN::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[trun] Track Fragment Run (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    uint32_t flags = getFlags();
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
      r << std::string(indent + 2, ' ') << "[" << i << "] ";
      trunSampleInformation samp = getSampleInformation(i);
      if (flags & trunsampleDuration){
        r << "Duration=" << samp.sampleDuration << " ";
      }
      if (flags & trunsampleSize){
        r << "Size=" << samp.sampleSize << " ";
      }
      if (flags & trunsampleFlags){
        r << "Flags=" << prettySampleFlags(samp.sampleFlags) << " ";
      }
      if (flags & trunsampleOffsets){
        r << "Offset=" << samp.sampleOffset << " ";
      }
      r << std::endl;
    }

    return r.str();
  }

  std::string prettySampleFlags(uint32_t flag){
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

  void TFHD::setFlags(uint32_t newFlags){
    setInt24(newFlags, 1);
  }

  uint32_t TFHD::getFlags(){
    return getInt24(1);
  }

  void TFHD::setTrackID(uint32_t newID){
    setInt32(newID, 4);
  }

  uint32_t TFHD::getTrackID(){
    return getInt32(4);
  }

  void TFHD::setBaseDataOffset(uint64_t newOffset){
    if (getFlags() & tfhdBaseOffset){
      setInt64(newOffset, 8);
    }
  }

  uint64_t TFHD::getBaseDataOffset(){
    if (getFlags() & tfhdBaseOffset){
      return getInt64(8);
    }else{
      return 0;
    }
  }

  void TFHD::setSampleDescriptionIndex(uint32_t newIndex){
    if ( !(getFlags() & tfhdSampleDesc)){
      return;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    setInt32(newIndex, offset);
  }

  uint32_t TFHD::getSampleDescriptionIndex(){
    if ( !(getFlags() & tfhdSampleDesc)){
      return 0;
    }
    int offset = 8;
    if (getFlags() & tfhdBaseOffset){
      offset += 8;
    }
    return getInt32(offset);
  }

  void TFHD::setDefaultSampleDuration(uint32_t newDuration){
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

  uint32_t TFHD::getDefaultSampleDuration(){
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

  void TFHD::setDefaultSampleSize(uint32_t newSize){
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

  uint32_t TFHD::getDefaultSampleSize(){
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

  void TFHD::setDefaultSampleFlags(uint32_t newFlags){
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

  uint32_t TFHD::getDefaultSampleFlags(){
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

  std::string TFHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfhd] Track Fragment Header (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version " << (int)getInt8(0) << std::endl;

    uint32_t flags = getFlags();
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

  AVCC::AVCC(){
    memcpy(data + 4, "avcC", 4);
    setInt8(0xFF, 4); //reserved + 4-bytes NAL length
  }

  void AVCC::setVersion(uint32_t newVersion){
    setInt8(newVersion, 0);
  }

  uint32_t AVCC::getVersion(){
    return getInt8(0);
  }

  void AVCC::setProfile(uint32_t newProfile){
    setInt8(newProfile, 1);
  }

  uint32_t AVCC::getProfile(){
    return getInt8(1);
  }

  void AVCC::setCompatibleProfiles(uint32_t newCompatibleProfiles){
    setInt8(newCompatibleProfiles, 2);
  }

  uint32_t AVCC::getCompatibleProfiles(){
    return getInt8(2);
  }

  void AVCC::setLevel(uint32_t newLevel){
    setInt8(newLevel, 3);
  }

  uint32_t AVCC::getLevel(){
    return getInt8(3);
  }

  void AVCC::setSPSNumber(uint32_t newSPSNumber){
    setInt8(newSPSNumber, 5);
  }

  uint32_t AVCC::getSPSNumber(){
    return getInt8(5);
  }

  void AVCC::setSPS(std::string newSPS){
    setInt16(newSPS.size(), 6);
    for (int i = 0; i < newSPS.size(); i++){
      setInt8(newSPS[i], 8 + i);
    } //not null-terminated
  }

  uint32_t AVCC::getSPSLen(){
    return getInt16(6);
  }

  char* AVCC::getSPS(){
    return payload() + 8;
  }

  void AVCC::setPPSNumber(uint32_t newPPSNumber){
    int offset = 8 + getSPSLen();
    setInt8(newPPSNumber, offset);
  }

  uint32_t AVCC::getPPSNumber(){
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

  uint32_t AVCC::getPPSLen(){
    int offset = 8 + getSPSLen() + 1;
    return getInt16(offset);
  }

  char* AVCC::getPPS(){
    int offset = 8 + getSPSLen() + 3;
    return payload() + offset;
  }

  std::string AVCC::toPrettyString(uint32_t indent){
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

  void SDTP::setVersion(uint32_t newVersion){
    setInt8(newVersion, 0);
  }

  uint32_t SDTP::getVersion(){
    return getInt8(0);
  }

  void SDTP::setValue(uint32_t newValue, size_t index){
    setInt8(newValue, index);
  }

  uint32_t SDTP::getValue(size_t index){
    getInt8(index);
  }

  std::string SDTP::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[sdtp] Sample Dependancy Type (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Samples: " << (boxedSize() - 12) << std::endl;
    for (size_t i = 1; i <= boxedSize() - 12; ++i){
      uint32_t val = getValue(i+3);
      r << std::string(indent + 2, ' ') << "[" << i << "] = ";
      switch (val & 3){
        case 0:
          r << "               ";
          break;
        case 1:
          r << "Redundant,     ";
          break;
        case 2:
          r << "Not redundant, ";
          break;
        case 3:
          r << "Error,         ";
          break;
      }
      switch (val & 12){
        case 0:
          r << "                ";
          break;
        case 4:
          r << "Not disposable, ";
          break;
        case 8:
          r << "Disposable,     ";
          break;
        case 12:
          r << "Error,          ";
          break;
      }
      switch (val & 48){
        case 0:
          r << "            ";
          break;
        case 16:
          r << "IFrame,     ";
          break;
        case 32:
          r << "Not IFrame, ";
          break;
        case 48:
          r << "Error,      ";
          break;
      }
      r << "(" << val << ")" << std::endl;
    }
    return r.str();
  }
  
  FTYP::FTYP(){
    memcpy(data + 4, "ftyp", 4);
  }
  
  void FTYP::setMajorBrand(uint32_t newMajorBrand){
    setInt32(newMajorBrand, 0);
  }
  
  uint32_t FTYP::getMajorBrand(){
    return getInt32(0);
  }
  
  void FTYP::setMinorVersion(uint32_t newMinorVersion){
    setInt32(newMinorVersion, 4);
  }
  
  uint32_t FTYP::getMinorVersion(){
    return getInt32(4);
  }
  
  uint32_t FTYP::getCompatibleBrandsCount(){
    return (payloadSize() - 8) / 4;
  }
  
  void FTYP::setCompatibleBrands(uint32_t newCompatibleBrand, size_t index){
    setInt32(newCompatibleBrand, 8 + (index * 4));
  }
  
  uint32_t FTYP::getCompatibleBrands(size_t index){
    if (index >= getCompatibleBrandsCount()){
      return 0;
    }
    return getInt32(8 + (index * 4));
  }
  
  std::string FTYP::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[ftyp] File Type (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "MajorBrand: 0x" << std::hex << getMajorBrand() << std::dec << std::endl;
    r << std::string(indent + 1, ' ') << "MinorVersion: " << getMinorVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "CompatibleBrands (" << getCompatibleBrandsCount() << "):" << std::endl;
    for (int i = 0; i < getCompatibleBrandsCount(); i++){
      r << std::string(indent + 2, ' ') << "[" << i << "] CompatibleBrand: 0x" << std::hex << getCompatibleBrands(i) << std::dec << std::endl;
    }
    return r.str();
  }
  
  MOOV::MOOV(){
    memcpy(data + 4, "moov", 4);
  }
  
  std::string MOOV::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[moov] Movie Box"));
  }
  
  MVEX::MVEX(){
    memcpy(data + 4, "mvex", 4);
  }
  
  std::string MVEX::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[mvex] Movie Extends Header Box"));
  }
  
  TREX::TREX(){
    memcpy(data + 4, "trex", 4);
  }
  
  void TREX::setTrackID(uint32_t newTrackID){
    setInt32(newTrackID, 0);
  }
  
  uint32_t TREX::getTrackID(){
    return getInt32(0);
  }
  
  void TREX::setDefaultSampleDescriptionIndex(uint32_t newDefaultSampleDescriptionIndex){
    setInt32(newDefaultSampleDescriptionIndex,4);
  }
  
  uint32_t TREX::getDefaultSampleDescriptionIndex(){
    return getInt32(4);
  }
  
  void TREX::setDefaultSampleDuration(uint32_t newDefaultSampleDuration){
    setInt32(newDefaultSampleDuration,8);
  }
  
  uint32_t TREX::getDefaultSampleDuration(){
    getInt32(8);
  }
  
  void TREX::setDefaultSampleSize(uint32_t newDefaultSampleSize){
    setInt32(newDefaultSampleSize,12);
  }
  
  uint32_t TREX::getDefaultSampleSize(){
    getInt32(12);
  }
  
  void TREX::setDefaultSampleFlags(uint32_t newDefaultSampleFlags){
    setInt32(newDefaultSampleFlags,16);
  }
  
  uint32_t TREX::getDefaultSampleFlags(){
    getInt32(16);
  }
  
  std::string TREX::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[trex] Track Extends (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleDescriptionIndex : " << getDefaultSampleDescriptionIndex() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleDuration : " << getDefaultSampleDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleSize : " << getDefaultSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "DefaultSampleFlags : " << getDefaultSampleFlags() << std::endl;
    return r.str();
  }

  TRAK::TRAK(){
    memcpy(data + 4, "trak", 4);
  }
  
  std::string TRAK::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[trak] Track Structure"));
  }

  MDIA::MDIA(){
    memcpy(data + 4, "mdia", 4);
  }
  
  std::string MDIA::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[mdia] Track Media Structure"));
  }

  MINF::MINF(){
    memcpy(data + 4, "minf", 4);
  }
  
  std::string MINF::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[minf] Media Information"));
  }

  DINF::DINF(){
    memcpy(data + 4, "dinf", 4);
  }
  
  std::string DINF::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[dinf] Data Information"));
  }

  MFRA::MFRA(){
    memcpy(data + 4, "mfra", 4);
  }
  
  std::string MFRA::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[mfra] Movie Fragment Random Acces Box"));
  }
  
  MFRO::MFRO(){
    memcpy(data + 4, "mfro", 4);
  }

  void MFRO::setSize(uint32_t newSize){
    setInt32(newSize,0);
  }
  
  uint32_t MFRO::getSize(){
    getInt32(0);
  }
  
  std::string MFRO::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mfro] Movie Fragment Random Access Offset (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Size: " << getSize() << std::endl;
    return r.str();
  }
  
  HDLR::HDLR(){
    memcpy(data + 4, "hdlr", 4);
  }
  
  void HDLR::setSize(uint32_t newSize){
    setInt32(newSize,0);
  }
  
  uint32_t HDLR::getSize(){
    return getInt32(0);
  }
  
  void HDLR::setPreDefined(uint32_t newPreDefined){
    setInt32(newPreDefined,4);
  }
  
  uint32_t HDLR::getPreDefined(){
    return getInt32(4);
  }
  
  void HDLR::setHandlerType(uint32_t newHandlerType){
    setInt32(newHandlerType, 8);
  }
  
  uint32_t HDLR::getHandlerType(){
    return getInt32(8);
  }
  
  void HDLR::setName(std::string newName){
    setString(newName, 24);
  }
  
  std::string HDLR::getName(){
    return getString(24);
  }
  
  std::string HDLR::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[hdlr] Handler Reference (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "PreDefined: " << getPreDefined() << std::endl;
    r << std::string(indent + 1, ' ') << "HandlerType: " << getHandlerType() << std::endl;
    r << std::string(indent + 1, ' ') << "Name: " << getName() << std::endl;
    return r.str();
  }
  
  //Note: next 4 headers inherit from fullBox, start at byte 4.
  VMHD::VMHD(){
    memcpy(data + 4, "vmhd", 4);
  }
  
  void VMHD::setGraphicsMode(uint16_t newGraphicsMode){
    setInt16(newGraphicsMode,4);
  }
  
  uint16_t VMHD::getGraphicsMode(){
    return getInt16(4);
  }
  
  uint32_t VMHD::getOpColorCount(){
    return 3;
  }
  
  void VMHD::setOpColor(uint16_t newOpColor, size_t index){
    if (index <3){
      setInt16(newOpColor, 6 + (2 * index));
    }
  }
  
  uint16_t VMHD::getOpColor(size_t index){
    if (index < 3){
      return getInt16(6 + (index * 2));
    }else{
      return 0;
    }
  }
  
  std::string VMHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[vmhd] Video Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "GraphicsMode: " << getGraphicsMode() << std::endl;
    for (int i = 0; i < getOpColorCount(); i++){
      r << std::string(indent + 1, ' ') << "OpColor["<<i<<"]: " << getOpColor(i) << std::endl;
    }
    return r.str();
  }
    
  SMHD::SMHD(){
    memcpy(data + 4, "smhd", 4);
  }
  
  void SMHD::setBalance(int16_t newBalance){
    setInt16(newBalance,4);
  }
  
  int16_t SMHD::getBalance(){
    return getInt16(4);
  }
  
  std::string SMHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[smhd] Sound Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Balance: " << getBalance() << std::endl;
    return r.str();
  }

  HMHD::HMHD(){
    memcpy(data + 4, "hmhd", 4);
  }
  
  void HMHD::setMaxPDUSize(uint16_t newMaxPDUSize){
    setInt16(newMaxPDUSize,4);
  }
  
  uint16_t HMHD::getMaxPDUSize(){
    return getInt16(4);
  }
  
  void HMHD::setAvgPDUSize(uint16_t newAvgPDUSize){
    setInt16(newAvgPDUSize,6);
  }
  
  uint16_t HMHD::getAvgPDUSize(){
    return getInt16(6);
  }
  
  void HMHD::setMaxBitRate(uint32_t newMaxBitRate){
    setInt32(newMaxBitRate,8);
  }
  
  uint32_t HMHD::getMaxBitRate(){
    return getInt32(8);
  }
  
  void HMHD::setAvgBitRate(uint32_t newAvgBitRate){
    setInt32(newAvgBitRate,12);
  }
  
  uint32_t HMHD::getAvgBitRate(){
    return getInt32(12);
  }
  
  std::string HMHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[hmhd] Hint Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "maxPDUSize: " << getMaxPDUSize() << std::endl;
    r << std::string(indent + 1, ' ') << "avgPDUSize: " << getAvgPDUSize() << std::endl;
    r << std::string(indent + 1, ' ') << "maxBitRate: " << getMaxBitRate() << std::endl;
    r << std::string(indent + 1, ' ') << "avgBitRate: " << getAvgBitRate() << std::endl;
    return r.str();
  }
  
  NMHD::NMHD(){
    memcpy(data + 4, "nmhd", 4);
  }
  
  std::string NMHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[nmhd] Null Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    return r.str();
  }
  
  MEHD::MEHD(){
    memcpy(data + 4, "mehd", 4);
  }
  
  void MEHD::setFragmentDuration(uint64_t newFragmentDuration){
    if (getVersion() == 0){
      setInt32(newFragmentDuration,4);
    }else{
      setInt64(newFragmentDuration,4);
    }
  }
  
  uint64_t MEHD::getFragmentDuration(){
    if(getVersion() == 0){
      return getInt32(4);
    }else{
      return getInt64(4);
    }
  }
  
  std::string MEHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mehd] Movie Extends Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "FragmentDuration: " << getFragmentDuration() << std::endl;
    return r.str();
  }
  
  STBL::STBL(){
    memcpy(data + 4, "stbl", 4);
  }
  
  std::string STBL::toPrettyString(uint32_t indent){
    return toPrettyContainerString(indent, std::string("[stbl] Sample Table"));
  }
  
  URL::URL(){
    memcpy(data + 4, "url ", 4);
  }
  
  void URL::setLocation(std::string newLocation){
    setString(newLocation, 4);
  }
  
  std::string URL::getLocation(){
    return std::string(getString(4),getStringLen(4));
  }
  
  std::string URL::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[url ] URL Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Location: " << getLocation() << std::endl;
    return r.str();
  }
  

  URN::URN(){
    memcpy(data + 4, "urn ", 4);
  }
  
  void URN::setName(std::string newName){
    setString(newName, 4);
  }
  
  std::string URN::getName(){
    return std::string(getString(4),getStringLen(4));
  }
  
  void URN::setLocation(std::string newLocation){
    setString(newLocation, 4 + getStringLen(4) + 1);
  }
  
  std::string URN::getLocation(){
    int loc = 4 + getStringLen(4) + 1;
    return std::string(getString(loc),getStringLen(loc));
  }
  
  std::string URN::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[urn ] URN Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "Name: " << getName() << std::endl;
    r << std::string(indent + 1, ' ') << "Location: " << getLocation() << std::endl;
    return r.str();
  }
  

  DREF::DREF(){
    memcpy(data + 4, "dref", 4);
  }
  
  uint32_t DREF::getEntryCount(){
    return getInt32(4);
  }
  
  void DREF::setDataEntry(fullBox & newDataEntry, size_t index){
    int i;
    uint32_t offset = 8; //start of boxes
    for (i=0; i< getEntryCount() && i < index; i++){
      offset += getBoxLen(offset);
    }
    if (index+1 > getEntryCount()){
      int amount = index + 1 - getEntryCount();
      if ( !reserve(payloadOffset + offset, 0, amount * 8)){
        return;
      }
      for (int j = 0; j < amount; ++j){
        memcpy(data + payloadOffset + offset + j * 8, "\000\000\000\010erro", 8);
      }
      setInt32(index + 1, 4);
      offset += (index - i) * 8;
    }
    setBox(newDataEntry, offset);
  }
  
  Box & DREF::getDataEntry(size_t index){
    uint32_t offset = 8;
    if (index > getEntryCount()){
      static Box res;
      return (Box &)res;
    }
    
    for (int i=0; i < index; i++){
      offset += getBoxLen(offset);
    }
    return (Box &)getBox(offset);
  }
  
  std::string DREF::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[dref] Data Reference Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (int32_t i = 0; i< getEntryCount(); i++){
      r << getDataEntry(i).toPrettyString(indent+1);
    }
    return r.str();
  }
  
  MVHD::MVHD(){
    memcpy(data + 4, "mvhd", 4);
  }
  
  void MVHD::setCreationTime(uint64_t newCreationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newCreationTime, 4);
    }else{
      setInt64(newCreationTime, 4);
    }
  }
  
  uint64_t MVHD::getCreationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(4);
    }else{
      return getInt64(4);
    }
  }
  
  void MVHD::setModificationTime(uint64_t newModificationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newModificationTime, 8);
    }else{
      setInt64(newModificationTime, 12);
    }
  }
  
  uint64_t MVHD::getModificationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(8);
    }else{
      return getInt64(12);
    }
  }
  
  void MVHD::setTimeScale(uint32_t newTimeScale){
    if (getVersion() == 0){
      setInt32((uint32_t) newTimeScale, 12);
    }else{
      setInt32(newTimeScale, 20);
    }
  }
  
  uint32_t MVHD::getTimeScale(){
    if (getVersion() == 0){
      return getInt32(12);
    }else{
      return getInt32(20);
    }
  }
  
  void MVHD::setDuration(uint64_t newDuration){
    if (getVersion() == 0){
      setInt32((uint32_t) newDuration, 16);
    }else{
      setInt64(newDuration, 24);
    }
  }
  
  uint64_t MVHD::getDuration(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(16);
    }else{
      return getInt64(24);
    }
  }
  
  void MVHD::setRate(uint32_t newRate){
    if (getVersion() == 0){
      setInt32( newRate, 20);
    }else{
      setInt32(newRate, 32);
    }
  }
  
  uint32_t MVHD::getRate(){
    if (getVersion() == 0){
      return getInt32(20);
    }else{
      return getInt32(32);
    }
  }
  
  void MVHD::setVolume(uint16_t newVolume){
    if (getVersion() == 0){
      setInt16(newVolume, 24);
    }else{
      setInt16(newVolume, 36);
    }
  }
  
  uint16_t MVHD::getVolume(){
    if (getVersion() == 0){
      return getInt16(24);
    }else{
      return getInt16(36);
    }
  }
  //10 bytes reserverd in between
  uint32_t MVHD::getMatrixCount(){
    return 9;
  }
  
  void MVHD::setMatrix(int32_t newMatrix, size_t index){
    int offset = 0;
    if (getVersion() == 0){
      offset = 24 + 2 + 10;
    }else{
      offset = 36 + 2 + 10;
    }
    setInt32(newMatrix, offset + index * 4);
  }
  
  int32_t MVHD::getMatrix(size_t index){
    int offset = 0;
    if (getVersion() == 0){
      offset = 24 + 2 + 10;
    }else{
      offset = 36 + 2 + 10;
    }
    return getInt32(offset + index * 4);
  }
  
  //24 bytes of pre-defined in between
  void MVHD::setTrackID(uint32_t newTrackID){
    if (getVersion() == 0){
      setInt32(newTrackID, 86);
    }else{
      setInt32(newTrackID, 98);
    }
  }
  
  uint32_t MVHD::getTrackID(){
    if (getVersion() == 0){
      return getInt32(86);
    }else{
      return getInt32(98);
    }
  }
  
  std::string MVHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mvhd] Movie Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale: " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Rate: " << getRate() << std::endl;
    r << std::string(indent + 1, ' ') << "Volume: " << getVolume() << std::endl;
    r << std::string(indent + 1, ' ') << "Matrix: ";
    for (int32_t i = 0; i< getMatrixCount(); i++){
      r << getMatrix(i);
      if (i!=getMatrixCount()-1){
        r << ", ";
      }
    }
    r << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    return r.str();
  }
  
  TFRA::TFRA(){
    memcpy(data + 4, "dref", 4);
  }
  
  //note, fullbox starts at byte 4
  void TFRA::setTrackID(uint32_t newTrackID){
    setInt32(newTrackID, 4);
  }
  
  uint32_t TFRA::getTrackID(){
    getInt32(4);
  }
  
  void TFRA::setLengthSizeOfTrafNum(char newVal){
    char part = getInt8(11);
    setInt8(((newVal & 0x03)<<4) + (part & 0xCF),11);
  }
  
  char TFRA::getLengthSizeOfTrafNum(){
    return (getInt8(11)>>4) & 0x03;
  }
  
  void TFRA::setLengthSizeOfTrunNum(char newVal){
    char part = getInt8(11);
    setInt8(((newVal & 0x03)<<2) + (part & 0xF3),11);
  }
  
  char TFRA::getLengthSizeOfTrunNum(){
    return (getInt8(11)>>2) & 0x03;
  }
  
  void TFRA::setLengthSizeOfSampleNum(char newVal){
    char part = getInt8(11);
    setInt8(((newVal & 0x03)) + (part & 0xFC),11);
  }
  
  char TFRA::getLengthSizeOfSampleNum(){
    return (getInt8(11)) & 0x03;
  }
  
  void TFRA::setNumberOfEntry(uint32_t newNumberOfEntry){
    setInt32(newNumberOfEntry,12);
  }
  
  uint32_t TFRA::getNumberOfEntry(){
    return getInt32(12);
  }
  
  uint32_t TFRA::getTFRAEntrySize(){
    int EntrySize= (getVersion()==1 ? 16 : 8);
    EntrySize += getLengthSizeOfTrafNum()+1;
    EntrySize += getLengthSizeOfTrunNum()+1;
    EntrySize += getLengthSizeOfSampleNum()+1;
    return EntrySize;
  }
  
  void TFRA::setTFRAEntry(TFRAEntry newTFRAEntry, uint32_t no){
    if (no + 1 > getNumberOfEntry()){//if a new entry is issued
      uint32_t offset = 16 + getTFRAEntrySize() * getNumberOfEntry();//start of filler in bytes
      uint32_t fillsize = (no + 1 - getNumberOfEntry())*getTFRAEntrySize();//filler in bytes
      if ( !reserve(offset, 0, fillsize)){//filling space
        return;
      }
      setNumberOfEntry(no+1);
    }
    uint32_t loc = 16 + no * getTFRAEntrySize();
    if (getVersion() == 1){
      setInt64(newTFRAEntry.time, loc);
      setInt64(newTFRAEntry.moofOffset, loc+8);
      loc += 16;
    }else{
      setInt32(newTFRAEntry.time, loc);
      setInt32(newTFRAEntry.moofOffset, loc+4);
      loc += 8;
    }
    switch (getLengthSizeOfTrafNum()){
      case 0:
        setInt8(newTFRAEntry.trafNumber, loc);        
        break;
      case 1:
        setInt16(newTFRAEntry.trafNumber, loc);        
        break;
      case 2:
        setInt24(newTFRAEntry.trafNumber, loc);        
        break;
      case 3:
        setInt32(newTFRAEntry.trafNumber, loc);        
        break;
    }
    loc += getLengthSizeOfTrafNum() + 1;
    switch (getLengthSizeOfTrunNum()){
      case 0:
        setInt8(newTFRAEntry.trunNumber, loc);        
        break;
      case 1:
        setInt16(newTFRAEntry.trunNumber, loc);        
        break;
      case 2:
        setInt24(newTFRAEntry.trunNumber, loc);        
        break;
      case 3:
        setInt32(newTFRAEntry.trunNumber, loc);        
        break;
    }
    loc += getLengthSizeOfTrunNum() + 1;
    switch (getLengthSizeOfSampleNum()){
      case 0:
        setInt8(newTFRAEntry.sampleNumber, loc);        
        break;
      case 1:
        setInt16(newTFRAEntry.sampleNumber, loc);        
        break;
      case 2:
        setInt24(newTFRAEntry.sampleNumber, loc);        
        break;
      case 3:
        setInt32(newTFRAEntry.sampleNumber, loc);        
        break;
    }
  }
  
  TFRAEntry & TFRA::getTFRAEntry(uint32_t no){
    static TFRAEntry retval;
    if (no >= getNumberOfEntry()){
      static TFRAEntry inval;
      return inval;
    }
    uint32_t loc = 16 + no * getTFRAEntrySize();
    if (getVersion() == 1){
      retval.time = getInt64(loc);
      retval.moofOffset = getInt64(loc + 8);
      loc += 16;
    }else{
      retval.time = getInt32(loc);
      retval.moofOffset = getInt32(loc + 4);
      loc += 8;
    }
    switch (getLengthSizeOfTrafNum()){
      case 0:
        retval.trafNumber = getInt8(loc);        
        break;
      case 1:
        retval.trafNumber = getInt16(loc);        
        break;
      case 2:
        retval.trafNumber = getInt24(loc);        
        break;
      case 3:
        retval.trafNumber = getInt32(loc);        
        break;
    }
    loc += getLengthSizeOfTrafNum() + 1;
    switch (getLengthSizeOfTrunNum()){
      case 0:
        retval.trunNumber = getInt8(loc);        
        break;
      case 1:
        retval.trunNumber = getInt16(loc);        
        break;
      case 2:
        retval.trunNumber = getInt24(loc);        
        break;
      case 3:
        retval.trunNumber = getInt32(loc);        
        break;
    }
    loc += getLengthSizeOfTrunNum() + 1;
    switch (getLengthSizeOfSampleNum()){
      case 0:
        retval.sampleNumber = getInt8(loc);        
        break;
      case 1:
        retval.sampleNumber = getInt16(loc);        
        break;
      case 2:
        retval.sampleNumber = getInt24(loc);        
        break;
      case 3:
        retval.sampleNumber = getInt32(loc);        
        break;
    }
    return retval;
  }
       
  std::string TFRA::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfra] Track Fragment Random Access Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfTrafNum: " << (int)getLengthSizeOfTrafNum() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfTrunNum: " << (int)getLengthSizeOfTrunNum() << std::endl;
    r << std::string(indent + 1, ' ') << "lengthSizeOfSampleNum: " << (int)getLengthSizeOfSampleNum() << std::endl;
    r << std::string(indent + 1, ' ') << "NumberOfEntry: " << getNumberOfEntry() << std::endl;
    for (int i = 0; i < getNumberOfEntry(); i++){
      static TFRAEntry temp;
      temp = getTFRAEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i <<"]:"<< std::endl;
      r << std::string(indent + 2, ' ') << "Time: " << temp.time << std::endl;
      r << std::string(indent + 2, ' ') << "MoofOffset: " << temp.moofOffset << std::endl;
      r << std::string(indent + 2, ' ') << "TrafNumber: " << temp.trafNumber << std::endl;
      r << std::string(indent + 2, ' ') << "TrunNumber: " << temp.trunNumber << std::endl;
      r << std::string(indent + 2, ' ') << "SampleNumber: " << temp.sampleNumber << std::endl;
    }
    return r.str();
  }

  TKHD::TKHD(){
    memcpy(data + 4, "tkhd", 4);
  }
  
  void TKHD::setCreationTime(uint64_t newCreationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newCreationTime, 4);
    }else{
      setInt64(newCreationTime, 4);
    }
  }
  
  uint64_t TKHD::getCreationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(4);
    }else{
      return getInt64(4);
    }
  }
  
  void TKHD::setModificationTime(uint64_t newModificationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newModificationTime, 8);
    }else{
      setInt64(newModificationTime, 12);
    }
  }
  
  uint64_t TKHD::getModificationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(8);
    }else{
      return getInt64(12);
    }
  }
  
  void TKHD::setTrackID(uint32_t newTrackID){
    if (getVersion() == 0){
      setInt32((uint32_t) newTrackID, 12);
    }else{
      setInt32(newTrackID, 20);
    }
  }
  
  uint32_t TKHD::getTrackID(){
    if (getVersion() == 0){
      return getInt32(12);
    }else{
      return getInt32(20);
    }
  }
  //note 4 bytes reserved in between
  void TKHD::setDuration(uint64_t newDuration){
    if (getVersion() == 0){
      setInt32(newDuration, 20);
    }else{
      setInt64(newDuration, 28);
    }
  }
  
  uint64_t TKHD::getDuration(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(20);
    }else{
      return getInt64(28);
    }
  }
  //8 bytes reserved in between
  void TKHD::setLayer(uint16_t newLayer){
    if (getVersion() == 0){
      setInt16(newLayer, 32);
    }else{
      setInt16(newLayer, 44);
    }
  }
  
  uint16_t TKHD::getLayer(){
    if (getVersion() == 0){
      return getInt16(32);
    }else{
      return getInt16(44);
    }
  }
  
  void TKHD::setAlternateGroup(uint16_t newAlternateGroup){
    if (getVersion() == 0){
      setInt16(newAlternateGroup, 34);
    }else{
      setInt16(newAlternateGroup, 46);
    }
  }
  
  uint16_t TKHD::getAlternateGroup(){
    if (getVersion() == 0){
      return getInt16(34);
    }else{
      return getInt16(46);
    }
  }
  
  void TKHD::setVolume(uint16_t newVolume){
    if (getVersion() == 0){
      setInt16(newVolume, 36);
    }else{
      setInt16(newVolume, 48);
    }
  }
  
  uint16_t TKHD::getVolume(){
    if (getVersion() == 0){
      return getInt16(36);
    }else{
      return getInt16(48);
    }
  }
  //2 bytes reserved in between
  uint32_t TKHD::getMatrixCount(){
    return 9;
  }
  
  void TKHD::setMatrix(int32_t newMatrix, size_t index){
    int offset = 0;
    if (getVersion() == 0){
      offset = 36 + 2 + 2;
    }else{
      offset = 48 + 2 + 2;
    }
    setInt32(newMatrix, offset + index * 4);
  }
  
  int32_t TKHD::getMatrix(size_t index){
    int offset = 0;
    if (getVersion() == 0){
      offset = 36 + 2 + 2;
    }else{
      offset = 48 + 2 + 2;
    }
    return getInt32(offset + index * 4);
  }
  
  void TKHD::setWidth(uint32_t newWidth){
    if (getVersion() == 0){
      setInt32(newWidth, 76);
    }else{
      setInt32(newWidth, 88);
    }
  }
  
  uint32_t TKHD::getWidth(){
    if (getVersion() == 0){
      return getInt32(76);
    }else{
      return getInt32(88);
    }
  }
  
  void TKHD::setHeight(uint32_t newHeight){
    if (getVersion() == 0){
      setInt32(newHeight, 80);
    }else{
      setInt32(newHeight, 92);
    }
  }
  
  uint32_t TKHD::getHeight(){
    if (getVersion() == 0){
      return getInt32(80);
    }else{
      return getInt32(92);
    }
  }
        
  std::string TKHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[tkhd] Track Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TrackID: " << getTrackID() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Layer: " << getLayer() << std::endl;
    r << std::string(indent + 1, ' ') << "AlternateGroup: " << getAlternateGroup() << std::endl;
    r << std::string(indent + 1, ' ') << "Volume: " << getVolume() << std::endl;
    r << std::string(indent + 1, ' ') << "Matrix: ";
    for (int32_t i = 0; i< getMatrixCount(); i++){
      r << getMatrix(i);
      if (i!=getMatrixCount()-1){
        r << ", ";
      }
    }
    r << std::endl;
    r << std::string(indent + 1, ' ') << "Width: " << (getWidth() >> 16) << "." << (getWidth() & 0xFFFF) << std::endl;
    r << std::string(indent + 1, ' ') << "Height: " << (getHeight() >> 16) << "." << (getHeight() & 0xFFFF) << std::endl;
    return r.str();
  }

  MDHD::MDHD(){
    memcpy(data + 4, "mdhd", 4);
  }
  
  void MDHD::setCreationTime(uint64_t newCreationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newCreationTime, 4);
    }else{
      setInt64(newCreationTime, 4);
    }
  }
  
  uint64_t MDHD::getCreationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(4);
    }else{
      return getInt64(4);
    }
  }
  
  void MDHD::setModificationTime(uint64_t newModificationTime){
    if (getVersion() == 0){
      setInt32((uint32_t) newModificationTime, 8);
    }else{
      setInt64(newModificationTime, 12);
    }
  }
  
  uint64_t MDHD::getModificationTime(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(8);
    }else{
      return getInt64(12);
    }
  }
  
  void MDHD::setTimeScale(uint32_t newTimeScale){
    if (getVersion() == 0){
      setInt32((uint32_t) newTimeScale, 12);
    }else{
      setInt32(newTimeScale, 20);
    }
  }
  
  uint32_t MDHD::getTimeScale(){
    if (getVersion() == 0){
      return getInt32(12);
    }else{
      return getInt32(20);
    }
  }
  
  void MDHD::setDuration(uint64_t newDuration){
    if (getVersion() == 0){
      setInt32((uint32_t) newDuration, 16);
    }else{
      setInt64(newDuration, 24);
    }
  }
  
  uint64_t MDHD::getDuration(){
    if (getVersion() == 0){
      return (uint64_t)getInt32(16);
    }else{
      return getInt64(24);
    }
  }

  void MDHD::setLanguage (uint16_t newLanguage){
    if (getVersion() == 0){
      setInt16(newLanguage & 0x7F, 20);
    }else{
      setInt16(newLanguage & 0x7F, 20);
    }
  }
  
  uint16_t MDHD::getLanguage(){
    if (getVersion() == 0){
      return getInt16(20) & 0x7F;
    }else{
      return getInt16(20) & 0x7F;
    }
  }
  
  std::string MDHD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[mdhd] Media Header Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "CreationTime: " << getCreationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "ModificationTime: " << getModificationTime() << std::endl;
    r << std::string(indent + 1, ' ') << "TimeScale: " << getTimeScale() << std::endl;
    r << std::string(indent + 1, ' ') << "Duration: " << getDuration() << std::endl;
    r << std::string(indent + 1, ' ') << "Language: 0x" << std::hex << getLanguage() << std::dec<< std::endl;
    return r.str();
  }
  
  STTS::STTS(){
    memcpy(data + 4, "stts", 4);
  }
  
  void STTS::setEntryCount(uint32_t newEntryCount){
    setInt32(newEntryCount, 4);
  }
  
  uint32_t STTS::getEntryCount(){
    return getInt32(4);
  }
  
  void STTS::setSTTSEntry(STTSEntry newSTTSEntry, uint32_t no){
    if(no + 1 > getEntryCount()){
      for (int i = getEntryCount(); i < no; i++){
        setInt64(0, 8 + (i * 8));//filling up undefined entries of 64 bits
      }
    }
    setInt32(newSTTSEntry.sampleCount, 8 + no * 8);
    setInt32(newSTTSEntry.sampleDelta, 8 + (no * 8) + 4);
  }
  
  STTSEntry STTS::getSTTSEntry(uint32_t no){
    static STTSEntry retval;
    if (no >= getEntryCount()){
      static STTSEntry inval;
      return inval;
    }
    retval.sampleCount = getInt32(8 + (no * 8));
    retval.sampleDelta = getInt32(8 + (no * 8) + 4);
    return retval;
  }
  
  std::string STTS::toPrettyString(uint32_t indent){
      std::stringstream r;
    r << std::string(indent, ' ') << "[stts] Sample Table Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (int i = 0; i < getEntryCount(); i++){
      static STTSEntry temp;
      temp = getSTTSEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i <<"]:"<< std::endl;
      r << std::string(indent + 2, ' ') << "SampleCount: " << temp.sampleCount << std::endl;
      r << std::string(indent + 2, ' ') << "SampleDelta: " << temp.sampleDelta << std::endl;
    }
    return r.str();

  }
  
  CTTS::CTTS(){
    memcpy(data + 4, "ctts", 4);
  }
  
  void CTTS::setEntryCount(uint32_t newEntryCount){
    setInt32(newEntryCount, 4);
  }
  
  uint32_t CTTS::getEntryCount(){
    return getInt32(4);
  }
  
  void CTTS::setCTTSEntry(CTTSEntry newCTTSEntry, uint32_t no){
    if(no + 1 > getEntryCount()){
      for (int i = getEntryCount(); i < no; i++){
        setInt64(0, 8 + (i * 8));//filling up undefined entries of 64 bits
      }
    }
    setInt32(newCTTSEntry.sampleCount, 8 + no * 8);
    setInt32(newCTTSEntry.sampleOffset, 8 + (no * 8) + 4);
  }
  
  CTTSEntry CTTS::getCTTSEntry(uint32_t no){
    static CTTSEntry retval;
    if (no >= getEntryCount()){
      static CTTSEntry inval;
      return inval;
    }
    retval.sampleCount = getInt32(8 + (no * 8));
    retval.sampleOffset = getInt32(8 + (no * 8) + 4);
    return retval;
  }
  
  std::string CTTS::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[stts] Sample Table Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (int i = 0; i < getEntryCount(); i++){
      static CTTSEntry temp;
      temp = getCTTSEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i <<"]:"<< std::endl;
      r << std::string(indent + 2, ' ') << "SampleCount: " << temp.sampleCount << std::endl;
      r << std::string(indent + 2, ' ') << "SampleOffset: " << temp.sampleOffset << std::endl;
    }
    return r.str();

  }

  STSC::STSC(){
    memcpy(data + 4, "stsc", 4);
  }
  
  void STSC::setEntryCount(uint32_t newEntryCount){
    setInt32(newEntryCount, 4);
  }
  
  uint32_t STSC::getEntryCount(){
    return getInt32(4);
  }
  
  void STSC::setSTSCEntry(STSCEntry newSTSCEntry, uint32_t no){
    if(no + 1 > getEntryCount()){
      for (int i = getEntryCount(); i < no; i++){
        setInt64(0, 8 + (i * 12));//filling up undefined entries of 64 bits
        setInt32(0, 8 + (i * 12) + 8);
      }
    }
    setInt32(newSTSCEntry.firstChunk, 8 + no * 12);
    setInt32(newSTSCEntry.samplesPerChunk, 8 + (no * 12) + 4);
    setInt32(newSTSCEntry.sampleDescriptionIndex, 8 + (no * 12) + 8);
  }
  
  STSCEntry STSC::getSTSCEntry(uint32_t no){
    static STSCEntry retval;
    if (no >= getEntryCount()){
      static STSCEntry inval;
      return inval;
    }
    retval.firstChunk = getInt32(8 + (no * 12));
    retval.samplesPerChunk = getInt32(8 + (no * 12) + 4);
    retval.sampleDescriptionIndex = getInt32(8 + (no * 12) + 8);
    return retval;
  }
  
  std::string STSC::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsc] Sample To Chunk Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (int i = 0; i < getEntryCount(); i++){
      static STSCEntry temp;
      temp = getSTSCEntry(i);
      r << std::string(indent + 1, ' ') << "Entry[" << i <<"]:"<< std::endl;
      r << std::string(indent + 2, ' ') << "FirstChunk: " << temp.firstChunk << std::endl;
      r << std::string(indent + 2, ' ') << "SamplesPerChunk: " << temp.samplesPerChunk << std::endl;
      r << std::string(indent + 2, ' ') << "SampleDescriptionIndex: " << temp.sampleDescriptionIndex << std::endl;
    }
    return r.str();
  }
  
  STCO::STCO(){
    memcpy(data + 4, "stco", 4);
  }
  
  void STCO::setEntryCount(uint32_t newEntryCount){
    setInt32(newEntryCount, 4);
  }
  
  uint32_t STCO::getEntryCount(){
    return getInt32(4);
  }
  
  void STCO::setChunkOffset(uint32_t newChunkOffset, uint32_t no){
    if (no + 1 > getEntryCount()){
      for (int i = getEntryCount(); i < no; i++){
        setInt32(0, 8 + i * 4);//filling undefined entries
      }
    }
    setInt32(newChunkOffset, 8 + no * 4);  
  }
  
  uint32_t STCO::getChunkOffset(uint32_t no){
    if (no + 1 >= getEntryCount()){
      return 0;
    }
    return getInt32(8 + no * 4);
  }
  
  std::string STCO::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[stco] Chunk Offset Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntryCount: " << getEntryCount() << std::endl;
    for (int i = 0; i < getEntryCount(); i++){
      r << std::string(indent + 1, ' ') << "ChunkOffset[" << i <<"]: " << getChunkOffset(i) << std::endl;
    }
    return r.str();
  }

  STSZ::STSZ(){
    memcpy(data + 4, "stsz", 4);
  }

  void STSZ::setSampleSize(uint32_t newSampleSize){
    setInt32(newSampleSize, 4);
  }
  
  uint32_t STSZ::getSampleSize(){
    return getInt32(4);
  }

  
  void STSZ::setSampleCount(uint32_t newSampleCount){
    setInt32(newSampleCount, 8);
  }
  
  uint32_t STSZ::getSampleCount(){
    return getInt32(8);
  }
  
  void STSZ::setEntrySize(uint32_t newEntrySize, uint32_t no){
    if (no + 1 > getSampleCount()){
      for (int i = getSampleCount(); i < no; i++){
        setInt32(0, 12 + i * 4);//filling undefined entries
      }
    }
    setInt32(newEntrySize, 12 + no * 4);  
  }
  
  uint32_t STSZ::getEntrySize(uint32_t no){
    if (no + 1 >= getSampleCount()){
      return 0;
    }
    return getInt32(12 + no * 4);
  }
  
  std::string STSZ::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsz] Sample Size Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "SampleSize: " << getSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "SampleCount: " << getSampleCount() << std::endl;
    for (int i = 0; i < getSampleCount(); i++){
      r << std::string(indent + 1, ' ') << "EntrySize[" << i <<"]: " << getEntrySize(i) << std::endl;
    }
    return r.str();
  }

  SampleEntry::SampleEntry(){
    memcpy(data + 4, "erro", 4);
  }
  
  void SampleEntry::setDataReferenceIndex(uint16_t newDataReferenceIndex){
    setInt16(newDataReferenceIndex, 6);
  }
  
  uint16_t SampleEntry::getDataReferenceIndex(){
    return getInt16(6);
  }
  
  std::string SampleEntry::toPrettySampleString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent + 1, ' ') << "DataReferenceIndex: " << getDataReferenceIndex() << std::endl;
    return r.str();  
  }

  CLAP::CLAP(){
    memcpy(data + 4, "clap", 4);
  }
  
  void CLAP::setCleanApertureWidthN(uint32_t newVal){
    setInt32(newVal,0);  
  }
  
  uint32_t CLAP::getCleanApertureWidthN(){
    return getInt32(0);
  }
  
  void CLAP::setCleanApertureWidthD(uint32_t newVal){
    setInt32(newVal,4);
  }
  
  uint32_t CLAP::getCleanApertureWidthD(){
    return getInt32(4);
  }
  
  void CLAP::setCleanApertureHeightN(uint32_t newVal){
    setInt32(newVal,8);
  }
  
  uint32_t CLAP::getCleanApertureHeightN(){
    return getInt32(8);
  }
  
  void CLAP::setCleanApertureHeightD(uint32_t newVal){
    setInt32(newVal, 12);
  }
  
  uint32_t CLAP::getCleanApertureHeightD(){
    return getInt32(12);
  }
  
  void CLAP::setHorizOffN(uint32_t newVal){
    setInt32(newVal, 16);
  }
  
  uint32_t CLAP::getHorizOffN(){
    return getInt32(16);
  }
  
  void CLAP::setHorizOffD(uint32_t newVal){
    setInt32(newVal, 20);
  }
  
  uint32_t CLAP::getHorizOffD(){
    return getInt32(20);
  }
  
  void CLAP::setVertOffN(uint32_t newVal){
    setInt32(newVal, 24);
  }
  
  uint32_t CLAP::getVertOffN(){
    return getInt32(24);
  }
  
  void CLAP::setVertOffD(uint32_t newVal){
    setInt32(newVal, 28);
  }
  
  uint32_t CLAP::getVertOffD(){
    return getInt32(32);
  }
  
  std::string CLAP::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[clap] Clean Aperture Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureWidthN: " << getCleanApertureWidthN() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureWidthD: " << getCleanApertureWidthD() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureHeightN: " << getCleanApertureHeightN() << std::endl;
    r << std::string(indent + 1, ' ') << "CleanApertureHeightD: " << getCleanApertureHeightD() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizOffN: " << getHorizOffN() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizOffD: " << getHorizOffD() << std::endl;
    r << std::string(indent + 1, ' ') << "VertOffN: " << getVertOffN() << std::endl;
    r << std::string(indent + 1, ' ') << "VertOffD: " << getVertOffD() << std::endl;
    return r.str();
  }
  
  PASP::PASP(){
    memcpy(data + 4, "pasp", 4);
  }
  
  void PASP::setHSpacing(uint32_t newVal){
    setInt32(newVal, 0);
  }
  
  uint32_t PASP::getHSpacing(){
    return getInt32(0);
  }
  
  void PASP::setVSpacing(uint32_t newVal){
    setInt32(newVal, 4);
  }
  
  uint32_t PASP::getVSpacing(){
    return getInt32(4);
  }
  
  std::string PASP::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[pasp] Pixel Aspect Ratio Box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "HSpacing: " << getHSpacing() << std::endl;
    r << std::string(indent + 1, ' ') << "VSpacing: " << getVSpacing() << std::endl;
    return r.str();
  }

  VisualSampleEntry::VisualSampleEntry(){
    memcpy(data + 4, "erro", 4);
  }
  
  void VisualSampleEntry::setWidth(uint16_t newWidth){
    setInt16(newWidth,24);
  }
  
  uint16_t VisualSampleEntry::getWidth(){
    return getInt16(24);
  }
  
  void VisualSampleEntry::setHeight(uint16_t newHeight){
    setInt16(newHeight, 26);
  }
  
  uint16_t VisualSampleEntry::getHeight(){
    return getInt16(26);
  }
  
  void VisualSampleEntry::setHorizResolution (uint32_t newHorizResolution){
    setInt32(newHorizResolution, 28);
  }
  
  uint32_t VisualSampleEntry::getHorizResolution(){
    return getInt32(28);
  }
  
  void VisualSampleEntry::setVertResolution (uint32_t newVertResolution){
    setInt32(newVertResolution,32);
  }
  
  uint32_t VisualSampleEntry::getVertResolution(){
    return getInt32(32);
  }
  
  void VisualSampleEntry::setFrameCount(uint16_t newFrameCount){
    setInt16(newFrameCount, 40);
  }
  
  uint16_t VisualSampleEntry::getFrameCount(){
    return getInt16(40);
  }

  void VisualSampleEntry::setCompressorName(std::string newCompressorName){
    setString(newCompressorName,42);
  }
  
  std::string VisualSampleEntry::getCompressorName(){
    return getString(42);
  }

  void VisualSampleEntry::setDepth(uint16_t newDepth){
    setInt16(newDepth, 74);
  }
  
  uint16_t VisualSampleEntry::getDepth(){
    getInt16(74);
  }

  Box & VisualSampleEntry::getCLAP(){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if(payloadSize() <84){//if the EntryBox is not big enough to hold a CLAP/PASP
      return ret;
    }
    if (getBox(76).isType("clap")){
      return getBox(76);
    }else{
      return ret;
    }
  }
  
  Box & VisualSampleEntry::getPASP(){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if(payloadSize() <84){//if the EntryBox is not big enough to hold a CLAP/PASP
      return ret;
    }
    if (getBox(76).isType("pasp")){
      return getBox(76);
    }else{
      if (payloadSize() < 76 + getBoxLen(76) + 8){
        return ret;
      }else{
        if (getBox(76+getBoxLen(76)).isType("pasp")){
          return getBox(76+getBoxLen(76));
        }else{
          return ret;
        }
      }
    }
  }
  
  std::string VisualSampleEntry::toPrettyVisualString(uint32_t indent, std::string name){
    std::stringstream r;
    r << std::string(indent, ' ') << name << " (" << boxedSize() << ")" << std::endl;
    r << toPrettySampleString(indent);
    r << std::string(indent + 1, ' ') << "Width: " << getWidth() << std::endl;
    r << std::string(indent + 1, ' ') << "Height: " << getHeight() << std::endl;
    r << std::string(indent + 1, ' ') << "HorizResolution: " << getHorizResolution() << std::endl;
    r << std::string(indent + 1, ' ') << "VertResolution: " << getVertResolution() << std::endl;
    r << std::string(indent + 1, ' ') << "FrameCount: " << getFrameCount() << std::endl;
    r << std::string(indent + 1, ' ') << "CompressorName: " << getCompressorName() << std::endl;
    r << std::string(indent + 1, ' ') << "Depth: " << getDepth() << std::endl;
    if (getCLAP().isType("clap")){
      r << getCLAP().toPrettyString(indent+1);
    }
    if (getPASP().isType("pasp")){
      r << getPASP().toPrettyString(indent+1);
    }
    return r.str();
  }

  AudioSampleEntry::AudioSampleEntry(){
    memcpy(data + 4, "erro", 4);
  }
  
  void AudioSampleEntry::setChannelCount(uint16_t newChannelCount){
    setInt16(newChannelCount,16);
  }
  
  uint16_t AudioSampleEntry::getChannelCount(){
    return getInt16(16);
  }
  
  void AudioSampleEntry::setSampleSize(uint16_t newSampleSize){
    setInt16(newSampleSize,18);
  }
  
  uint16_t AudioSampleEntry::getSampleSize(){
    return getInt16(18);
  }
  
  void AudioSampleEntry::setPreDefined(uint16_t newPreDefined){
    setInt16(newPreDefined,20);
  }
  
  uint16_t AudioSampleEntry::getPreDefined(){
    return getInt16(20);
  }
  
  void AudioSampleEntry::setSampleRate(uint32_t newSampleRate){
    setInt32(newSampleRate << 16, 24);
  }
  
  uint32_t AudioSampleEntry::getSampleRate(){
    return getInt32(24) >> 16;
  }
  
  std::string AudioSampleEntry::toPrettyAudioString(uint32_t indent, std::string name){
    std::stringstream r;
    r << std::string(indent, ' ') << name << " (" << boxedSize() << ")" << std::endl;
    r << toPrettySampleString(indent);
    r << std::string(indent + 1, ' ') << "ChannelCount: " << getChannelCount() << std::endl;
    r << std::string(indent + 1, ' ') << "SampleSize: " << getSampleSize() << std::endl;
    r << std::string(indent + 1, ' ') << "PreDefined: " << getPreDefined() << std::endl;
    r << std::string(indent + 1, ' ') << "SampleRate: " << getSampleRate() << std::endl;
    return r.str();
  }

  MP4A::MP4A(){
    memcpy(data + 4, "mp4a", 4);
  }
  
  std::string MP4A::toPrettyString(uint32_t indent){
    return toPrettyAudioString(indent, "[mp4a] MPEG 4 Audio");
  }

  AVC1::AVC1(){
    memcpy(data + 4, "avc1", 4);
  }
  
  std::string AVC1::toPrettyString(uint32_t indent){
    return toPrettyVisualString(indent, "[avc1] Advanced Video Codec 1");
  }

  STSD::STSD(){
    memcpy(data + 4, "stsd", 4);
  }

  void STSD::setEntryCount (uint32_t newEntryCount){
    setInt32(newEntryCount, 4);
  }
  
  uint32_t STSD::getEntryCount(){
    return getInt32(4);
  }
  
  void STSD::setEntry(Box & newContent, uint32_t no){
  int tempLoc = 8;
    int entryCount = getEntryCount();
    for (int i = 0; i < no; i++){
      if (i < entryCount){
        tempLoc += getBoxLen(tempLoc);
      }else{
        if ( !reserve(tempLoc, 0, (no - entryCount) * 8)){
          return;
        }
        memset(data + tempLoc, 0, (no - entryCount) * 8);
        tempLoc += (no - entryCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
    if (getEntryCount() < no){
      setEntryCount(no);
    }
  }
  
  Box & STSD::getEntry(uint32_t no){
    static Box ret = Box((char*)"\000\000\000\010erro", false);
    if (no > getEntryCount()){
      return ret;
    }
    int i = 0;
    int tempLoc = 8;
    while (i < no){
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string STSD::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[stsd] Sample Description Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 1, ' ') << "EntrySize: " << getEntryCount() << std::endl;
    Box curBox;
    int tempLoc = 0;
    int contentCount = getEntryCount();
    for (int i = 0; i < contentCount; i++){
      curBox = getEntry(i);
      r << curBox.toPrettyString(indent + 1);
      tempLoc += getBoxLen(tempLoc);
    }
    return r.str();
  }

  static char c2hex(int c){
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  }
  
  UUID::UUID(){
    memcpy(data + 4, "uuid", 4);
    setInt64(0, 0);
    setInt64(0, 8);
  }

  std::string UUID::getUUID(){
    std::stringstream r;
    r << std::hex;
    for (int i = 0; i < 16; ++i){
      if (i == 4 || i == 6 || i == 8 || i == 10){
        r << "-";
      }
      r << std::setfill('0') << std::setw(2) << std::right << (int)(data[8+i]);
    }
    return r.str();
  }

  void UUID::setUUID(const std::string & uuid_string){
    //reset UUID to zero
    for (int i = 0; i < 4; ++i){
      ((uint32_t*)(data+8))[i] = 0;
    }
    //set the UUID from the string, char by char
    int i = 0;
    for (size_t j = 0; j < uuid_string.size(); ++j){
      if (uuid_string[j] == '-'){
        continue;
      }
      data[8+i/2] |= (c2hex(uuid_string[j]) << ((~i & 1) << 2));
      ++i;
    }
  }

  void UUID::setUUID(const char * raw_uuid){
    memcpy(data+8, raw_uuid, 16);
  }

  std::string UUID::toPrettyString(uint32_t indent){
    std::string UUID = getUUID();
    if (UUID == "d4807ef2-ca39-4695-8e54-26cb9e46a79f"){
      return ((UUID_TrackFragmentReference*)this)->toPrettyString(indent);
    }
    std::stringstream r;
    r << std::string(indent, ' ') << "[uuid] Extension box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "UUID: " << UUID << std::endl;
    r << std::string(indent + 1, ' ') << "Unknown UUID - ignoring contents." << std::endl;
    return r.str();
  }

  UUID_TrackFragmentReference::UUID_TrackFragmentReference(){
    setUUID((std::string)"d4807ef2-ca39-4695-8e54-26cb9e46a79f");
  }

  void UUID_TrackFragmentReference::setVersion(uint32_t newVersion){
    setInt8(newVersion, 16);
  }
  
  uint32_t UUID_TrackFragmentReference::getVersion(){
    return getInt8(16);
  }
  
  void UUID_TrackFragmentReference::setFlags(uint32_t newFlags){
    setInt24(newFlags, 17);
  }
  
  uint32_t UUID_TrackFragmentReference::getFlags(){
    return getInt24(17);
  }
  
  void UUID_TrackFragmentReference::setFragmentCount(uint32_t newCount){
    setInt8(newCount, 20);
  }
  
  uint32_t UUID_TrackFragmentReference::getFragmentCount(){
    return getInt8(20);
  }
  
  void UUID_TrackFragmentReference::setTime(size_t num, uint64_t newTime){
    if (getVersion() == 0){
      setInt32(newTime, 21+(num*8));
    }else{
      setInt64(newTime, 21+(num*16));
    }
  }
  
  uint64_t UUID_TrackFragmentReference::getTime(size_t num){
    if (getVersion() == 0){
      return getInt32(21+(num*8));
    }else{
      return getInt64(21+(num*16));
    }
  }
  
  void UUID_TrackFragmentReference::setDuration(size_t num, uint64_t newDuration){
    if (getVersion() == 0){
      setInt32(newDuration, 21+(num*8)+4);
    }else{
      setInt64(newDuration, 21+(num*16)+8);
    }
  }
  
  uint64_t UUID_TrackFragmentReference::getDuration(size_t num){
    if (getVersion() == 0){
      return getInt32(21+(num*8)+4);
    }else{
      return getInt64(21+(num*16)+8);
    }
  }

  std::string UUID_TrackFragmentReference::toPrettyString(uint32_t indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[d4807ef2-ca39-4695-8e54-26cb9e46a79f] Track Fragment Reference (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Fragments: " << getFragmentCount() << std::endl;
    int j = getFragmentCount();
    for (int i = 0; i < j; ++i){
      r << std::string(indent + 2, ' ') << "[" << i << "] Time = " << getTime(i) << ", Duration = " << getDuration(i) << std::endl;
    }
    return r.str();
  }


}
