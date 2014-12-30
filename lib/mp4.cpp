#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "mp4_adobe.h"
#include "mp4_ms.h"
#include "mp4_generic.h"
#include "json.h"

#include "defines.h"

/// Contains all MP4 format related code.
namespace MP4 {

  /// Creates a new box, optionally using the indicated pointer for storage.
  /// If manage is set to true, the pointer will be realloc'ed when the box needs to be resized.
  /// If the datapointer is NULL, manage is assumed to be true even if explicitly given as false.
  /// If managed, the pointer will be free'd upon destruction.
  Box::Box(char * datapointer, bool manage) {

    data = datapointer;
    managed = manage;
    payloadOffset = 8;
    if (data == 0) {
      clear();
    } else {
      data_size = ntohl(((int *)data)[0]);
    }
  }

  Box::Box(const Box & rs) {
    data = rs.data;
    managed = false;
    payloadOffset = rs.payloadOffset;
    if (data == 0) {
      clear();
    } else {
      data_size = ntohl(((int *)data)[0]);
    }
  }

  Box & Box::operator = (const Box & rs) {
    clear();
    if (data) {
      free(data);
      data = 0;
    }
    data = rs.data;
    managed = false;
    payloadOffset = rs.payloadOffset;
    if (data == 0) {
      clear();
    } else {
      data_size = ntohl(((int *)data)[0]);
    }
    return *this;
  }


  /// If managed, this will free the data pointer.
  Box::~Box() {
    if (managed && data) {
      free(data);
      data = 0;
    }
  }

  /// Returns the values at byte positions 4 through 7.
  std::string Box::getType() {
    return std::string(data + 4, 4);
  }

  /// Returns true if the given 4-byte boxtype is equal to the values at byte positions 4 through 7.
  bool Box::isType(const char * boxType) {
    return !memcmp(boxType, data + 4, 4);
  }

  /// Reads the first 8 bytes and returns
  std::string readBoxType(FILE * newData) {
    char retVal[8] = {0, 0, 0, 0, 'e', 'r', 'r', 'o'};
    long long unsigned int pos = ftell(newData);
    fread(retVal, 8, 1, newData);
    fseek(newData, pos, SEEK_SET);
    return std::string(retVal + 4, 4);
  }

  ///\todo make good working calcBoxSize with size and payloadoffset calculation
  unsigned long int calcBoxSize(char readVal[16]) {
    return (readVal[0] << 24) | (readVal[1] << 16) | (readVal[2] << 8) | (readVal[3]);
  }

