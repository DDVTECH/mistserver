#include "mp4_dash.h"
#include "defines.h"

namespace MP4 {
  SIDX::SIDX() {
    memcpy(data + 4, "sidx", 4);
    setVersion(0);
    setFlags(0);
  }

  void SIDX::setReferenceID(uint32_t newReferenceID) {
    setInt32(newReferenceID, 4);
  }

  uint32_t SIDX::getReferenceID() {
    return getInt32(4);
  }

  void SIDX::setTimescale(uint32_t newTimescale) {
    setInt32(newTimescale, 8);
  }

  uint32_t SIDX::getTimescale() {
    return getInt32(8);
  }

  void SIDX::setEarliestPresentationTime(uint64_t newEarliestPresentationTime) {
    if (getVersion() == 0) {
      setInt32(newEarliestPresentationTime, 12);
    } else {
      setInt64(newEarliestPresentationTime, 12);
    }
  }

  uint64_t SIDX::getEarliestPresentationTime() {
    if (getVersion() == 0) {
      return getInt32(12);
    }
    return getInt64(12);
  }

  void SIDX::setFirstOffset(uint64_t newFirstOffset) {
    if (getVersion() == 0) {
      setInt32(newFirstOffset, 16);
    } else {
      setInt64(newFirstOffset, 20);
    }
  }

  uint64_t SIDX::getFirstOffset() {
    if (getVersion() == 0) {
      return getInt32(16);
    }
    return getInt64(20);
  }

  uint16_t SIDX::getReferenceCount() {
    if (getVersion() == 0) {
      return getInt16(22);
    }
    return getInt16(30);
  }

  void SIDX::setReference(sidxReference & newRef, size_t index) {
    if (index >= getReferenceCount()) {
      setInt16(index + 1, (getVersion() == 0 ? 22 : 30));
    }
    uint32_t offset = 24 + (index * 12) + (getVersion() == 0 ? 0 : 8);
    uint32_t tmp = (newRef.referenceType ? 0x80000000 : 0) | newRef.referencedSize;
    setInt32(tmp, offset);
    setInt32(newRef.subSegmentDuration, offset + 4);
    tmp = (newRef.sapStart ? 0x80000000 : 0) | ((newRef.sapType & 0x7) << 24) | newRef.sapDeltaTime;
    setInt32(tmp, offset + 8);
  }

  sidxReference SIDX::getReference(size_t index) {
    sidxReference result;
    if (index >= getReferenceCount()) {
      DEBUG_MSG(DLVL_DEVEL, "Warning, attempt to obtain reference out of bounds");
      return result;
    }
    uint32_t offset = 24 + (index * 12) + (getVersion() == 0 ? 0 : 8);
    uint32_t tmp = getInt32(offset);
    result.referenceType = tmp & 0x80000000;
    result.referencedSize = tmp & 0x7FFFFFFF;
    result.subSegmentDuration = getInt32(offset + 4);
    tmp = getInt32(offset + 8);
    result.sapStart = tmp & 0x80000000;
    result.sapType = (tmp & 0x70000000) >> 24;
    result.sapDeltaTime = (tmp & 0x0FFFFFFF);
    return result;
  }

