#pragma once
#include <string>

namespace Util{
  bool stringToBool(std::string & str); 
}

namespace Bit{
  //bitfield getters
  unsigned long long getMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits);
  unsigned long long getByName(char * pointer);
  //bitfield setters
  void setMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits, unsigned long long value);
  void setByName(char * pointer);

  //Host to binary/binary to host functions - similar to kernel ntoh/hton functions.

  /// Retrieves a short in network order from the pointer p.
  inline unsigned short btohs(const char * p) {
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
  inline unsigned long btoh24(const char * p) {
    return ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
  }

  /// Stores a long value of val in network order to the pointer p.
  inline void htob24(char * p, unsigned long val) {
    p[0] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[2] = val & 0xFF;
  }

  /// Retrieves a 40-bit uint in network order from the pointer p.
  inline uint64_t btoh40(const char * p) {
    return ((uint64_t)p[0] << 32) | ((uint64_t)p[1] << 24) | ((uint64_t)p[2] << 16) | ((uint64_t)p[3] << 8) | p[4];
  }

  /// Stores a 40-bit uint value of val in network order to the pointer p.
  inline void htob40(char * p, uint64_t val) {
    p[0] = (val >> 32) & 0xFF;
    p[1] = (val >> 24) & 0xFF;
    p[2] = (val >> 16) & 0xFF;
    p[3] = (val >> 8) & 0xFF;
    p[4] = val & 0xFF;
  }

  /// Retrieves a 48-bit uint in network order from the pointer p.
  inline uint64_t btoh48(const char * p) {
    return ((uint64_t)p[0] << 40) | ((uint64_t)p[1] << 32) | ((uint64_t)p[2] << 24) | ((uint64_t)p[3] << 16) | ((uint64_t)p[4] << 8) | p[5];
  }

  /// Stores a 48-bit uint value of val in network order to the pointer p.
  inline void htob48(char * p, uint64_t val) {
    p[0] = (val >> 40) & 0xFF;
    p[1] = (val >> 32) & 0xFF;
    p[2] = (val >> 24) & 0xFF;
    p[3] = (val >> 16) & 0xFF;
    p[4] = (val >> 8) & 0xFF;
    p[5] = val & 0xFF;
  }

  /// Retrieves a 56-bit uint in network order from the pointer p.
  inline uint64_t btoh56(const char * p) {
    return ((uint64_t)p[0] << 48) | ((uint64_t)p[1] << 40) | ((uint64_t)p[2] << 32) | ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 16) | ((uint64_t)p[5] << 8) | p[6];
  }

  /// Stores a 56-bit uint value of val in network order to the pointer p.
  inline void htob56(char * p, uint64_t val) {
    p[0] = (val >> 48) & 0xFF;
    p[1] = (val >> 40) & 0xFF;
    p[2] = (val >> 32) & 0xFF;
    p[3] = (val >> 24) & 0xFF;
    p[4] = (val >> 16) & 0xFF;
    p[5] = (val >> 8) & 0xFF;
    p[6] = val & 0xFF;
  }

  /// Retrieves a long long in network order from the pointer p.
  inline unsigned long long btohll(const char * p) {
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

  inline float btohf(const char * p){
    uint32_t tmp = btohl(p);
    return *reinterpret_cast<float*>(&tmp);
  }

  inline float htobf(char * p, float val){
    htobl(p, *reinterpret_cast<unsigned long*>(&val));
  }

  inline double btohd(const char * p){
    uint64_t tmp = btohll(p);
    return *reinterpret_cast<double*>(&tmp);
  }

  inline float htobd(char * p, double val){
    htobll(p, *reinterpret_cast<unsigned long*>(&val));
  }

  /// Retrieves a short in little endian from the pointer p.
  inline unsigned short btohs_le(const char * p) {
    return ((unsigned short)p[1] << 8) | p[0];
  }

  /// Stores a short value of val in little endian to the pointer p.
  inline void htobs_le(char * p, unsigned short val) {
    p[1] = (val >> 8) & 0xFF;
    p[0] = val & 0xFF;
  }

  /// Retrieves a long in network order from the pointer p.
  inline unsigned long btohl_le(const char * p) {
    return ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) | p[0];
  }

  /// Stores a long value of val in little endian to the pointer p.
  inline void htobl_le(char * p, unsigned long val) {
    p[3] = (val >> 24) & 0xFF;
    p[2] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[0] = val & 0xFF;
  }

  /// Retrieves a long in little endian from the pointer p.
  inline unsigned long btoh24_le(const char * p) {
    return ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) | p[0];
  }

  /// Stores a long value of val in network order to the pointer p.
  inline void htob24_le(char * p, unsigned long val) {
    p[2] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[0] = val & 0xFF;
  }

  /// Retrieves a long long in little endian from the pointer p.
  inline unsigned long long btohll_le(const char * p) {
    return ((unsigned long long)p[7] << 56) | ((unsigned long long)p[6] << 48) | ((unsigned long long)p[5] << 40) | ((unsigned long long)p[4] << 32) | ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) | p[0];
  }

  /// Stores a long value of val in little endian to the pointer p.
  inline void htobll_le(char * p, unsigned long long val) {
    p[7] = (val >> 56) & 0xFF;
    p[6] = (val >> 48) & 0xFF;
    p[5] = (val >> 40) & 0xFF;
    p[4] = (val >> 32) & 0xFF;
    p[3] = (val >> 24) & 0xFF;
    p[2] = (val >> 16) & 0xFF;
    p[1] = (val >> 8) & 0xFF;
    p[0] = val & 0xFF;
  }

}

