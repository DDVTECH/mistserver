#pragma once
#include "mp4.h"

namespace MP4 {
  class SDTP: public Box {
    public:
      SDTP();
      void setVersion(uint32_t newVersion);
      uint32_t getVersion();
      void setValue(uint32_t newValue, size_t index);
      uint32_t getValue(size_t index);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID: public Box {
    public:
      UUID();
      std::string getUUID();
      void setUUID(const std::string & uuid_string);
      void setUUID(const char * raw_uuid);
      std::string toPrettyString(uint32_t indent = 0);
  };

  class UUID_TrackFragmentReference: public UUID {
    public:
      UUID_TrackFragmentReference();
      void setVersion(uint32_t newVersion);
      uint32_t getVersion();
      void setFlags(uint32_t newFlags);
      uint32_t getFlags();
      void setFragmentCount(uint32_t newCount);
      uint32_t getFragmentCount();
      void setTime(size_t num, uint64_t newTime);
      uint64_t getTime(size_t num);
      void setDuration(size_t num, uint64_t newDuration);
      uint64_t getDuration(size_t num);
      std::string toPrettyString(uint32_t indent = 0);
  };

}
