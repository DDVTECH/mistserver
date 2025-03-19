#include "auth.h"

#include "encode.h"

#include <inttypes.h>
#include <iomanip>
#include <sstream>
#include <stdio.h>
#include <string.h>

namespace Secure{

  /// Calculates a MD5 digest as per rfc1321, returning it as a hexadecimal alphanumeric string.
  std::string md5(std::string input){return md5(input.data(), input.size());}

  /// Calculates a MD5 digest as per rfc1321, returning it as a hexadecimal alphanumeric string.
  std::string md5(const char *input, size_t in_len) {
    char output[16];
    md5bin(input, in_len, output);
    std::stringstream outStr;
    for (size_t i = 0; i < 16; ++i) {
      outStr << std::hex << std::setw(2) << std::setfill('0') << (size_t)(output[i] & 0xff);
    }
    return outStr.str();
  }

  /// Calculates a SHA256 digest as per NSAs SHA-2, returning it as a hexadecimal alphanumeric string.
  std::string sha256(std::string input){return sha256(input.data(), input.size());}

  /// Calculates a SHA256 digest as per NSAs SHA-2, returning it as a hexadecimal alphanumeric string.
  std::string sha256(const char *input, size_t in_len) {
    char output[32];
    sha256bin(input, in_len, output);
    std::stringstream outStr;
    for (size_t i = 0; i < 32; ++i) {
      outStr << std::hex << std::setw(2) << std::setfill('0') << (size_t)(output[i] & 0xff);
    }
    return outStr.str();
  }

