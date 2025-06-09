#ifndef INCL_INFLATE
#define INCL_INFLATE

// you probably want:
// byte_buffer do_inflate(byte_buffer * input_bytes, int * error, uint8_t header_mode)

#include "buffers.h"

#include <stdint.h> // basic types
#include <stdio.h> // printf
#include <stdlib.h> // size_t

#define ASSERT_OR_BROKEN_FILE(expr, ret)              \
  {                                                   \
    if (!(expr)) {                                    \
      *error = -1;                                    \
      printf("assert failed on line %d\n", __LINE__); \
      return ret;                                     \
    }                                                 \
  }
#define ASSERT_OR_BROKEN_DECODER(expr, ret)           \
  {                                                   \
    if (!(expr)) {                                    \
      *error = 1;                                     \
      printf("assert failed on line %d\n", __LINE__); \
      return ret;                                     \
    }                                                 \
  }

static uint32_t infl_compute_adler32(const uint8_t *data, size_t size) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < size; i += 1) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

static uint32_t infl_compute_crc32(const uint8_t *data, size_t size, uint32_t init) {
  uint32_t crc_table[256] = {0};

  for (size_t i = 0; i < 256; i += 1) {
    uint32_t c = i;
    for (size_t j = 0; j < 8; j += 1) c = (c >> 1) ^ ((c & 1) ? 0xEDB88320 : 0);
    crc_table[i] = c;
  }

  init ^= 0xFFFFFFFF;
  for (size_t i = 0; i < size; i += 1) init = crc_table[(init ^ data[i]) & 0xFF] ^ (init >> 8);

  return init ^ 0xFFFFFFFF;
}

static void build_code(uint8_t *code_lens, uint16_t *code_lits, uint16_t *code_by_len, size_t total_count, int *error) {
  uint16_t min = 0;
  uint16_t len_count[15] = {0};
  for (size_t val = 0; val < total_count; val += 1) {
    uint8_t len = code_lens[val];
    if (len) len_count[len]++;
  }

  for (size_t i = 0; i < 15; i += 1) {
    uint16_t old_min = min;
    min += min + (len_count[i] << 1); // not a typo
    if (min < old_min) min = 0xFFFF;
    // printf("giving count cap value %d to code length %d (length %d has count %d)\n", min, i+1, i, len_count[i]);
    ASSERT_OR_BROKEN_FILE(min <= (1 << (i + 1)), )
    code_by_len[i + 1] = min;
  }
  for (uint16_t val = 0; val < total_count; val += 1) {
    uint8_t len = code_lens[val];
    if (len) {
      uint16_t code = code_by_len[len]++;
      // printf("assigning code %02X to value %d\n", code, val);
      code_lits[code] = val;
      // printf("reading: code %04X (len %d) has symbol %d\n", code, len, val);
    }
  }
}
static uint16_t read_huff_code(bit_buffer *input, uint16_t *code_by_len, int *error) {
  uint8_t code_len = 1;
  uint16_t code = bit_pop(input);
  while (code_len < 16 && code >= code_by_len[code_len]) {
    code = (code << 1) | bit_pop(input);
    code_len += 1;
  }
  // printf("read a code (%d) with length %d\n", code, code_len);
  ASSERT_OR_BROKEN_FILE(code != 0 || code_len < 16, code)
  ASSERT_OR_BROKEN_FILE(code < (1 << 15), code)
  return code;
}

