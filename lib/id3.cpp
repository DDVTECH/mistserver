#include "id3.h"
#include "bitfields.h"

namespace ID3{

  /// Returns true if this pointer contains a valid ID3 tag
  bool isID3(const char * p, const size_t l){
    //All tags are at least 10 bytes long
    if (l < 10){return false;}
    //Check for header bytes
    if (p[0] != 'I' || p[1] != 'D' || p[2] != '3'){return false;}
    return true;
  }
  
  ///Returns the total byte length of the passed ID3 tag, or 0 if invalid or unknown.
  size_t getID3Size(const char * p, const size_t l){
    if (!isID3(p, l)){return 0;}
    size_t len = 10 + ((p[5] & 0x10)?10:0);//length of header + optional footer
    return len + ss32dec(p+6);
  }

  uint32_t ss32dec(const char * p){
    uint32_t ret = ((uint32_t)(p[3] & 0x7F));
    ret += ((uint32_t)(p[2] & 0x7F)) << 7;
    ret += ((uint32_t)(p[1] & 0x7F)) << 14;
    ret += ((uint32_t)(p[0] & 0x7F)) << 21;
    return ret;
  }

  //Tag constructor, expects optional pointer and length. May both be zero.
  ID3::Tag::Tag(const char * p, const size_t l): ptr(p), len(l){
    if (!isID3(ptr, len)){
      FAIL_MSG("Attempted to parse a non-ID3 tag as ID3 tag!");
      return;
    }
  }



}//ID3 namespace


