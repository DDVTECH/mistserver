#pragma once
#include "mp4.h"

namespace MP4 {
  struct sidxReference {
    bool referenceType;
    uint32_t referencedSize;
    uint32_t subSegmentDuration;
    bool sapStart;
    uint8_t sapType;
    uint32_t sapDeltaTime;
  };

  class SIDX: public fullBox {
    public:
      SIDX();
      void setReferenceID(uint32_t newReferenceID);
      uint32_t getReferenceID();
      void setTimescale(uint32_t newTimescale);
      uint32_t getTimescale();

      void setEarliestPresentationTime(uint64_t newEarliestPresentationTime);
      uint64_t getEarliestPresentationTime();
      void setFirstOffset(uint64_t newFirstOffset);
      uint64_t getFirstOffset();

      uint16_t getReferenceCount();
      void setReference(sidxReference & newRef, size_t index);
      sidxReference getReference(size_t index);

      std::string toPrettyString(uint32_t indent = 0);
  };

  class TFDT: public fullBox {
    public:
      TFDT();
      void setBaseMediaDecodeTime(uint64_t newBaseMediaDecodeTime);
      uint64_t getBaseMediaDecodeTime();

      std::string toPrettyString(uint32_t indent = 0);
  };

  class IODS: public fullBox {
    public:
      IODS();
      void setIODTypeTag(char value);
      char getIODTypeTag();

      void setDescriptorTypeLength(char length);
      char getDescriptorTypeLength();

      void setODID(short id);
      short getODID();

      void setODProfileLevel(char value);
      char getODProfileLevel();

      void setODSceneLevel(char value);
      char getODSceneLevel();

      void setODAudioLevel(char value);
      char getODAudioLevel();

      void setODVideoLevel(char value);
      char getODVideoLevel();

      void setODGraphicsLevel(char value);
      char getODGraphicsLevel();

      std::string toPrettyString(uint32_t indent = 0);
  };
}

