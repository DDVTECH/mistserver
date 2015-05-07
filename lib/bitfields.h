


namespace Bit{
  //bitfield getters
  unsigned long long getMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits);
  unsigned long long getByName(char * pointer);
  //bitfield setters
  void setMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits, unsigned long long value);
  void setByName(char * pointer);

  //Host to binary/binary to host functions - similar to kernel ntoh/hton functions.

  /// Retrieves a short in network order from the pointer p.
  inline unsigned short btohs(char * p) {
    return ((unsigned short)p[0] << 8) | p[1];
  }

  /// Stores a short value of val in network order to the pointer p.
  inline void htobs(char * p, unsigned short val) {
    p[0] = (val >> 8) & 0xFF;
    p[1] = val & 0xFF;
  }

  /// Retrieves a long in network order from the pointer p.
  inline unsigned long btohl(const char * p) {
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3];
  }

  /// Stores a long value of val in network order to the pointer p.
  inline void htobl(char * p, unsigned long val) {
    p[0] = (val >> 24) & 0xFF;
    p[1] = (val >> 16) & 0xFF;
    p[2] = (val >> 8) & 0xFF;
    p[3] = val & 0xFF;
  }

  /// Retrieves a long in network order from the pointer p.
  inline unsigned long btoh24(char * p) {
    return ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
  }

  /// Stores a long value of val in network order to the pointer p.
  inline void htob24(char * p, unsigned long val) {
    p[0] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[2] = val & 0xFF;
  }

  /// Retrieves a long long in network order from the pointer p.
  inline unsigned long long btohll(char * p) {
    return ((unsigned long long)p[0] << 56) | ((unsigned long long)p[1] << 48) | ((unsigned long long)p[2] << 40) | ((unsigned long long)p[3] << 32) | ((unsigned long)p[4] << 24) | ((unsigned long)p[5] << 16) | ((unsigned long)p[6] << 8) | p[7];
  }

  /// Stores a long value of val in network order to the pointer p.
  inline void htobll(char * p, unsigned long long val) {
    p[0] = (val >> 56) & 0xFF;
    p[1] = (val >> 48) & 0xFF;
    p[2] = (val >> 40) & 0xFF;
    p[3] = (val >> 32) & 0xFF;
    p[4] = (val >> 24) & 0xFF;
    p[5] = (val >> 16) & 0xFF;
    p[6] = (val >> 8) & 0xFF;
    p[7] = val & 0xFF;
  }

}

