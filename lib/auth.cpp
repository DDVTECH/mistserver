#include "auth.h"
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

namespace Secure {

  /// Calculates a MD5 digest as per rfc1321, returning it as a hexadecimal alphanumeric string.
  std::string md5(std::string input) {
    return md5(input.data(), input.size());
  }


  /// Adds 64 bytes of data to the current MD5 hash.
  /// hash is the current hash, represented by 4 unsigned longs.
  /// data is the 64 bytes of data that need to be added.
  static inline void md5_add64(uint32_t * hash, const char * data){
    //Inspired by the pseudocode as available on Wikipedia on March 2nd, 2015.
    uint32_t M[16];
    for (unsigned int i = 0; i < 16; ++i){
      M[i] = data[i << 2] | (data[(i<<2)+1] << 8) | (data[(i<<2)+2] << 16) | (data[(i<<2)+3] << 24);
    }
    static unsigned char shift[] = {7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22, 5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20, 4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23, 6, 10, 15, 21,  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    static uint32_t K[] = { 0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8, 0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665, 0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    uint32_t A = hash[0];
    uint32_t B = hash[1];
    uint32_t C = hash[2];
    uint32_t D = hash[3];
    for (unsigned int i = 0; i < 64; ++i){
      uint32_t F, g;
      if (i < 16){
        F = (B & C) | ((~B) & D);
        g = i;
      }else if (i < 32){
        F = (D & B) | ((~D) & C);
        g = (5*i + 1) % 16;
      }else if (i < 48){
        F = B ^ C ^ D;
        g = (3*i + 5) % 16;
      }else{
        F = C ^ (B | (~D));
        g = (7*i) % 16;
      }
      uint32_t dTemp = D;
      D = C;
      C = B;
      uint32_t x = A + F + K[i] + M[g];
      B += (x << shift[i] | (x >> (32-shift[i])));
      A = dTemp;
    }
    hash[0] += A;
    hash[1] += B;
    hash[2] += C;
    hash[3] += D;
  }

  /// Calculates a MD5 digest as per rfc1321, returning it as a hexadecimal alphanumeric string.
  std::string md5(const char * input, const unsigned int in_len){
    //Initialize the hash, according to MD5 spec.
    uint32_t hash[] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    //Add as many whole blocks of 64 bytes as possible from the input, until < 64 are left.
    unsigned int offset = 0;
    while (offset+64 <= in_len){
      md5_add64(hash, input+offset);
      offset += 64;
    }
    //now, copy the remainder to a 64 byte buffer.
    char buffer[64];
    memcpy(buffer, input+offset, in_len-offset);
    //Calculate how much we've filled in that buffer
    offset = in_len - offset;
    //We know at least 1 byte must be empty, so we can safely do this
    buffer[offset] = 0x80;//append 0x80
    //fill to the end of the buffer with zeroes
    memset(buffer+offset+1, 0, 64-offset-1);
    if (offset > 55){
      //There's no space for the length, add what we have and zero it
      md5_add64(hash, buffer);
      memset(buffer, 0, 64);
    }
    unsigned long long bit_len = in_len << 3;
    //Write the length into the last 8 bytes
    buffer[56] = (bit_len >> 0) & 0xff;
    buffer[57] = (bit_len >> 8) & 0xff;
    buffer[58] = (bit_len >> 16) & 0xff;
    buffer[59] = (bit_len >> 24) & 0xff;
    buffer[60] = (bit_len >> 32) & 0xff;
    buffer[61] = (bit_len >> 40) & 0xff;
    buffer[62] = (bit_len >> 48) & 0xff;
    buffer[63] = (bit_len >> 54) & 0xff;
    //Add the last bit of buffer
    md5_add64(hash, buffer);
    //convert hash to hexadecimal string
    char outstr[33];
    snprintf(outstr, 33, "%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x%02x", hash[0] & 0xff, (hash[0] >> 8) & 0xff, (hash[0] >> 16) & 0xff, (hash[0] >> 24) & 0xff, hash[1] & 0xff, (hash[1] >> 8) & 0xff, (hash[1] >> 16) & 0xff, (hash[1] >> 24) & 0xff, hash[2] & 0xff, (hash[2] >> 8) & 0xff, (hash[2] >> 16) & 0xff, (hash[2] >> 24) & 0xff, hash[3] & 0xff, (hash[3] >> 8) & 0xff, (hash[3] >> 16) & 0xff, (hash[3] >> 24) & 0xff);
    return std::string(outstr);
  }

}