  /// Adds 64 bytes of data to the current MD5 hash.
  /// hash is the current hash, represented by 4 unsigned longs.
  /// data is the 64 bytes of data that need to be added.
  static inline void md5_add64(uint32_t *hash, const char *data){
    // Inspired by the pseudocode as available on Wikipedia on March 2nd, 2015.
    uint32_t M[16];
    for (size_t i = 0; i < 16; ++i) {
      M[i] = data[i << 2] | (data[(i << 2) + 1] << 8) | (data[(i << 2) + 2] << 16) |
             (data[(i << 2) + 3] << 24);
    }
    static unsigned char shift[] ={7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                    5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    static uint32_t K[] ={0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
                           0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
                           0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
                           0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
                           0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
                           0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
                           0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
                           0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
                           0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
                           0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
                           0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    uint32_t A = hash[0];
    uint32_t B = hash[1];
    uint32_t C = hash[2];
    uint32_t D = hash[3];
    for (size_t i = 0; i < 64; ++i) {
      uint32_t F, g;
      if (i < 16){
        F = (B & C) | ((~B) & D);
        g = i;
      }else if (i < 32){
        F = (D & B) | ((~D) & C);
        g = (5 * i + 1) % 16;
      }else if (i < 48){
        F = B ^ C ^ D;
        g = (3 * i + 5) % 16;
      }else{
        F = C ^ (B | (~D));
        g = (7 * i) % 16;
      }
      uint32_t dTemp = D;
      D = C;
      C = B;
      uint32_t x = A + F + K[i] + M[g];
      B += (x << shift[i] | (x >> (32 - shift[i])));
      A = dTemp;
    }
    hash[0] += A;
    hash[1] += B;
    hash[2] += C;
    hash[3] += D;
  }

  /// Calculates a MD5 digest as per rfc1321, returning it as binary.
  /// Assumes output is big enough to contain 16 bytes of data.
  void md5bin(const char *input, size_t in_len, char *output) {
    // Initialize the hash, according to MD5 spec.
    uint32_t hash[] ={0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    // Add as many whole blocks of 64 bytes as possible from the input, until < 64 are left.
    size_t offset = 0;
    while (offset + 64 <= in_len){
      md5_add64(hash, input + offset);
      offset += 64;
    }
    // now, copy the remainder to a 64 byte buffer.
    char buffer[64];
    memcpy(buffer, input + offset, in_len - offset);
    // Calculate how much we've filled in that buffer
    offset = in_len - offset;
    // We know at least 1 byte must be empty, so we can safely do this
    buffer[offset] = 0x80; // append 0x80
    // fill to the end of the buffer with zeroes
    memset(buffer + offset + 1, 0, 64 - offset - 1);
    if (offset > 55){
      // There's no space for the length, add what we have and zero it
      md5_add64(hash, buffer);
      memset(buffer, 0, 64);
    }
    unsigned long long bit_len = in_len << 3;
    // Write the length into the last 8 bytes
    buffer[56] = (bit_len >> 0) & 0xff;
    buffer[57] = (bit_len >> 8) & 0xff;
    buffer[58] = (bit_len >> 16) & 0xff;
    buffer[59] = (bit_len >> 24) & 0xff;
    buffer[60] = (bit_len >> 32) & 0xff;
    buffer[61] = (bit_len >> 40) & 0xff;
    buffer[62] = (bit_len >> 48) & 0xff;
    buffer[63] = (bit_len >> 54) & 0xff;
    // Add the last bit of buffer
    md5_add64(hash, buffer);
    // Write to output
    // convert hash to hexadecimal string
    output[0] = (hash[0] >> 0) & 0xff;
    output[1] = (hash[0] >> 8) & 0xff;
    output[2] = (hash[0] >> 16) & 0xff;
    output[3] = (hash[0] >> 24) & 0xff;
    output[4] = (hash[1] >> 0) & 0xff;
    output[5] = (hash[1] >> 8) & 0xff;
    output[6] = (hash[1] >> 16) & 0xff;
    output[7] = (hash[1] >> 24) & 0xff;
    output[8] = (hash[2] >> 0) & 0xff;
    output[9] = (hash[2] >> 8) & 0xff;
    output[10] = (hash[2] >> 16) & 0xff;
    output[11] = (hash[2] >> 24) & 0xff;
    output[12] = (hash[3] >> 0) & 0xff;
    output[13] = (hash[3] >> 8) & 0xff;
    output[14] = (hash[3] >> 16) & 0xff;
    output[15] = (hash[3] >> 24) & 0xff;
  }

  /// Adds 64 bytes of data to the current SHA256 hash.
  /// hash is the current hash, represented by 8 unsigned longs.
  /// data is the 64 bytes of data that need to be added.
  static inline void sha256_add64(uint32_t *hash, const char *data){
    // Inspired by the pseudocode as available on Wikipedia on March 3rd, 2015.
    uint32_t w[64];
    for (size_t i = 0; i < 16; ++i) {
      w[i] = (uint32_t)data[(i << 2) + 3] | ((uint32_t)data[(i << 2) + 2] << 8) |
             ((uint32_t)data[(i << 2) + 1] << 16) | ((uint32_t)data[(i << 2) + 0] << 24);
    }

    // Right rotate function. Shifts bytes off the least significant end, wrapping them to the most significant end.
    auto rr = [](uint64_t x, size_t c) -> uint64_t { return (x << (32 - c)) | ((x & 0xFFFFFFFF) >> c); };

    for (size_t i = 16; i < 64; ++i) {
      uint32_t s0 = rr(w[i - 15], 7) ^ rr(w[i - 15], 18) ^ ((w[i - 15] & 0xFFFFFFFF) >> 3);
      uint32_t s1 = rr(w[i - 2], 17) ^ rr(w[i - 2], 19) ^ ((w[i - 2] & 0xFFFFFFFF) >> 10);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    static uint32_t k[] ={0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
                           0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
                           0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
                           0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
                           0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
                           0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
                           0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
                           0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
                           0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
                           0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
                           0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
    uint32_t a = hash[0];
    uint32_t b = hash[1];
    uint32_t c = hash[2];
    uint32_t d = hash[3];
    uint32_t e = hash[4];
    uint32_t f = hash[5];
    uint32_t g = hash[6];
    uint32_t h = hash[7];
    for (size_t i = 0; i < 64; ++i) {
      uint32_t temp1 = h + (rr(e, 6) ^ rr(e, 11) ^ rr(e, 25)) + (g ^ (e & (f ^ g))) + k[i] + w[i];
      uint32_t temp2 = (rr(a, 2) ^ rr(a, 13) ^ rr(a, 22)) + ((a & b) | (c & (a | b)));
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  /// Calculates a SHA256 digest as per NSAs SHA-2, returning it as binary.
  /// Assumes output is big enough to contain 32 bytes of data.
  void sha256bin(const char *input, size_t in_len, char *output) {
    // Initialize the hash, according to MD5 spec.
    uint32_t hash[] ={0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    // Add as many whole blocks of 64 bytes as possible from the input, until < 64 are left.
    size_t offset = 0;
    while (offset + 64 <= in_len){
      sha256_add64(hash, input + offset);
      offset += 64;
    }
    // now, copy the remainder to a 64 byte buffer.
    char buffer[64];
    memcpy(buffer, input + offset, in_len - offset);
    // Calculate how much we've filled in that buffer
    offset = in_len - offset;
    // We know at least 1 byte must be empty, so we can safely do this
    buffer[offset] = 0x80; // append 0x80
    // fill to the end of the buffer with zeroes
    memset(buffer + offset + 1, 0, 64 - offset - 1);
    if (offset > 55){
      // There's no space for the length, add what we have and zero it
      sha256_add64(hash, buffer);
      memset(buffer, 0, 64);
    }
    unsigned long long bit_len = in_len << 3;
    // Write the length into the last 8 bytes
    buffer[56] = (bit_len >> 54) & 0xff;
    buffer[57] = (bit_len >> 48) & 0xff;
    buffer[58] = (bit_len >> 40) & 0xff;
    buffer[59] = (bit_len >> 32) & 0xff;
    buffer[60] = (bit_len >> 24) & 0xff;
    buffer[61] = (bit_len >> 16) & 0xff;
    buffer[62] = (bit_len >> 8) & 0xff;
    buffer[63] = (bit_len >> 0) & 0xff;
    // Add the last bit of buffer
    sha256_add64(hash, buffer);
    // Write result to output
    output[3] = hash[0] & 0xff;
    output[2] = (hash[0] >> 8) & 0xff;
    output[1] = (hash[0] >> 16) & 0xff;
    output[0] = (hash[0] >> 24) & 0xff;
    output[7] = hash[1] & 0xff;
    output[6] = (hash[1] >> 8) & 0xff;
    output[5] = (hash[1] >> 16) & 0xff;
    output[4] = (hash[1] >> 24) & 0xff;
    output[11] = hash[2] & 0xff;
    output[10] = (hash[2] >> 8) & 0xff;
    output[9] = (hash[2] >> 16) & 0xff;
    output[8] = (hash[2] >> 24) & 0xff;
    output[15] = hash[3] & 0xff;
    output[14] = (hash[3] >> 8) & 0xff;
    output[13] = (hash[3] >> 16) & 0xff;
    output[12] = (hash[3] >> 24) & 0xff;
    output[19] = hash[4] & 0xff;
    output[18] = (hash[4] >> 8) & 0xff;
    output[17] = (hash[4] >> 16) & 0xff;
    output[16] = (hash[4] >> 24) & 0xff;
    output[23] = hash[5] & 0xff;
    output[22] = (hash[5] >> 8) & 0xff;
    output[21] = (hash[5] >> 16) & 0xff;
    output[20] = (hash[5] >> 24) & 0xff;
    output[27] = hash[6] & 0xff;
    output[26] = (hash[6] >> 8) & 0xff;
    output[25] = (hash[6] >> 16) & 0xff;
    output[24] = (hash[6] >> 24) & 0xff;
    output[31] = hash[7] & 0xff;
    output[30] = (hash[7] >> 8) & 0xff;
    output[29] = (hash[7] >> 16) & 0xff;
    output[28] = (hash[7] >> 24) & 0xff;
  }

  static inline void sha512_add128(uint64_t *hash, const char *data) {
    uint64_t w[80];
    // Initialize first 16 words from input (big-endian)
    for (size_t i = 0; i < 16; ++i) {
      w[i] = 0;
      for (size_t j = 0; j < 8; ++j) { w[i] |= (uint64_t)data[i * 8 + j] << ((7 - j) * 8); }
    }

    auto rr = [](uint64_t x, size_t c) -> uint64_t { return (x >> c) | (x << (64 - c)); };

    for (size_t i = 16; i < 80; ++i) {
      uint64_t s0 = rr(w[i - 15], 1) ^ rr(w[i - 15], 8) ^ (w[i - 15] >> 7);
      uint64_t s1 = rr(w[i - 2], 19) ^ rr(w[i - 2], 61) ^ (w[i - 2] >> 6);
      w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    static const uint64_t k[80] = {
      0x428a2f98d728ae22ULL, 0x7137449123ef65cdULL, 0xb5c0fbcfec4d3b2fULL, 0xe9b5dba58189dbbcULL,
      0x3956c25bf348b538ULL, 0x59f111f1b605d019ULL, 0x923f82a4af194f9bULL, 0xab1c5ed5da6d8118ULL,
      0xd807aa98a3030242ULL, 0x12835b0145706fbeULL, 0x243185be4ee4b28cULL, 0x550c7dc3d5ffb4e2ULL,
      0x72be5d74f27b896fULL, 0x80deb1fe3b1696b1ULL, 0x9bdc06a725c71235ULL, 0xc19bf174cf692694ULL,
      0xe49b69c19ef14ad2ULL, 0xefbe4786384f25e3ULL, 0x0fc19dc68b8cd5b5ULL, 0x240ca1cc77ac9c65ULL,
      0x2de92c6f592b0275ULL, 0x4a7484aa6ea6e483ULL, 0x5cb0a9dcbd41fbd4ULL, 0x76f988da831153b5ULL,
      0x983e5152ee66dfabULL, 0xa831c66d2db43210ULL, 0xb00327c898fb213fULL, 0xbf597fc7beef0ee4ULL,
      0xc6e00bf33da88fc2ULL, 0xd5a79147930aa725ULL, 0x06ca6351e003826fULL, 0x142929670a0e6e70ULL,
      0x27b70a8546d22ffcULL, 0x2e1b21385c26c926ULL, 0x4d2c6dfc5ac42aedULL, 0x53380d139d95b3dfULL,
      0x650a73548baf63deULL, 0x766a0abb3c77b2a8ULL, 0x81c2c92e47edaee6ULL, 0x92722c851482353bULL,
      0xa2bfe8a14cf10364ULL, 0xa81a664bbc423001ULL, 0xc24b8b70d0f89791ULL, 0xc76c51a30654be30ULL,
      0xd192e819d6ef5218ULL, 0xd69906245565a910ULL, 0xf40e35855771202aULL, 0x106aa07032bbd1b8ULL,
      0x19a4c116b8d2d0c8ULL, 0x1e376c085141ab53ULL, 0x2748774cdf8eeb99ULL, 0x34b0bcb5e19b48a8ULL,
      0x391c0cb3c5c95a63ULL, 0x4ed8aa4ae3418acbULL, 0x5b9cca4f7763e373ULL, 0x682e6ff3d6b2b8a3ULL,
      0x748f82ee5defb2fcULL, 0x78a5636f43172f60ULL, 0x84c87814a1f0ab72ULL, 0x8cc702081a6439ecULL,
      0x90befffa23631e28ULL, 0xa4506cebde82bde9ULL, 0xbef9a3f7b2c67915ULL, 0xc67178f2e372532bULL,
      0xca273eceea26619cULL, 0xd186b8c721c0c207ULL, 0xeada7dd6cde0eb1eULL, 0xf57d4f7fee6ed178ULL,
      0x06f067aa72176fbaULL, 0x0a637dc5a2c898a6ULL, 0x113f9804bef90daeULL, 0x1b710b35131c471bULL,
      0x28db77f523047d84ULL, 0x32caab7b40c72493ULL, 0x3c9ebe0a15c9bebcULL, 0x431d67c49c100d4cULL,
      0x4cc5d4becb3e42b6ULL, 0x597f299cfc657e2aULL, 0x5fcb6fab3ad6faecULL, 0x6c44198c4a475817ULL};

    uint64_t a = hash[0], b = hash[1], c = hash[2], d = hash[3];
    uint64_t e = hash[4], f = hash[5], g = hash[6], h = hash[7];

    for (size_t i = 0; i < 80; ++i) {
      uint64_t s1 = rr(e, 14) ^ rr(e, 18) ^ rr(e, 41);
      uint64_t ch = (e & f) ^ ((~e) & g);
      uint64_t temp1 = h + s1 + ch + k[i] + w[i];
      uint64_t s0 = rr(a, 28) ^ rr(a, 34) ^ rr(a, 39);
      uint64_t maj = (a & b) ^ (a & c) ^ (b & c);
      uint64_t temp2 = s0 + maj;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  /// Handler function that calculates a SHA384 digest, returning it as binary.
  void sha384bin(const char *input, size_t in_len, char *output) {
    return sha512bin(input, in_len, output, true);
  }

  /// Handler function that calculates a SHA512 digest, returning it as binary.
  void sha512bin(const char *input, size_t in_len, char *output) {
    return sha512bin(input, in_len, output, false);
  }

  /// Calculates a SHA512 digest, returning it as binary.
  /// Assumes output is big enough to contain 64 bytes of data.
  void sha512bin(const char *input, size_t in_len, char *output, bool is384) {
    // Initialize the hash, according to spec of either SHA-384 or SHA-512
    uint64_t hash[8];
    if (is384) {
      hash[0] = 0xcbbb9d5dc1059ed8ULL;
      hash[1] = 0x629a292a367cd507ULL;
      hash[2] = 0x9159015a3070dd17ULL;
      hash[3] = 0x152fecd8f70e5939ULL;
      hash[4] = 0x67332667ffc00b31ULL;
      hash[5] = 0x8eb44a8768581511ULL;
      hash[6] = 0xdb0c2e0d64f98fa7ULL;
      hash[7] = 0x47b5481dbefa4fa4ULL;
    } else {
      hash[0] = 0x6a09e667f3bcc908ULL;
      hash[1] = 0xbb67ae8584caa73bULL;
      hash[2] = 0x3c6ef372fe94f82bULL;
      hash[3] = 0xa54ff53a5f1d36f1ULL;
      hash[4] = 0x510e527fade682d1ULL;
      hash[5] = 0x9b05688c2b3e6c1fULL;
      hash[6] = 0x1f83d9abfb41bd6bULL;
      hash[7] = 0x5be0cd19137e2179ULL;
    }

    // Add as many whole blocks of 128 bytes as possible from the input, until < 128 are left.
    size_t offset = 0;
    while (offset + 128 <= in_len) {
      sha512_add128(hash, input + offset);
      offset += 128;
    }
    // Copy the remainder to a 64 byte buffer.
    char buffer[128] = {0};
    memcpy(buffer, input + offset, in_len - offset);
    offset = in_len - offset;

    // Append 0x80 and zero pad
    buffer[offset] = 0x80;
    memset(buffer + offset + 1, 0, 128 - offset - 1);

    if (offset > 111) {
      sha512_add128(hash, buffer);
      memset(buffer, 0, 128);
    }

    uint64_t low = in_len * 8ULL, high = 0;
    for (size_t i = 0; i < 8; ++i) buffer[112 + i] = (high >> (56 - i * 8)) & 0xff;
    for (size_t i = 0; i < 8; ++i) buffer[120 + i] = (low >> (56 - i * 8)) & 0xff;

    // Process final block
    sha512_add128(hash, buffer);

    // Output result as big-endian
    size_t out_bytes = is384 ? 6 : 8;
    for (int i = 0; i < out_bytes; i++) {
      for (int j = 0; j < 8; j++) { output[i * 8 + j] = (hash[i] >> (56 - j * 8)) & 0xff; }
    }
  }

  /// Generic delegating function for different SHA algorithms that return the binary
  void shabin(const char *input, size_t in_len, char *output, SHA alg) {
    switch (alg) {
      case SHA256: sha256bin(input, in_len, output); break;
      case SHA384: sha384bin(input, in_len, output); break;
      case SHA512: sha512bin(input, in_len, output); break;
    }
  }

  /// Generic function that computes the base64url-encoded HMAC digest for the given SHA algorithm
  std::string digest_hmac(const std::string & msg, const JSON::Value & _key, SHA alg) {
    char binHash[64] = {0};
    std::string key = Encodings::Base64::decode(_key["k"].asStringRef());
    hmac_shabin(msg.c_str(), msg.size(), key.c_str(), key.size(), binHash, alg);
    return Encodings::Base64::encode(std::string(binHash, alg / 8), true);
  }

  /// Performs HMAC on msg with given key.
  /// Uses given hasher function, requires hashSize to be set accordingly.
  /// Output is returned as hexadecimal alphanumeric string.
  /// The hasher function must be the "bin" version of the hasher to have a compatible function signature.
  std::string hmac(std::string msg, std::string key, size_t hashSize, void hasher(const char *, size_t, char *), size_t blockSize) {
    return hmac(msg.data(), msg.size(), key.data(), key.size(), hashSize, hasher, blockSize);
  }

  /// Performs HMAC on msg with given key.
  /// Uses given hasher function, requires hashSize to be set accordingly.
  /// Output is returned as hexadecimal alphanumeric string.
  /// The hasher function must be the "bin" version of the hasher to have a compatible function signature.
  std::string hmac(const char *msg, size_t msg_len, const char *key, size_t key_len, size_t hashSize,
                   void hasher(const char *, size_t, char *), size_t blockSize) {
    char output[hashSize];
    hmacbin(msg, msg_len, key, key_len, hashSize, hasher, blockSize, output);
    std::stringstream outStr;
    for (size_t i = 0; i < hashSize; ++i) {
      outStr << std::hex << std::setw(2) << std::setfill('0') << (size_t)(output[i] & 0xff);
    }
    return outStr.str();
  }

  /// Performs HMAC on msg with given key.
  /// Uses given hasher function, requires hashSize to be set accordingly.
  /// Output is written in binary form to output, and assumes hashSize bytes are available to be written to.
  /// The hasher function must be the "bin" version of the hasher to have a compatible function signature.
  void hmacbin(const char *msg, size_t msg_len, const char *key, size_t key_len, size_t hashSize,
               void hasher(const char *, size_t, char *), size_t blockSize, char *output) {
    char key_data[blockSize]; // holds key as used in HMAC algorithm
    if (key_len > blockSize){
      // If the key given is too big, hash it.
      hasher(key, key_len, key_data);
      memset(key_data + hashSize, 0, blockSize - hashSize);
    }else{
      // Otherwise, use as-is, zero-padded if too small.
      memcpy(key_data, key, key_len);
      memset(key_data + key_len, 0, blockSize - key_len);
    }
    // key_data now contains hashSize bytes of key data, treated as per spec.
    char inner[blockSize + msg_len];  // holds data for inner hash
    char outer[blockSize + hashSize]; // holds data for outer hash
    for (size_t i = 0; i < blockSize; ++i) {
      inner[i] = key_data[i] ^ 0x36;
      outer[i] = key_data[i] ^ 0x5c;
    }
    // Copy the message to the inner hash data buffer
    memcpy(inner + blockSize, msg, msg_len);
    // Calculate the inner hash
    hasher(inner, blockSize + msg_len, outer + blockSize);
    // Calculate the outer hash
    hasher(outer, blockSize + hashSize, output);
  }

  /// Generic convenience function that returns the hexadecimal alphanumeric HMAC of msg and key
  std::string hmac_sha(std::string msg, std::string key, SHA alg) {
    return hmac_sha(msg.data(), msg.size(), key.data(), key.size(), alg);
  }

  /// Generic convenience function that returns the hexadecimal alphanumeric HMAC of msg and key
  std::string hmac_sha(const char *msg, size_t msg_len, const char *key, size_t key_len, SHA alg) {
    switch (alg) {
      case SHA256: return hmac(msg, msg_len, key, key_len, 32, sha256bin, 64);
      case SHA384: return hmac(msg, msg_len, key, key_len, 48, sha384bin, 128);
      case SHA512: return hmac(msg, msg_len, key, key_len, 64, sha512bin, 128);
      default: return "";
    }
  }

  /// Generic convenience function that sets output to the HMAC-SHA256/384/512 of msg and key in binary format.
  /// Assumes at least 32/48/64 bytes are available for writing in output.
  void hmac_shabin(const char *msg, size_t msg_len, const char *key, size_t key_len, char *output, SHA alg) {
    switch (alg) {
      case SHA256: return hmacbin(msg, msg_len, key, key_len, 32, sha256bin, 64, output);
      case SHA384: return hmacbin(msg, msg_len, key, key_len, 48, sha384bin, 128, output);
      case SHA512: return hmacbin(msg, msg_len, key, key_len, 64, sha512bin, 128, output);
    }
  }
}// namespace Secure
