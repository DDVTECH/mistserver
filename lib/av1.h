#pragma once
#include <cstddef>
#include <string>

namespace AV1 {

  std::string leb128(size_t i);

  class OBU {
    public:
      OBU() { init(0, 0); }
      OBU(const void *ptr, size_t len) { init(ptr, len); }
      void init(const void *ptr, size_t len);
      operator bool() { return p && l; }
      std::string getType();
      std::string toString();
      bool isKeyframe();
      size_t getSize() { return l; }
      size_t getPayloadSize() { return pl_size; }
      size_t getHeaderSize() { return hdr_size; }
      void *data() { return p; }

    private:
      char *p;
      size_t l;
      size_t pl_size;
      size_t hdr_size;
  };

}; // namespace AV1