  bool skipBox(FILE * newData) {
    char readVal[16];
    long long unsigned int pos = ftell(newData);
    if (fread(readVal, 4, 1, newData)) {
      uint64_t size = calcBoxSize(readVal);
      if (size == 1) {
        if (fread(readVal + 4, 12, 1, newData)) {
          size = 0 + ntohl(((int *)readVal)[2]);
          size <<= 32;
          size += ntohl(((int *)readVal)[3]);
        } else {
          return false;
        }
      } else if (size == 0) {
        fseek(newData, 0, SEEK_END);
      }
      DEBUG_MSG(DLVL_DEVEL, "skipping size 0x%.8lX", size);
      if (fseek(newData, pos + size, SEEK_SET) == 0) {
        return true;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }

  bool Box::read(FILE * newData) {
    char readVal[16];
    long long unsigned int pos = ftell(newData);
    if (fread(readVal, 4, 1, newData)) {
      payloadOffset = 8;
      uint64_t size = calcBoxSize(readVal);
      if (size == 1) {
        if (fread(readVal + 4, 12, 1, newData)) {
          size = 0 + ntohl(((int *)readVal)[2]);
          size <<= 32;
          size += ntohl(((int *)readVal)[3]);
          payloadOffset = 16;
        } else {
          return false;
        }
      }
      fseek(newData, pos, SEEK_SET);
      data = (char *)realloc(data, size);
      data_size = size;
      return (fread(data, size, 1, newData) == 1);
    } else {
      return false;
    }
  }

  /// Reads out a whole box (if possible) from newData, copying to the internal data storage and removing from the input string.
  /// \returns True on success, false otherwise.
  bool Box::read(std::string & newData) {
    if (!managed) {
      return false;
    }
    if (newData.size() > 4) {
      payloadOffset = 8;
      uint64_t size = ntohl(((int *)newData.c_str())[0]);
      if (size == 1) {
        if (newData.size() > 16) {
          size = 0 + ntohl(((int *)newData.c_str())[2]);
          size <<= 32;
          size += ntohl(((int *)newData.c_str())[3]);
          payloadOffset = 16;
        } else {
          return false;
        }
      }
      if (newData.size() >= size) {
        data = (char *)realloc(data, size);
        data_size = size;
        memcpy(data, newData.data(), size);
        newData.erase(0, size);
        return true;
      }
    }
    return false;
  }

  /// Returns the total boxed size of this box, including the header.
  uint64_t Box::boxedSize() {
    if (payloadOffset == 16) {
      return ((uint64_t)ntohl(((int *)data)[2]) << 32) + ntohl(((int *)data)[3]);
    }
    return ntohl(((int *)data)[0]);
  }

  /// Retruns the size of the payload of thix box, excluding the header.
  /// This value is defined as boxedSize() - 8.
  uint64_t Box::payloadSize() {
    return boxedSize() - payloadOffset;
  }

  /// Returns a copy of the data pointer.
  char * Box::asBox() {
    return data;
  }

  char * Box::payload() {
    return data + payloadOffset;
  }

  /// Makes this box managed if it wasn't already, resetting the internal storage to 8 bytes (the minimum).
  /// If this box wasn't managed, the original data is left intact - otherwise it is free'd.
  /// If it was somehow impossible to allocate 8 bytes (should never happen), this will cause segfaults later.
  void Box::clear() {
    if (data && managed) {
      free(data);
    }
    managed = true;
    payloadOffset = 8;
    data = (char *)malloc(8);
    if (data) {
      data_size = 8;
      ((int *)data)[0] = htonl(data_size);
    } else {
      data_size = 0;
    }
  }

  /// Attempts to typecast this Box to a more specific type and call the toPrettyString() function of that type.
  /// If this failed, it will print out a message saying pretty-printing is not implemented for boxtype.
  std::string Box::toPrettyString(uint32_t indent) {
    switch (ntohl(*((int *)(data + 4)))) { //type is at this address
      case 0x6D666864:
        return ((MFHD *)this)->toPrettyString(indent);
        break;
      case 0x6D6F6F66:
        return ((MOOF *)this)->toPrettyString(indent);
        break;
      case 0x61627374:
        return ((ABST *)this)->toPrettyString(indent);
        break;
      case 0x61667274:
        return ((AFRT *)this)->toPrettyString(indent);
        break;
      case 0x61667261:
        return ((AFRA *)this)->toPrettyString(indent);
        break;
      case 0x61737274:
        return ((ASRT *)this)->toPrettyString(indent);
        break;
      case 0x7472756E:
        return ((TRUN *)this)->toPrettyString(indent);
        break;
      case 0x74726166:
        return ((TRAF *)this)->toPrettyString(indent);
        break;
      case 0x74666864:
        return ((TFHD *)this)->toPrettyString(indent);
        break;
      case 0x61766343:
        return ((AVCC *)this)->toPrettyString(indent);
        break;
      case 0x73647470:
        return ((SDTP *)this)->toPrettyString(indent);
        break;
      case 0x66747970:
        return ((FTYP *)this)->toPrettyString(indent);
        break;
      case 0x6D6F6F76:
        return ((MOOV *)this)->toPrettyString(indent);
        break;
      case 0x6D766578:
        return ((MVEX *)this)->toPrettyString(indent);
        break;
      case 0x74726578:
        return ((TREX *)this)->toPrettyString(indent);
        break;
      case 0x6D667261:
        return ((MFRA *)this)->toPrettyString(indent);
        break;
      case 0x7472616B:
        return ((TRAK *)this)->toPrettyString(indent);
        break;
      case 0x6D646961:
        return ((MDIA *)this)->toPrettyString(indent);
        break;
      case 0x6D696E66:
        return ((MINF *)this)->toPrettyString(indent);
        break;
      case 0x64696E66:
        return ((DINF *)this)->toPrettyString(indent);
        break;
      case 0x6D66726F:
        return ((MFRO *)this)->toPrettyString(indent);
        break;
      case 0x68646C72:
        return ((HDLR *)this)->toPrettyString(indent);
        break;
      case 0x766D6864:
        return ((VMHD *)this)->toPrettyString(indent);
        break;
      case 0x736D6864:
        return ((SMHD *)this)->toPrettyString(indent);
        break;
      case 0x686D6864:
        return ((HMHD *)this)->toPrettyString(indent);
        break;
      case 0x6E6D6864:
        return ((NMHD *)this)->toPrettyString(indent);
        break;
      case 0x6D656864:
        return ((MEHD *)this)->toPrettyString(indent);
        break;
      case 0x7374626C:
        return ((STBL *)this)->toPrettyString(indent);
        break;
      case 0x64726566:
        return ((DREF *)this)->toPrettyString(indent);
        break;
      case 0x75726C20:
        return ((URL *)this)->toPrettyString(indent);
        break;
      case 0x75726E20:
        return ((URN *)this)->toPrettyString(indent);
        break;
      case 0x6D766864:
        return ((MVHD *)this)->toPrettyString(indent);
        break;
      case 0x74667261:
        return ((TFRA *)this)->toPrettyString(indent);
        break;
      case 0x746B6864:
        return ((TKHD *)this)->toPrettyString(indent);
        break;
      case 0x6D646864:
        return ((MDHD *)this)->toPrettyString(indent);
        break;
      case 0x73747473:
        return ((STTS *)this)->toPrettyString(indent);
        break;
      case 0x63747473:
        return ((CTTS *)this)->toPrettyString(indent);
        break;
      case 0x73747363:
        return ((STSC *)this)->toPrettyString(indent);
        break;
      case 0x7374636F:
        return ((STCO *)this)->toPrettyString(indent);
        break;
      case 0x7374737A:
        return ((STSZ *)this)->toPrettyString(indent);
        break;
      case 0x73747364:
        return ((STSD *)this)->toPrettyString(indent);
        break;
      case 0x6D703461://mp4a
      case 0x656E6361://enca
        return ((MP4A *)this)->toPrettyString(indent);
        break;
      case 0x61616320:
        return ((AAC *)this)->toPrettyString(indent);
        break;
      case 0x61766331:
        return ((AVC1 *)this)->toPrettyString(indent);
        break;
      case 0x68323634://h264
      case 0x656E6376://encv
        return ((H264 *)this)->toPrettyString(indent);
        break;
      case 0x65647473:
        return ((EDTS *)this)->toPrettyString(indent);
        break;
      case 0x73747373:
        return ((STSS *)this)->toPrettyString(indent);
        break;
      case 0x6D657461:
        return ((META *)this)->toPrettyString(indent);
        break;
      case 0x656C7374:
        return ((ELST *)this)->toPrettyString(indent);
        break;
      case 0x65736473:
        return ((ESDS *)this)->toPrettyString(indent);
        break;
      case 0x75647461:
        return ((UDTA *)this)->toPrettyString(indent);
        break;
      case 0x75756964:
        return ((UUID *)this)->toPrettyString(indent);
        break;
      default:
        break;
    }
    std::string retval = std::string(indent, ' ') + "Unimplemented pretty-printing for box " + std::string(data + 4, 4) + "\n";
    /// \todo Implement hexdump for unimplemented boxes?
    return retval;
  }

  /// Sets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt8(char newData, size_t index) {
    index += payloadOffset;
    if (index >= boxedSize()) {
      if (!reserve(index, 0, 1)) {
        return;
      }
    }
    data[index] = newData;
  }

  /// Gets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  char Box::getInt8(size_t index) {
    index += payloadOffset;
    if (index >= boxedSize()) {
      if (!reserve(index, 0, 1)) {
        return 0;
      }
      setInt8(0, index - payloadOffset);
    }
    return data[index];
  }

