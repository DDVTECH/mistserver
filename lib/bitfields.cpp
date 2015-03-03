#include "bitfields.h"

/// Takes a pointer, offset bitcount and data bitcount, returning the unsigned int read from the givens.
/// offsetBits may be > 7, in which case offsetBits / 8 is added to the pointer automatically.
/// This function assumes Most Significant Bits first.
/// If dataBits > 64, only the last 64 bits are returned.
unsigned long long Bit::getMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits){
  //If the offset is a whole byte or more, add the whole bytes to the pointer instead.
  pointer += offsetBits >> 3;
  //The offset is now guaranteed less than a whole byte.
  offsetBits &= 0x07;
  unsigned long long retVal = 0;
  //Now we parse the remaining bytes
  while (dataBits){
    //Calculate how many bits we're reading from this byte
    //We assume all except for the offset
    unsigned int curBits = 8 - offsetBits;
    //If that is too much, we use the remainder instead
    if (curBits > dataBits){
      curBits = dataBits;
    }
    //First, shift the current return value by the amount of bits we're adding
    retVal <<= curBits;
    //Next, add those bits from the current pointer position at the correct offset, increasing the pointer by one
    retVal |= ((int)(*(pointer++)) << offsetBits) >> (8 - curBits);
    //Finally, set the offset to zero and remove curBits from dataBits.
    offsetBits = 0;
    dataBits -= curBits;
  }//Loop until we run out of dataBits, then return the result
  return retVal;
}

/// Takes a pointer, offset bitcount and data bitcount, setting to given value.
/// offsetBits may be > 7, in which case offsetBits / 8 is added to the pointer automatically.
/// This function assumes Most Significant Bits first.
/// WARNING: UNFINISHED. DO NOT USE.
/// \todo Finish writing this - untested atm.
void Bit::setMSB(char * pointer, unsigned int offsetBits, unsigned int dataBits, unsigned long long value){
  //Set the pointer to the last byte we need to be setting
  pointer += (offsetBits + dataBits) >> 3;
  //The offset is now guaranteed less than a whole byte.
  offsetBits = (offsetBits + dataBits) & 0x07;
  unsigned long long retVal = 0;
  //Now we set the remaining bytes
  while (dataBits){
    //Calculate how many bits we're setting in this byte
    //We assume all that will fit in the current byte
    unsigned int curBits = offsetBits;
    //If that is too much, we use the remainder instead
    if (curBits > dataBits){
      curBits = dataBits;
    }
    //Set the current pointer position at the correct offset, increasing the pointer by one
    retVal |= ((int)(*(pointer++)) << offsetBits) >> (8 - curBits);
    *pointer = (((*pointer) << offsetBits) >> offsetBits) | ((value & 0xFF) << (8 - offsetBits));
    --pointer;
    //Finally, shift the current value by the amount of bits we're adding
    value >>= offsetBits;
    //... and set the offset to eight and remove curBits from dataBits.
    offsetBits = 8;
    dataBits -= curBits;
  }//Loop until we run out of dataBits, then return the result
}