  std::string SIDX::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[sidx] Segment Index Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r <<  std::string(indent + 1, ' ') << "ReferenceID " << getReferenceID() << std::endl;
    r <<  std::string(indent + 1, ' ') << "Timescale " << getTimescale() << std::endl;
    r <<  std::string(indent + 1, ' ') << "EarliestPresentationTime " << getEarliestPresentationTime() << std::endl;
    r <<  std::string(indent + 1, ' ') << "FirstOffset " << getFirstOffset() << std::endl;
    r <<  std::string(indent + 1, ' ') << "References [" << getReferenceCount() << "]" << std::endl;
    for (int i = 0; i < getReferenceCount(); i++) {
      sidxReference tmp = getReference(i);
      r <<  std::string(indent + 2, ' ') << "[" << i << "]" << std::endl;
      r <<  std::string(indent + 3, ' ') << "ReferenceType " << (int)tmp.referenceType << std::endl;
      r <<  std::string(indent + 3, ' ') << "ReferencedSize " << tmp.referencedSize << std::endl;
      r <<  std::string(indent + 3, ' ') << "SubSegmentDuration " << tmp.subSegmentDuration << std::endl;
      r <<  std::string(indent + 3, ' ') << "StartsWithSAP " << (int)tmp.sapStart << std::endl;
      r <<  std::string(indent + 3, ' ') << "SAP Type " << (int)tmp.sapType << std::endl;
      r <<  std::string(indent + 3, ' ') << "SAP DeltaTime " << tmp.sapDeltaTime << std::endl;
    }
    return r.str();
  }

  TFDT::TFDT() {
    memcpy(data + 4, "tfdt", 4);
    setVersion(0);
    setFlags(0);
  }

  void TFDT::setBaseMediaDecodeTime(uint64_t newBaseMediaDecodeTime) {
    if (getVersion() == 1) {
      setInt64(newBaseMediaDecodeTime, 4);
    } else {
      setInt32(newBaseMediaDecodeTime, 4);
    }
  }

  uint64_t TFDT::getBaseMediaDecodeTime() {
    if (getVersion() == 1) {
      return getInt64(4);
    }
    return getInt32(4);
  }

  std::string TFDT::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[tfdt] Track Fragment Base Media Decode Time Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r <<  std::string(indent + 1, ' ') << "BaseMediaDecodeTime " << getBaseMediaDecodeTime() << std::endl;
    return r.str();
  }

  IODS::IODS() {
    memcpy(data + 4, "iods", 4);
    setVersion(0);
    setFlags(0);
    setIODTypeTag(0x10);
    setDescriptorTypeLength(0x07);
    setODID(0x004F);
    setODProfileLevel(0xFF);
    setODSceneLevel(0xFF);
    setODAudioLevel(0xFF);
    setODVideoLevel(0xFF);
    setODGraphicsLevel(0xFF);
  }

  void IODS::setIODTypeTag(char value) {
    setInt8(value, 4);
  }

  char IODS::getIODTypeTag() {
    return getInt8(4);
  }

  void IODS::setDescriptorTypeLength(char length) {
    setInt8(length, 5);
  }

  char IODS::getDescriptorTypeLength() {
    return getInt8(5);
  }

  void IODS::setODID(short id) {
    setInt16(id, 6);
  }

  short IODS::getODID() {
    return getInt16(6);
  }

  void IODS::setODProfileLevel(char value) {
    setInt8(value, 8);
  }

  char IODS::getODProfileLevel() {
    return getInt8(8);
  }

  void IODS::setODSceneLevel(char value) {
    setInt8(value, 9);
  }

  char IODS::getODSceneLevel() {
    return getInt8(9);
  }

  void IODS::setODAudioLevel(char value) {
    setInt8(value, 10);
  }

  char IODS::getODAudioLevel() {
    return getInt8(10);
  }

  void IODS::setODVideoLevel(char value) {
    setInt8(value, 11);
  }

  char IODS::getODVideoLevel() {
    return getInt8(11);
  }

  void IODS::setODGraphicsLevel(char value) {
    setInt8(value, 12);
  }

  char IODS::getODGraphicsLevel() {
    return getInt8(12);
  }


  std::string IODS::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[iods] IODS Box (" << boxedSize() << ")" << std::endl;
    r << fullBox::toPrettyString(indent);
    r << std::string(indent + 2, ' ') << "IOD Type Tag: " << std::hex << std::setw(2) << std::setfill('0') << (int)getIODTypeTag() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "DescriptorTypeLength: " << std::hex << std::setw(2) << std::setfill('0') << (int)getDescriptorTypeLength() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD ID: " << std::hex << std::setw(4) << std::setfill('0') << (int)getODID() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD Profile Level: " << std::hex << std::setw(2) << std::setfill('0') << (int)getODProfileLevel() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD Scene Level: " << std::hex << std::setw(2) << std::setfill('0') << (int)getODSceneLevel() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD Audio Level: " << std::hex << std::setw(2) << std::setfill('0') << (int)getODAudioLevel() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD Video Level: " << std::hex << std::setw(2) << std::setfill('0') << (int)getODVideoLevel() << std::dec << std::endl;
    r << std::string(indent + 2, ' ') << "OD Graphics Level: " << std::hex << std::setw(2) << std::setfill('0') << (int)getODGraphicsLevel() << std::dec << std::endl;
    return r.str();
  }
}