static void do_lz77(bit_buffer *input, byte_buffer *ret, uint16_t *code_lits, uint16_t *code_by_len,
                    uint16_t *dist_code_lits, uint16_t *dist_code_by_len, int *error) {
  int huff_error = 0;
  uint16_t literal = 256;
  do {
    uint16_t lit_code = read_huff_code(input, code_by_len, &huff_error);
    ASSERT_OR_BROKEN_FILE(huff_error == 0, )
    literal = code_lits[lit_code];
    ASSERT_OR_BROKEN_FILE(literal <= 285, )

    if (literal < 256) {
      // printf("literal %d\n", literal);
      byte_push(ret, literal);
    } else if (literal > 256) {
      uint8_t len_extra_bits = 0;
      if (literal >= 261 && literal < 285) len_extra_bits = (literal - 261) / 4;
      uint16_t len_mins[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                               31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
      uint16_t len = len_mins[literal - 257] + bits_pop(input, len_extra_bits);

      uint16_t dist_code = read_huff_code(input, dist_code_by_len, &huff_error);
      ASSERT_OR_BROKEN_FILE(huff_error == 0, )
      uint16_t dist_literal = dist_code_lits[dist_code];
      ASSERT_OR_BROKEN_FILE(dist_literal <= 29, )

      uint8_t dist_extra_bits = 0;
      if (dist_literal >= 2) dist_extra_bits = (dist_literal - 2) / 2;
      uint16_t dist_mins[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
      uint16_t dist = dist_mins[dist_literal] + bits_pop(input, dist_extra_bits);
      ASSERT_OR_BROKEN_FILE(dist <= ret->len, )

      // printf("lz77 len %d dist %d\n", len, dist);

      for (size_t j = 0; j < len; j++) byte_push(ret, ret->data[ret->len - dist]);
    } else {
      // printf("end of stream at 0x%02llX\n", input->byte_index);
      break;
    }

    if (input->byte_index == input->buffer.len && input->bit_index != 0) ASSERT_OR_BROKEN_FILE(0, )
  } while (literal != 256);
  // puts("block ended!");
}

// decompression starts at input_bytes->cur
// on error, error is set to nonzero. otherwise error is unset
// positive error: bug in decoder
// negative error: broken DEFLATE data
// returns any decompressed data even on error
// on success, sets input_bytes->cur field to where the decompressor stopped decompressing
static byte_buffer do_inflate(byte_buffer *input_bytes, int *error, uint8_t header_mode) {
  byte_buffer ret = {0, 0, 0, 0};
  bit_buffer input = {*input_bytes, input_bytes->cur, 0};

  uint16_t static_lits[1 << 9];
  memset(static_lits, 0, sizeof(uint16_t) * (1 << 9));
  for (uint16_t i = 0; i < 24; i++) static_lits[i] = i + 256;
  for (uint16_t i = 0; i < 144; i++) static_lits[i + 48] = i;
  for (uint16_t i = 0; i < 8; i++) static_lits[i + 192] = i + 280;
  for (uint16_t i = 0; i < 112; i++) static_lits[i + 400] = i + 144;

  uint16_t static_by_len[16];
  memset(static_by_len, 0, sizeof(uint16_t) * 16);
  static_by_len[7] = 0x18;
  static_by_len[8] = 0xC8;
  static_by_len[9] = 0xFFFF;

  uint16_t static_dists[1 << 5];
  memset(static_dists, 0, sizeof(uint16_t) * (1 << 5));
  for (uint16_t i = 0; i < 32; i++) static_dists[i] = i;

  uint16_t static_dists_by_len[16];
  memset(static_dists_by_len, 0, sizeof(uint16_t) * 16);
  static_dists_by_len[5] = 0xFFFF;

  if (header_mode == 1 || header_mode == 10) {
    uint16_t info = bits_pop(&input, 16);
    uint16_t check = byteswap_int(info, 2);
    ASSERT_OR_BROKEN_FILE(check / 31 * 31 == check, ret) // check value
    uint8_t cmf = info & 0xF;
    uint8_t flg = info >> 8;
    ASSERT_OR_BROKEN_FILE((cmf & 0xF) == 8, ret) // deflate
    ASSERT_OR_BROKEN_FILE((flg & 0x20) == 0, ret) // FDICT flag; dictionaries are not supported
  } else if (header_mode == 2 || header_mode == 20) {
    ASSERT_OR_BROKEN_FILE(bits_pop(&input, 8) == 0x1F, ret) // magic
    ASSERT_OR_BROKEN_FILE(bits_pop(&input, 8) == 0x8B, ret) // magic
    ASSERT_OR_BROKEN_FILE(bits_pop(&input, 8) == 0x08, ret) // deflate

    uint8_t flg = bits_pop(&input, 8); // flags
    // uint8_t ftext = flg & 1; // file is ascii text - unused
    uint8_t fcrc = !!(flg & 2); // header CRC is present
    uint8_t fextra = !!(flg & 4); // extra sections are present
    uint8_t fname = !!(flg & 8); // original filename is present
    uint8_t fcomment = !!(flg & 16); // comment is present

    ASSERT_OR_BROKEN_FILE((fcomment >> 5) == 0, ret) // reserved bits, must be zero

    bits_pop(&input, 32); // modification time, unused

    bits_pop(&input, 8); // extra flags, unused (indicates compression strength)
    bits_pop(&input, 8); // OS, unused

    if (fextra) {
      uint16_t xlen = bits_pop(&input, 16);
      // extra field is not used
      for (size_t i = 0; i < xlen; i += 1) bits_pop(&input, 8);
    }
    while (fname && bits_pop(&input, 8) != 0) {}
    while (fcomment && bits_pop(&input, 8) != 0) {}
    if (fcrc) {
      uint16_t crc = infl_compute_crc32(input.buffer.data, input.byte_index, 0) & 0xFFFF;
      uint16_t expected_crc = bits_pop(&input, 16);
      ASSERT_OR_BROKEN_FILE(crc == expected_crc, ret)
    }
  }

  while (1) {
    // printf("-- starting a block at %08X:%d\n", input.byte_index, input.bit_index);
    uint8_t final = bit_pop(&input);
    uint8_t type = bits_pop(&input, 2);
    // if (final)
    //     printf("-- it's final!!!\n");
    if (type == 0) {
      bits_align_to_byte(&input);
      // printf("-- literal addr %08llX\n", (unsigned long long)input.byte_index);
      uint16_t len = bits_pop(&input, 16);
      uint16_t nlen = bits_pop(&input, 16);

      // printf("literal len: %d\n", len);
      ASSERT_OR_BROKEN_FILE(len == (uint16_t)~nlen, ret)
      ASSERT_OR_BROKEN_FILE(input.byte_index + 1 + len <= input.buffer.len, ret)

      bytes_push(&ret, &input.buffer.data[input.byte_index + 1], len);
      input.byte_index += len;
    } else if (type == 1) {
      int lz77_error = 0;
      do_lz77(&input, &ret, static_lits, static_by_len, static_dists, static_dists_by_len, &lz77_error);
      ASSERT_OR_BROKEN_FILE(lz77_error == 0, ret)
    } else if (type == 2) {
      uint16_t len_count = bits_pop(&input, 5) + 257;
      uint8_t dist_count = bits_pop(&input, 5) + 1;
      uint8_t codelen_count = bits_pop(&input, 4) + 4;

      uint8_t inst_code_vals[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      uint8_t inst_code_lens[19] = {0};
      uint16_t inst_code_lits[1 << 7] = {0};
      uint16_t inst_code_by_len[16] = {0};

      for (uint16_t i = 0; i < codelen_count; i += 1) {
        uint8_t len = bits_pop(&input, 3);
        inst_code_lens[inst_code_vals[i]] = len;
      }

      int code_error = 0;
      // puts("building meta code");
      build_code(inst_code_lens, inst_code_lits, inst_code_by_len, 19, &code_error);
      ASSERT_OR_BROKEN_FILE(code_error == 0, ret)

      int huff_error = 0;
      // parse compressed code lengths
      uint8_t raw_code_lens[288 + 32] = {0};
      for (size_t i = 0; i < len_count + dist_count; i += 1) {
        uint16_t inst_code = read_huff_code(&input, inst_code_by_len, &huff_error);
        ASSERT_OR_BROKEN_FILE(huff_error == 0, ret)
        uint16_t inst = inst_code_lits[inst_code];
        // printf("\t\t\t\t\t(for value %d)\n", i < 288 ? i : i - 288);

        if (inst < 16)
          raw_code_lens[i] = inst;
        else if (inst == 16) {
          ASSERT_OR_BROKEN_FILE(i > 0, ret)
          uint8_t count = bits_pop(&input, 2) + 3;
          ASSERT_OR_BROKEN_FILE(i + count <= 288 + 32, ret)
          for (size_t j = i; j < i + count; j += 1) raw_code_lens[j] = raw_code_lens[i - 1];
          i += count - 1;
        } else if (inst == 17 || inst == 18) {
          uint8_t count = inst == 18 ? bits_pop(&input, 7) + 11 : bits_pop(&input, 3) + 3;
          ASSERT_OR_BROKEN_FILE(i + count <= 288 + 32, ret)
          for (size_t j = i; j < i + count; j += 1) raw_code_lens[j] = 0;
          i += count - 1;
        } else
          ASSERT_OR_BROKEN_FILE(0, ret)
      }

      uint8_t code_lens[288] = {0};
      uint16_t code_lits[1 << 15] = {0};
      uint16_t code_by_len[16] = {0};
      memcpy(code_lens, raw_code_lens, len_count);
      // printf("building lit code, count %d\n", len_count);
      build_code(code_lens, code_lits, code_by_len, 288, &code_error);
      ASSERT_OR_BROKEN_FILE(code_error == 0, ret)

      uint8_t dist_code_lens[32] = {0};
      uint16_t dist_code_lits[1 << 15] = {0};
      uint16_t dist_code_by_len[16] = {0};
      memcpy(dist_code_lens, &raw_code_lens[len_count], dist_count);
      build_code(dist_code_lens, dist_code_lits, dist_code_by_len, 32, &code_error);
      ASSERT_OR_BROKEN_FILE(code_error == 0, ret)

      // printf("-- finished reading huff at %08X:%d\n", input.byte_index, input.bit_index);

      int lz77_error = 0;
      do_lz77(&input, &ret, code_lits, code_by_len, dist_code_lits, dist_code_by_len, &lz77_error);
      ASSERT_OR_BROKEN_FILE(lz77_error == 0, ret)
    } else
      ASSERT_OR_BROKEN_FILE(0, ret)

    // if we tried to read past the end of the input
    if (input.byte_index > input.buffer.len)
      ASSERT_OR_BROKEN_FILE(0, ret)
    else if (input.byte_index == input.buffer.len && input.bit_index != 0)
      ASSERT_OR_BROKEN_FILE(0, ret)

    if (final) break;
  }
  if (header_mode == 1) {
    bits_align_to_byte(&input);
    // printf("-- literal addr %08llX\n", (unsigned long long)input.byte_index);
    uint32_t expected_checksum = byteswap_int(bits_pop(&input, 32), 4);
    uint32_t checksum = infl_compute_adler32(ret.data, ret.len);
    ASSERT_OR_BROKEN_FILE(expected_checksum == checksum, ret)
  } else if (header_mode == 2) {
    uint32_t crc = infl_compute_crc32(ret.data, ret.len, 0);
    uint32_t expected_crc = bits_pop(&input, 32);
    ASSERT_OR_BROKEN_FILE(crc == expected_crc, ret)
    uint32_t expected_size = bits_pop(&input, 32);
    uint32_t size = ret.len & 0xFFFFFFFF;
    ASSERT_OR_BROKEN_FILE(expected_size == size, ret)
  }

  input_bytes->cur = input.byte_index + (input.bit_index != 0);

  return ret;
}

#undef ASSERT_OR_BROKEN_FILE
#undef ASSERT_OR_BROKEN_DECODER

#endif // INCL_INFLATE
