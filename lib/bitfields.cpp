#include "bitfields.h"
#include <string.h>

/// Takes a pointer, offset bitcount and data bitcount, returning the unsigned int read from the
/// givens. offsetBits may be > 7, in which case offsetBits / 8 is added to the pointer
/// automatically. This function assumes Most Significant Bits first. If dataBits > 64, only the
/// last 64 bits are returned.
unsigned long long Bit::getMSB(char *pointer, unsigned int offsetBits, unsigned int dataBits){
  // If the offset is a whole byte or more, add the whole bytes to the pointer instead.
  pointer += offsetBits >> 3;
  // The offset is now guaranteed less than a whole byte.
  offsetBits &= 0x07;
  unsigned long long retVal = 0;
  // Now we parse the remaining bytes
  while (dataBits){
    // Calculate how many bits we're reading from this byte
    // We assume all except for the offset
    unsigned int curBits = 8 - offsetBits;
    // If that is too much, we use the remainder instead
    if (curBits > dataBits){curBits = dataBits;}
    // First, shift the current return value by the amount of bits we're adding
    retVal <<= curBits;
    // Next, add those bits from the current pointer position at the correct offset, increasing the
    // pointer by one
    retVal |= ((int)(*(pointer++)) << offsetBits) >> (8 - curBits);
    // Finally, set the offset to zero and remove curBits from dataBits.
    offsetBits = 0;
    dataBits -= curBits;
  }// Loop until we run out of dataBits, then return the result
  return retVal;
}

/// Takes a pointer, offset bitcount and data bitcount, setting to given value.
/// offsetBits may be > 7, in which case offsetBits / 8 is added to the pointer automatically.
/// This function assumes Most Significant Bits first.
/// WARNING: UNFINISHED. DO NOT USE.
/// \todo Finish writing this - untested atm.
void Bit::setMSB(char *pointer, unsigned int offsetBits, unsigned int dataBits, unsigned long long value){
  WARN_MSG("setMSB is unfinished");
  // Set the pointer to the last byte we need to be setting
  pointer += (offsetBits + dataBits) >> 3;
  // The offset is now guaranteed less than a whole byte.
  offsetBits = (offsetBits + dataBits) & 0x07;
  // Now we set the remaining bytes
  while (dataBits){
    // Calculate how many bits we're setting in this byte
    // We assume all that will fit in the current byte
    unsigned int curBits = offsetBits;
    // If that is too much, we use the remainder instead
    if (curBits > dataBits){curBits = dataBits;}
    // Set the current pointer position at the correct offset, increasing the pointer by one
    *pointer = (((*pointer) << offsetBits) >> offsetBits) | ((value & 0xFF) << (8 - offsetBits));
    --pointer;
    // Finally, shift the current value by the amount of bits we're adding
    value >>= offsetBits;
    //... and set the offset to eight and remove curBits from dataBits.
    offsetBits = 8;
    dataBits -= curBits;
  }// Loop until we run out of dataBits, then return the result
}

/// Parses a string reference to a boolean.
/// Returns true if the string, with whitespace removed and converted to lowercase, prefix-matches
/// any of: "1", "yes", "true", "cont". Returns false otherwise.
bool Util::stringToBool(std::string &str){
  std::string tmp;
  tmp.reserve(4);
  for (unsigned int i = 0; i < str.size() && tmp.size() < 4; ++i){
    if (!::isspace(str[i])){tmp.push_back((char)tolower(str[i]));}
  }
  return (strncmp(tmp.c_str(), "1", 1) == 0 || strncmp(tmp.c_str(), "yes", 3) == 0 ||
          strncmp(tmp.c_str(), "true", 4) == 0 || strncmp(tmp.c_str(), "cont", 4) == 0);
}
