#include "mp4_ms.h"

namespace MP4 {

  static char c2hex(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
  }


  SDTP::SDTP() {
    memcpy(data + 4, "sdtp", 4);
  }

  void SDTP::setVersion(uint32_t newVersion) {
    setInt8(newVersion, 0);
  }

  uint32_t SDTP::getVersion() {
    return getInt8(0);
  }

  void SDTP::setValue(uint32_t newValue, size_t index) {
    setInt8(newValue, index);
  }

  uint32_t SDTP::getValue(size_t index) {
    return getInt8(index);
  }

  std::string SDTP::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[sdtp] Sample Dependancy Type (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Samples: " << (boxedSize() - 12) << std::endl;
    for (size_t i = 1; i <= boxedSize() - 12; ++i) {
      uint32_t val = getValue(i + 3);
      r << std::string(indent + 2, ' ') << "[" << i << "] = ";
      switch (val & 3) {
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
      switch (val & 12) {
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
      switch (val & 48) {
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

  UUID::UUID() {
    memcpy(data + 4, "uuid", 4);
    setInt64(0, 0);
    setInt64(0, 8);
  }

  std::string UUID::getUUID() {
    std::stringstream r;
    r << std::hex;
    for (int i = 0; i < 16; ++i) {
      if (i == 4 || i == 6 || i == 8 || i == 10) {
        r << "-";
      }
      r << std::setfill('0') << std::setw(2) << std::right << (int)(data[8 + i]);
    }
    return r.str();
  }

  void UUID::setUUID(const std::string & uuid_string) {
    //reset UUID to zero
    for (int i = 0; i < 4; ++i) {
      ((uint32_t *)(data + 8))[i] = 0;
    }
    //set the UUID from the string, char by char
    int i = 0;
    for (size_t j = 0; j < uuid_string.size(); ++j) {
      if (uuid_string[j] == '-') {
        continue;
      }
      data[8 + i / 2] |= (c2hex(uuid_string[j]) << ((~i & 1) << 2));
      ++i;
    }
  }

  void UUID::setUUID(const char * raw_uuid) {
    memcpy(data + 8, raw_uuid, 16);
  }

  std::string UUID::toPrettyString(uint32_t indent) {
    std::string UUID = getUUID();
    if (UUID == "d4807ef2-ca39-4695-8e54-26cb9e46a79f") {
      return ((UUID_TrackFragmentReference *)this)->toPrettyString(indent);
    }
    std::stringstream r;
    r << std::string(indent, ' ') << "[uuid] Extension box (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "UUID: " << UUID << std::endl;
    r << std::string(indent + 1, ' ') << "Unknown UUID - ignoring contents." << std::endl;
    return r.str();
  }

  UUID_TrackFragmentReference::UUID_TrackFragmentReference() {
    setUUID((std::string)"d4807ef2-ca39-4695-8e54-26cb9e46a79f");
  }

  void UUID_TrackFragmentReference::setVersion(uint32_t newVersion) {
    setInt8(newVersion, 16);
  }

  uint32_t UUID_TrackFragmentReference::getVersion() {
    return getInt8(16);
  }

  void UUID_TrackFragmentReference::setFlags(uint32_t newFlags) {
    setInt24(newFlags, 17);
  }

  uint32_t UUID_TrackFragmentReference::getFlags() {
    return getInt24(17);
  }

  void UUID_TrackFragmentReference::setFragmentCount(uint32_t newCount) {
    setInt8(newCount, 20);
  }

  uint32_t UUID_TrackFragmentReference::getFragmentCount() {
    return getInt8(20);
  }

  void UUID_TrackFragmentReference::setTime(size_t num, uint64_t newTime) {
    if (getVersion() == 0) {
      setInt32(newTime, 21 + (num * 8));
    } else {
      setInt64(newTime, 21 + (num * 16));
    }
  }

  uint64_t UUID_TrackFragmentReference::getTime(size_t num) {
    if (getVersion() == 0) {
      return getInt32(21 + (num * 8));
    } else {
      return getInt64(21 + (num * 16));
    }
  }

  void UUID_TrackFragmentReference::setDuration(size_t num, uint64_t newDuration) {
    if (getVersion() == 0) {
      setInt32(newDuration, 21 + (num * 8) + 4);
    } else {
      setInt64(newDuration, 21 + (num * 16) + 8);
    }
  }

  uint64_t UUID_TrackFragmentReference::getDuration(size_t num) {
    if (getVersion() == 0) {
      return getInt32(21 + (num * 8) + 4);
    } else {
      return getInt64(21 + (num * 16) + 8);
    }
  }

  std::string UUID_TrackFragmentReference::toPrettyString(uint32_t indent) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[d4807ef2-ca39-4695-8e54-26cb9e46a79f] Track Fragment Reference (" << boxedSize() << ")" << std::endl;
    r << std::string(indent + 1, ' ') << "Version: " << getVersion() << std::endl;
    r << std::string(indent + 1, ' ') << "Fragments: " << getFragmentCount() << std::endl;
    int j = getFragmentCount();
    for (int i = 0; i < j; ++i) {
      r << std::string(indent + 2, ' ') << "[" << i << "] Time = " << getTime(i) << ", Duration = " << getDuration(i) << std::endl;
    }
    return r.str();
  }
}