  /// Sets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt16(short newData, size_t index) {
    index += payloadOffset;
    if (index + 1 >= boxedSize()) {
      if (!reserve(index, 0, 2)) {
        return;
      }
    }
    newData = htons(newData);
    memcpy(data + index, (char *) &newData, 2);
  }

  /// Gets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  short Box::getInt16(size_t index) {
    index += payloadOffset;
    if (index + 1 >= boxedSize()) {
      if (!reserve(index, 0, 2)) {
        return 0;
      }
      setInt16(0, index - payloadOffset);
    }
    short result;
    memcpy((char *) &result, data + index, 2);
    return ntohs(result);
  }

  /// Sets the 24 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt24(uint32_t newData, size_t index) {
    index += payloadOffset;
    if (index + 2 >= boxedSize()) {
      if (!reserve(index, 0, 3)) {
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
  uint32_t Box::getInt24(size_t index) {
    index += payloadOffset;
    if (index + 2 >= boxedSize()) {
      if (!reserve(index, 0, 3)) {
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
  void Box::setInt32(uint32_t newData, size_t index) {
    index += payloadOffset;
    if (index + 3 >= boxedSize()) {
      if (!reserve(index, 0, 4)) {
        return;
      }
    }
    ((int *)(data + index))[0] = htonl(newData);
  }

  /// Gets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  uint32_t Box::getInt32(size_t index) {
    index += payloadOffset;
    if (index + 3 >= boxedSize()) {
      if (!reserve(index, 0, 4)) {
        return 0;
      }
      setInt32(0, index - payloadOffset);
    }
    return ntohl(((int *)(data + index))[0]);
  }

  /// Sets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt64(uint64_t newData, size_t index) {
    index += payloadOffset;
    if (index + 7 >= boxedSize()) {
      if (!reserve(index, 0, 8)) {
        return;
      }
    }
    ((int *)(data + index))[0] = htonl((int)(newData >> 32));
    ((int *)(data + index))[1] = htonl((int)(newData & 0xFFFFFFFF));
  }

  /// Gets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  uint64_t Box::getInt64(size_t index) {
    index += payloadOffset;
    if (index + 7 >= boxedSize()) {
      if (!reserve(index, 0, 8)) {
        return 0;
      }
      setInt64(0, index - payloadOffset);
    }
    uint64_t result = ntohl(((int *)(data + index))[0]);
    result <<= 32;
    result += ntohl(((int *)(data + index))[1]);
    return result;
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(std::string newData, size_t index) {
    setString((char *)newData.c_str(), newData.size(), index);
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(char * newData, size_t size, size_t index) {
    index += payloadOffset;
    if (index >= boxedSize()) {
      if (!reserve(index, 0, 1)) {
        return;
      }
      data[index] = 0;
    }
    if (getStringLen(index) != size) {
      if (!reserve(index, getStringLen(index) + 1, size + 1)) {
        return;
      }
    }
    memcpy(data + index, newData, size + 1);
  }

  /// Gets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string is out of range.
  /// Returns null if resizing failed.
  char * Box::getString(size_t index) {
    index += payloadOffset;
    if (index >= boxedSize()) {
      if (!reserve(index, 0, 1)) {
        return 0;
      }
      data[index] = 0;
    }
    return data + index;
  }

  /// Returns the length of the NULL-terminated string at the given index.
  /// Returns 0 if out of range.
  size_t Box::getStringLen(size_t index) {
    index += payloadOffset;
    if (index >= boxedSize()) {
      return 0;
    }
    return strlen(data + index);
  }

  /// Gets a reference to the box at the given index.
  /// Do not store or copy this reference, for there will be raptors.
  /// Will attempt to resize if out of range.
  /// Returns an 8-byte error box if resizing failed.
  Box & Box::getBox(size_t index) {
    static Box retbox = Box((char *)"\000\000\000\010erro", false);
    index += payloadOffset;
    if (index + 8 > boxedSize()) {
      if (!reserve(index, 0, 8)) {
        retbox = Box((char *)"\000\000\000\010erro", false);
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
  size_t Box::getBoxLen(size_t index) {
    if ((index + payloadOffset + 8) > boxedSize()) {
      return 0;
    }
    return getBox(index).boxedSize();
  }

  /// Replaces the existing box at the given index by the new box newEntry.
  /// Will resize if needed, will reserve new space if out of range.
  void Box::setBox(Box & newEntry, size_t index) {
    int oldlen = getBoxLen(index);
    int newlen = newEntry.boxedSize();
    if (oldlen != newlen && !reserve(index + payloadOffset, oldlen, newlen)) {
      return;
    }
    memcpy(data + index + payloadOffset, newEntry.asBox(), newlen);
  }

  /// Attempts to reserve enough space for wanted bytes of data at given position, where current bytes of data is now reserved.
  /// This will move any existing data behind the currently reserved space to the proper location after reserving.
  /// \returns True on success, false otherwise.
  bool Box::reserve(size_t position, size_t current, size_t wanted) {
    if (current == wanted) {
      return true;
    }
    if (position > boxedSize()) {
      wanted += position - boxedSize();
    }
    if (current < wanted) {
      //make bigger
      if (boxedSize() + (wanted - current) > data_size) {
        //realloc if managed, otherwise fail
        if (!managed) {
          return false;
        }
        void * ret = realloc(data, boxedSize() + (wanted - current));
        if (!ret) {
          return false;
        }
        data = (char *)ret;
        memset(data + boxedSize(), 0, wanted - current); //initialize to 0
        data_size = boxedSize() + (wanted - current);
      }
    }
    //move data behind, if any
    if (boxedSize() > (position + current)) {
      memmove(data + position + wanted, data + position + current, boxedSize() - (position + current));
    }
    //calculate and set new size
    if (payloadOffset != 16) {
      int newSize = boxedSize() + (wanted - current);
      ((int *)data)[0] = htonl(newSize);
    }
    return true;
  }

  fullBox::fullBox() {
    setVersion(0);
  }

  void fullBox::setVersion(char newVersion) {
    setInt8(newVersion, 0);
  }

  char fullBox::getVersion() {
    return getInt8(0);
  }

  void fullBox::setFlags(uint32_t newFlags) {
    setInt24(newFlags, 1);
  }

  uint32_t fullBox::getFlags() {
    return getInt24(1);
  }

  std::string fullBox::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent + 1, ' ') << "Version: " << (int)getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Flags: " << getFlags() << std::endl;
    return r.str();
  }

  containerBox::containerBox() {

  }

  uint32_t containerBox::getContentCount() {
    int res = 0;
    unsigned int tempLoc = 0;
    while (tempLoc < boxedSize() - 8) {
      res++;
      tempLoc += Box(getBox(tempLoc).asBox(), false).boxedSize();
    }
    return res;
  }

  void containerBox::setContent(Box & newContent, uint32_t no) {
    int tempLoc = 0;
    unsigned int contentCount = getContentCount();
    for (unsigned int i = 0; i < no; i++) {
      if (i < contentCount) {
        tempLoc += getBoxLen(tempLoc);
      } else {
        if (!reserve(tempLoc, 0, (no - contentCount) * 8)) {
          return;
        };
        memset(data + tempLoc, 0, (no - contentCount) * 8);
        tempLoc += (no - contentCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
  }

  Box & containerBox::getContent(uint32_t no) {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (no > getContentCount()) {
      return ret;
    }
    unsigned int i = 0;
    int tempLoc = 0;
    while (i < no) {
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string containerBox::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[" << getType() << "] Container Box (" << boxedSize() << ")" << std::endl;
    Box curBox;
    int tempLoc = 0;
    int contentCount = getContentCount();
    for (int i = 0; i < contentCount; i++) {
      curBox = getContent(i);
      r << curBox.toPrettyString(indent + 1);
      tempLoc += getBoxLen(tempLoc);
    }
    return r.str();
  }

  uint32_t containerFullBox::getContentCount() {
    int res = 0;
    unsigned int tempLoc = 4;
    while (tempLoc < boxedSize() - 8) {
      res++;
      tempLoc += getBoxLen(tempLoc);
    }
    return res;
  }

  void containerFullBox::setContent(Box & newContent, uint32_t no) {
    int tempLoc = 4;
    unsigned int contentCount = getContentCount();
    for (unsigned int i = 0; i < no; i++) {
      if (i < contentCount) {
        tempLoc += getBoxLen(tempLoc);
      } else {
        if (!reserve(tempLoc, 0, (no - contentCount) * 8)) {
          return;
        };
        memset(data + tempLoc, 0, (no - contentCount) * 8);
        tempLoc += (no - contentCount) * 8;
        break;
      }
    }
    setBox(newContent, tempLoc);
  }

  Box & containerFullBox::getContent(uint32_t no) {
    static Box ret = Box((char *)"\000\000\000\010erro", false);
    if (no > getContentCount()) {
      return ret;
    }
    unsigned int i = 0;
    int tempLoc = 4;
    while (i < no) {
      tempLoc += getBoxLen(tempLoc);
      i++;
    }
    return getBox(tempLoc);
  }

  std::string containerFullBox::toPrettyCFBString(uint32_t indent, std::string boxName) {
    std::stringstream r;
    r << std::string(indent, ' ') << boxName << " (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    Box curBox;
    int tempLoc = 4;
    int contentCount = getContentCount();
    for (int i = 0; i < contentCount; i++) {
      curBox = getContent(i);
      r << curBox.toPrettyString(indent + 1);
      tempLoc += getBoxLen(tempLoc);
    }
    return r.str();
  }
}
