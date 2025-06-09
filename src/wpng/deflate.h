#ifndef INCL_DEFLATE
#define INCL_DEFLATE

// you probably want:
// static bit_buffer do_deflate(const uint8_t * input, uint64_t input_len, int8_t quality_level, uint8_t header_mode)

#include "buffers.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// must return a buffer with at least 8-byte alignment
#ifndef DEFL_REALLOC
#define DEFL_REALLOC realloc
#endif

// must return a buffer with at least 8-byte alignment
#ifndef DEFL_MALLOC
#define DEFL_MALLOC malloc
#endif

#ifndef DEFL_FREE
#define DEFL_FREE free
#endif

// must be at least 3
static const size_t lz77_min_lookback_length = 4;

#ifndef DEFL_LOW_MEMORY
#define DEFL_HASH_SIZE (20) // 1m
#define DEFL_PREVLINK_SIZE (16)
#else
#ifndef DEFL_ULTRA_LOW_MEMORY
#define DEFL_HASH_SIZE (18) // ~256k
#define DEFL_PREVLINK_SIZE (16)
#else
#define DEFL_HASH_SIZE (15) // ~32k
#define DEFL_PREVLINK_SIZE (15)
#endif
#endif

// for finding lookback matches, we use a chained hash table with limited, location-based chaining
typedef struct {
    uint32_t *hashtable;
    uint32_t *prevlink;
    uint32_t max_distance;
    uint16_t chain_len;
} defl_hashmap;

const size_t defl_prevlink_mask = ((1 << DEFL_PREVLINK_SIZE) - 1);

#define DEFL_HASH_LENGTH ((lz77_min_lookback_length) < 4 ? (lz77_min_lookback_length) : 4)

static inline uint32_t hashmap_hash_raw(const void *bytes) {
  // hashing function (can be anything; go ahead and optimize it as long as it doesn't result in tons of collisions)
  uint32_t temp = 0xA68BB0D5;
  // unaligned-safe 32-bit load
  uint32_t a = 0;
  memcpy(&a, bytes, DEFL_HASH_LENGTH);
  // then just multiply it by the const and return the top N bits
  return a * temp;
}
static inline uint32_t hashmap_hash(const void *bytes) {
  return hashmap_hash_raw(bytes) >> (32 - DEFL_HASH_SIZE);
}
static inline uint32_t defl_hashlink_index(uint64_t value) {
  return value & defl_prevlink_mask;
}

// bytes must point to four characters
static inline void hashmap_insert(defl_hashmap *hashmap, const uint8_t *bytes, uint64_t value) {
  const uint32_t key = hashmap_hash(bytes);
  hashmap->prevlink[defl_hashlink_index(value)] = hashmap->hashtable[key];
  hashmap->hashtable[key] = value;
}

// bytes must point to four characters and be inside of buffer
static inline uint64_t hashmap_get(defl_hashmap *hashmap, size_t i, const uint8_t *input, const size_t buffer_len,
                                   const size_t pre_context, uint64_t *min_len, size_t *back_distance) {
  uint64_t remaining = buffer_len - i;
  if (i >= buffer_len || remaining <= DEFL_HASH_SIZE) return -1;

  const uint32_t key = hashmap_hash(&input[i]);
  uint64_t value = hashmap->hashtable[key];
  // file might be more than 4gb, so map in the upper bits of the current address
  if (sizeof(size_t) > sizeof(uint32_t)) value |= i & 0xFFFFFFFF00000000;
  if (!value) return -1;

  // if we hit 128 bytes we call it good enough and take it
  const uint64_t good_enough_length = 128;

  // look for best match under key
  uint64_t best = -1;
  uint64_t best_size = lz77_min_lookback_length - 1;
  uint64_t best_d = 0;
  uint64_t first_value = value;
  uint16_t chain_len = hashmap->chain_len;
  while (chain_len-- > 0) {
    if (i - value > hashmap->max_distance) break;
    if (memcmp(&input[i], &input[value], lz77_min_lookback_length) == 0 && input[i + best_size] == input[value + best_size]) {
      uint64_t size = 0;

      size_t d = 1;
      while (value > 0 && input[i - d] == input[value - 1] && d <= pre_context && d < 200) {
        value -= 1;
        remaining += 1;
        d += 1;
      }
      d -= 1;

      while (size + d < 258 && size < remaining && input[i + size] == input[value + d + size]) size += 1;

      if (size > 258 - d) size = 258 - d;

      // bad heuristic for "is it worth it?"
      if (size > best_size) {
        best_size = size;
        best = value;
        best_d = d;
        // get out if we're being expensive
        if (size >= good_enough_length || size >= remaining) break;
      }
    }
    value = hashmap->prevlink[defl_hashlink_index(value)];
    if (sizeof(size_t) > sizeof(uint32_t)) value |= i & 0xFFFFFFFF00000000;

    if (value == 0 || value > i || value == first_value) break;
    const uint32_t key_2 = hashmap_hash(&input[value]);
    if (key_2 != key) break;
  }

  if (best_size != lz77_min_lookback_length - 1) *min_len = best_size;
  *back_distance = best_d;
  return best;
}

typedef struct _huff_node {
    struct _huff_node *children[2];
    int64_t freq;
    // We length-limit our codes to 15 bits, so storing them in a u16 is fine.
    uint16_t code;
    uint8_t code_len;
    uint16_t symbol;
} huff_node_t;

static huff_node_t *alloc_huff_node(void) {
  return (huff_node_t *)DEFL_MALLOC(sizeof(huff_node_t));
}

static void free_huff_nodes(huff_node_t *node) {
  if (node->children[0]) free_huff_nodes(node->children[0]);
  if (node->children[1]) free_huff_nodes(node->children[1]);
  DEFL_FREE(node);
}

static void push_code(huff_node_t *node, uint8_t bit) {
  // node->code |= (bit & 1) << node->code_len;
  node->code <<= 1;
  node->code |= bit & 1;
  node->code_len += 1;

  if (node->children[0]) push_code(node->children[0], bit);
  if (node->children[1]) push_code(node->children[1], bit);
}

static int count_compare(const void *a, const void *b) {
  int64_t n = *((int64_t *)b) - *((int64_t *)a);
  return n > 0 ? 1 : n < 0 ? -1 : 0;
}

static int huff_len_compare(const void *a, const void *b) {
  int64_t len_a = (*(huff_node_t **)a)->code_len;
  int64_t len_b = (*(huff_node_t **)b)->code_len;
  if (len_a < len_b)
    return -1;
  else if (len_a > len_b)
    return 1;
  uint16_t part_a = (*(huff_node_t **)a)->symbol;
  uint16_t part_b = (*(huff_node_t **)b)->symbol;
  if (part_a < part_b)
    return -1;
  else if (part_a > part_b)
    return 1;
  return 0;
}

uint64_t bitswap(uint64_t bits, uint8_t len) {
  for (size_t b = 0; b < len / 2; b++) {
    size_t b2 = len - b - 1;
    uint64_t diff = (!((bits >> b) & 1)) != (!((bits >> b2) & 1));
    diff = (diff << b) | (diff << b2);
    bits ^= diff;
  }
  return bits;
}
size_t gen_canonical_code(uint64_t *counts, huff_node_t **unordered_dict, huff_node_t **dict,
                          huff_node_t **root_to_free, size_t capacity) {
  // printf("capacity is %d\n", capacity);

  // we stuff the byte identity into the bottom N bits
  uint64_t symbol_bits = 9;

  size_t symbol_count = 0;
  uint64_t total_count = 0;
  for (size_t b = 0; b < capacity; b++) {
    // printf("count of %d is %d\n", b, counts[b]);
    if (counts[b]) symbol_count += 1;
    total_count += counts[b];
    counts[b] = (counts[b] << symbol_bits) | b;
  }

  qsort(counts, capacity, sizeof(uint64_t), count_compare);

  // we want to generate a length-limited code with a maximum of 15 bits...
  // ... which means that the minimum frequency must be at least 1/(1<<14) of the total count
  // (we give ourselves 1 bit of leniency because otherwise it doesn't work)
  if (symbol_count > 0) {
    const uint64_t n = 1 << 14;
    // use ceiled division to make super extra sure that we don't go over 1/16k
    uint64_t min_ok_count = (total_count + n - 1) / n;
    while ((counts[symbol_count - 1] >> symbol_bits) < min_ok_count) {
      for (int i = symbol_count - 1; i >= 0; i -= 1) {
        // We use an x = max(minimum, x) approach instead of just adding to every count, because
        //  if we never add to the most frequent item's frequency, we will definitely converge.
        // (Specifically, this is guaranteed to converge if there are 16k or less symbols in
        //  the dictionary, which is true.)
        // More proof of convergence: We will eventually add less than 16k to "total_count"
        //  two `while` iterations in a row, which will cause min_ok_count to stop changing.
        if (counts[i] >> symbol_bits < min_ok_count) {
          // printf("freq of %d is too low (%d)...", i, (counts[i] >> symbol_bits));
          uint64_t diff = min_ok_count - (counts[i] >> symbol_bits);
          counts[i] += diff << symbol_bits;
          // printf(" increased to %d\n", counts[i] >> symbol_bits);
          total_count += diff;
        } else
          break;
      }
      min_ok_count = (total_count + n - 1) / n;
    }
  }

  // set up raw huff nodes
  for (size_t i = 0; i < capacity; i += 1) {
    unordered_dict[i] = alloc_huff_node();
    assert(unordered_dict[i]);
    unordered_dict[i]->symbol = counts[i] & ((1 << symbol_bits) - 1);
    unordered_dict[i]->code = 0;
    unordered_dict[i]->code_len = 0;
    unordered_dict[i]->freq = counts[i] >> symbol_bits;
    unordered_dict[i]->children[0] = 0;
    unordered_dict[i]->children[1] = 0;
  }

  // set up byte name -> huff node dict
  for (size_t i = 0; i < capacity; i += 1) dict[unordered_dict[i]->symbol] = unordered_dict[i];

  // set up tree generation queues
  huff_node_t *queue[600];
  memset(queue, 0, sizeof(queue));

  size_t queue_count = capacity;
  // printf("queue count is %d\n", queue_count);

  for (size_t i = 0; i < capacity; i += 1) queue[i] = unordered_dict[i];

  // remove zero-frequency items from the input queue
  while (queue_count > 0 && queue[queue_count - 1]->freq == 0) {
    // printf("freq of %d is zero, deleting\n", queue[queue_count - 1]->symbol);
    dict[queue[queue_count - 1]->symbol] = 0;
    free_huff_nodes(queue[queue_count - 1]);
    queue_count -= 1;
  }

  uint8_t queue_needs_free = 0;
  // start pumping through the queues
  while (queue_count > 1) {
    queue_needs_free = 1;

    huff_node_t *lowest = queue[queue_count - 1];
    huff_node_t *next_lowest = queue[queue_count - 2];

    queue_count -= 2;

    assert(lowest && next_lowest);

    // make new node
    huff_node_t *new_node = alloc_huff_node();
    assert(new_node);
    new_node->symbol = 0;
    new_node->code = 0;
    new_node->code_len = 0;
    new_node->freq = lowest->freq + next_lowest->freq;
    new_node->children[0] = next_lowest;
    new_node->children[1] = lowest;

    push_code(new_node->children[0], 0);
    push_code(new_node->children[1], 1);

    // insert new element at end of array, then bubble it down to the correct place
    queue[queue_count] = new_node;
    queue_count += 1;
    assert(queue_count <= 600);
    for (size_t i = queue_count - 1; i > 0; i -= 1) {
      if (queue[i]->freq >= queue[i - 1]->freq) {
        huff_node_t *temp = queue[i];
        queue[i] = queue[i - 1];
        queue[i - 1] = temp;
      }
    }
  }

  // With the above done, our basic huffman tree is built. Now we need to canonicalize it.
  // Canonicalization algorithms only work on sorted lists. Because of frequency ties, our
  //  code list might not be sorted by code length. Let's fix that by sorting it first.

  if (symbol_count >= 2) qsort(unordered_dict, symbol_count, sizeof(huff_node_t *), huff_len_compare);

  // If we only have one symbol, we need to ensure that it thinks it has a code length of exactly 1.
  if (symbol_count == 1) unordered_dict[0]->code_len = 1;

  // Now we ACTUALLY canonicalize the huffman code list.

  uint64_t canon_code = 0;
  uint64_t canon_len = 0;
  uint16_t codes_per_len[300] = {0};
  for (size_t i = 0; i < symbol_count; i += 1) {
    if (canon_code == 0) {
      canon_len = unordered_dict[i]->code_len;
      codes_per_len[canon_len] += 1;
      unordered_dict[i]->code = 0;
      canon_code += 1;
      continue;
    }
    if (unordered_dict[i]->code_len > canon_len) canon_code <<= unordered_dict[i]->code_len - canon_len;

    canon_len = unordered_dict[i]->code_len;
    codes_per_len[canon_len] += 1;
    uint64_t code = canon_code;

    code = bitswap(code, canon_len);
    unordered_dict[i]->code = code;

    canon_code += 1;
  }

  // despite all we've done to them, our huffman tree nodes still have their child pointers intact
  // so we can recursively free all our nodes all at once
  if (queue_needs_free) *root_to_free = queue[0];
  // if we only have 0 or 1 nodes, then the queue doesn't run, so we need to free them directly
  // (only if there are actually any nodes, though)
  else if (symbol_count == 1)
    *root_to_free = unordered_dict[0];

  return symbol_count;
}

static void huff_write_code_desc(bit_buffer *ret, huff_node_t **dict, huff_node_t **dist_dict) {
  uint32_t len_count = 286;
  while (len_count > 257) {
    if (dict[len_count - 1] && dict[len_count - 1]->code_len) break;
    len_count -= 1;
  }
  uint32_t dist_count = 30;
  while (dist_count > 1) {
    if (dist_dict[dist_count - 1] && dist_dict[dist_count - 1]->code_len) break;
    dist_count -= 1;
  }

  uint8_t lens[316] = {0};
  for (size_t i = 0; i < len_count; i += 1) lens[i] = dict[i] ? dict[i]->code_len : 0;

  for (size_t i = 0; i < dist_count; i += 1) lens[i + len_count] = dist_dict[i] ? dist_dict[i]->code_len : 0;

  // for the sake of simplicity we don't bother building a perfectly compressed huff code description
  // instead, we only do RLE
  // as far as I can tell, basically only doing RLE only loses us a couple bytes
  bits_push(ret, len_count - 257, 5);
  bits_push(ret, dist_count - 1, 5);
  bits_push(ret, 15, 4); // 19 (add 4)

  // lengths of code compression codes...
  bits_push(ret, 7, 3); // 16 - copy/RLE (3-6 aka 4-7)
  bits_push(ret, 6, 3); // 17 - multi-zero short (3-10)
  bits_push(ret, 7, 3); // 18 - multi-zero long (11-138)
  for (size_t i = 0; i < 16; i++) // 0, 8, 7, 9, etc
    bits_push(ret, i == 0 ? 5 : 4, 3);

  for (size_t i = 0; i < len_count + dist_count; i += 1) {
    size_t same_count = 1;
    for (size_t j = i + 1; j < len_count + dist_count && same_count < (lens[i] == 0 ? 138 : 7); j += 1) {
      if (lens[j] == lens[i])
        same_count += 1;
      else
        break;
    }
    if (lens[i] == 0) {
      if (same_count >= 11) {
        // puts("doing long zero rle");
        bits_push(ret, bitswap(0x7F, 7), 7); // 18 - multi-zero long (11-138)
        bits_push(ret, same_count - 11, 7);
        i += same_count - 1;
      } else if (same_count >= 3) {
        // puts("doing short zero rle");
        bits_push(ret, bitswap(0x3E, 6), 6); // 17 - multi-zero short (3-10)
        bits_push(ret, same_count - 3, 3);
        i += same_count - 1;
      } else
        bits_push(ret, bitswap(0x1E, 5), 5);
    } else {
      if (same_count >= 4) {
        // puts("doing normal rle");

        bits_push(ret, bitswap(lens[i] - 1, 4), 4);

        bits_push(ret, bitswap(0x7E, 7), 7); // 16 - copy/RLE (3-6 aka 4-7)
        bits_push(ret, same_count - 4, 2);
        i += same_count - 1;
      } else
        bits_push(ret, bitswap(lens[i] - 1, 4), 4);
    }
    // printf("writing: code %04X (len %d) has symbol %d\n", dict[i] ? dict[i]->code : 0, dict[i] ? dict[i]->code_len : 0, i);
  }
}

static void len_get_info(size_t len, uint16_t *arg_code, uint16_t *arg_bit_count, uint16_t *bits) {
  uint16_t len_mins[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  assert(len >= len_mins[0]);
  uint16_t code = 285;
  for (size_t i = 0; i < 29; i += 1) {
    if (len < len_mins[i]) break;
    code = i + 257;
  }
  if (arg_code) *arg_code = code;
  if (bits) *bits = 0;
  if (arg_bit_count) *arg_bit_count = 0;
  if (len == 258 || len <= 10) return;

  code -= 257;
  uint16_t bit_count = (code - 4) / 4;
  if (arg_bit_count) *arg_bit_count = bit_count;
  uint16_t bit_data = len - len_mins[code];
  if (bits) {
    assert(bit_data < (1 << bit_count));
    *bits = bit_data;
  }
}

static void dist_get_info(size_t dist, uint16_t *arg_code, uint16_t *arg_bit_count, uint16_t *bits) {
  uint16_t dist_mins[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                            193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  assert(dist >= dist_mins[0]);
  uint16_t code = 29;
  for (size_t i = 0; i < 30; i += 1) {
    if (dist < dist_mins[i]) break;
    code = i;
  }
  if (arg_code) *arg_code = code;
  if (bits) *bits = 0;
  if (arg_bit_count) *arg_bit_count = 0;
  if (dist <= 4) return;

  uint16_t bit_count = code >= 4 ? (code - 2) / 2 : 0;
  if (arg_bit_count) *arg_bit_count = bit_count;
  uint16_t bit_data = dist - dist_mins[code];
  if (bits) {
    assert(bit_data < (1 << bit_count));
    *bits = bit_data;
  }
}

static uint32_t defl_compute_adler32(const uint8_t *data, size_t size) {
  uint32_t a = 1;
  uint32_t b = 0;
  for (size_t i = 0; i < size; i += 1) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

static uint32_t defl_compute_crc32(const uint8_t *data, size_t size, uint32_t init) {
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

// quality level: from -12 to 12, indicates compression quality. 0 means "store without compressing". the higher the quality, the slower.
static bit_buffer do_deflate(const uint8_t *input, uint64_t input_len, int8_t quality_level, uint8_t header_mode) {
  if (quality_level > 12) quality_level = 12;
  if (quality_level < -12) quality_level = -12;

  defl_hashmap hashmap;
  hashmap.hashtable = (uint32_t *)DEFL_MALLOC(sizeof(uint32_t) * (1 << DEFL_HASH_SIZE));
  assert(hashmap.hashtable);
  hashmap.prevlink = (uint32_t *)DEFL_MALLOC(sizeof(uint32_t) * (1 << DEFL_PREVLINK_SIZE));
  assert(hashmap.prevlink);
  memset(hashmap.hashtable, 0, sizeof(uint32_t) * (1 << DEFL_HASH_SIZE));
  memset(hashmap.prevlink, 0, sizeof(uint32_t) * (1 << DEFL_PREVLINK_SIZE));

  int8_t chain_bits = quality_level - 1 + (quality_level < 0);
  if (chain_bits < 0) chain_bits = 0;
  hashmap.chain_len = (1 << chain_bits);

  hashmap.max_distance = (1 << (quality_level + 11 + (quality_level < 0)));
  if (hashmap.max_distance > 32768) hashmap.max_distance = 32768;

  // set up buffers

  bit_buffer ret;
  memset(&ret, 0, sizeof(bit_buffer));

  // zlib
  if (header_mode == 1) {
    // standard deflate
    bits_push(&ret, 0x78, 8);
    // default compression strength
    bits_push(&ret, 0x9C, 8);
  }
  // gzip
  else if (header_mode >= 2) {
    // magic
    bits_push(&ret, 0x1F, 8);
    bits_push(&ret, 0x8B, 8);
    // deflate compression
    bits_push(&ret, 0x08, 8);
    // no flags (no filename, comment, header crc, etc)
    bits_push(&ret, 0x00, 8);
    // no timestamp
    bits_push(&ret, 0x00, 32);
    // fastest (4) compression or maximum (2) compression...???
    bits_push(&ret, quality_level == 0 ? 4 : 2, 8);
    // unknown origin filesystem
    bits_push(&ret, 0xFF, 8);
  }

  uint32_t checksum =
    header_mode ? header_mode == 1 ? defl_compute_adler32(input, input_len) : defl_compute_crc32(input, input_len, 0) : 0;

  uint64_t i = 0;
  // Split up into chunks, so that each chunk can have a more ideal huffman code.
  // The chunk size is arbitrary.
  // Each chunk is prefixed with a byte-aligned 32-bit integer giving the number of output tokens in the chunk.

  uint64_t chunk_max_commands = (1 << 15);
  uint64_t chunk_max_literal_count = (1 << 14);
  uint64_t chunk_max_source_count = (1 << 20);

  // commands have 4 numbers: size, pointer, lb_size, and distance
  uint64_t *commands = (uint64_t *)DEFL_MALLOC(sizeof(uint64_t) * chunk_max_commands * 4);
  assert(commands);
  size_t command_count = 0;

  // store only
  if (quality_level == 0) {
    while (i < input_len) {
      bit_push(&ret, 0); // not the final chunk
      bits_push(&ret, 0, 2); // uncompressed chunk
      bits_align_to_byte(&ret);
      size_t amount = input_len - i;
      if (amount > 0xFFFF) amount = 0xFFFF;
      bits_push(&ret, amount, 16);
      bits_push(&ret, ~amount, 16);

      bytes_push(&ret.buffer, &input[i], amount);
      ret.byte_index += amount;
      i += amount;
    }
  }
  while (i < input_len) {
    uint64_t lb_size = 0;
    uint64_t lb_loc = 0;

    memset(commands, 0, sizeof(uint64_t) * chunk_max_commands * 4);
    command_count = 0;

    uint64_t counts[288] = {0};
    uint64_t dist_counts[32] = {0};

    uint64_t literal_count = 0;
    size_t i_start = i;
    while (i < input_len && command_count < chunk_max_commands && literal_count < chunk_max_literal_count &&
           i - i_start < chunk_max_source_count) {
      // store a literal if we found no lookback
      uint64_t size = 0;
      while (i + size < input_len && size < 258) {
        size_t back_distance = 0;
        if (i + size + DEFL_HASH_LENGTH < input_len)
          lb_loc = hashmap_get(&hashmap, i + size, input, input_len, size, &lb_size, &back_distance);
        if (lb_size != 0) {
          // zlib-style "lazy" search: only confirm the match if the next byte isn't a good match too
          if (lb_size < 64 && i + size + 1 + DEFL_HASH_LENGTH < input_len && size + 1 < 258) {
            uint64_t lb_size_2 = 0;
            size_t back_distance_2 = 0;
            uint64_t lb_loc_2 = hashmap_get(&hashmap, i + size + 1, input, input_len, size + 1, &lb_size_2, &back_distance_2);
            if (lb_size_2 >= lb_size + 1) {
              size += 1;
              lb_loc = lb_loc_2;
              lb_size = lb_size_2;
              back_distance = back_distance_2;
            }
          }
          if (lb_size != 0) {
            size -= back_distance;
            break;
          }
        }
        // need to update the hashmap mid-literal
        if (i + size + DEFL_HASH_LENGTH < input_len) hashmap_insert(&hashmap, &input[i + size], i + size);
        size += 1;
      }

      assert(size <= input_len - i);
      if (lb_size > 258) lb_size = 258;

      literal_count += size;

      // check for literal
      if (size != 0) {
        // printf("producing literal with size %d\n", size);
        commands[command_count * 4 + 0] = size;
        commands[command_count * 4 + 1] = (uint64_t)&input[i];

        size_t end = i + size;
        while (i < end) counts[input[i++]] += 1;
      }
      // check for lookback hit
      if (lb_size != 0) {
        uint64_t dist = i - lb_loc;
        assert(dist <= i);

        uint16_t size_code = 0;
        len_get_info(lb_size, &size_code, 0, 0);
        counts[size_code] += 1;

        uint16_t dist_code = 0;
        dist_get_info(dist, &dist_code, 0, 0);
        dist_counts[dist_code] += 1;

        commands[command_count * 4 + 2] = lb_size;
        commands[command_count * 4 + 3] = dist;

        // advance cursor and update hashmap
        uint64_t start_i = i;
        i += 1;
        for (size_t j = 1; j < lb_size; j++) {
          if (i + DEFL_HASH_LENGTH < input_len) hashmap_insert(&hashmap, &input[i], i);
          i += 1;
        }
        if (start_i + DEFL_HASH_LENGTH < input_len) hashmap_insert(&hashmap, &input[start_i], start_i);

        lb_size = 0;
      }
      command_count += 1;
    }
    counts[256] = 1;

    // build huff dictionaries

    // dict is in order of symbol, including unused symbols
    // unordered dict is in order of frequency/code

    huff_node_t *unordered_dict[288] = {0};
    huff_node_t *dict[288] = {0};
    huff_node_t *root_to_free = 0;
    gen_canonical_code(counts, unordered_dict, dict, &root_to_free, 288);

    huff_node_t *dist_unordered_dict[32] = {0};
    huff_node_t *dist_dict[32] = {0};
    huff_node_t *dist_root_to_free = 0;
    gen_canonical_code(dist_counts, dist_unordered_dict, dist_dict, &dist_root_to_free, 32);

    // printf("chunk starting at %08X:%d\n", ret.byte_index, ret.bit_index);

    bit_push(&ret, 0); // not the final chunk
    bits_push(&ret, 2, 2); // dynamic huffman chunk

    huff_write_code_desc(&ret, dict, dist_dict);

    // printf("huff desc ended at %08X:%d\n", ret.byte_index, ret.bit_index);

    for (size_t j = 0; j < command_count; j++) {
      uint64_t size = commands[j * 4 + 0];
      uint64_t lb_size = commands[j * 4 + 2];
      uint64_t dist = commands[j * 4 + 3];

      // push literals
      if (size != 0) {
        // if (j < 8 &&  0x000332CC)
        //     printf("producing lookback with size %d and dist %d\n", lb_size, dist);

        uint8_t *start = (uint8_t *)commands[j * 4 + 1];
        uint8_t *end = start + size;
        while (start < end) {
          huff_node_t *node = dict[*(start++)];
          bits_push(&ret, node->code, node->code_len);
        }
      }

      if (dist != 0) // lookback
      {
        uint16_t size_code = 0;
        uint16_t size_bit_count = 0;
        uint16_t size_bits = 0;
        len_get_info(lb_size, &size_code, &size_bit_count, &size_bits);

        huff_node_t *size_node = dict[size_code];
        bits_push(&ret, size_node->code, size_node->code_len);
        bits_push(&ret, size_bits, size_bit_count);

        uint16_t dist_code = 0;
        uint16_t dist_bit_count = 0;
        uint16_t dist_bits = 0;
        dist_get_info(dist, &dist_code, &dist_bit_count, &dist_bits);

        huff_node_t *dist_node = dist_dict[dist_code];
        bits_push(&ret, dist_node->code, dist_node->code_len);
        bits_push(&ret, dist_bits, dist_bit_count);
      }
    }
    huff_node_t *lit_node = dict[256];
    // printf("pushing code 0x%X with length %d\n", lit_node->code, lit_node->code_len);
    bits_push(&ret, lit_node->code, lit_node->code_len);

    // printf("chunk ended at %08X:%d\n", ret.byte_index, ret.bit_index);

    if (root_to_free) free_huff_nodes(root_to_free);
    if (dist_root_to_free) free_huff_nodes(dist_root_to_free);

    // puts("-- ending compressed block!");
  }
  // push an empty chunk at the end with the final chunk flag set

  // printf("-- addr %08llX bit %d\n", (unsigned long long)ret.byte_index, ret.bit_index);
  bit_push(&ret, 1); // final chunk
  bits_push(&ret, 0, 2); // uncompressed chunk
  // printf("-- addr %08llX bit %d\n", (unsigned long long)ret.byte_index, ret.bit_index);
  bits_align_to_byte(&ret);

  // printf("-- addr %08llX\n", (unsigned long long)ret.byte_index);

  bits_push(&ret, 0, 16); // zero length
  bits_push(&ret, 0xFFFF, 16); // zero length (one's complement)

  // zlib
  if (header_mode == 1) {
    bits_align_to_byte(&ret);
    bits_push(&ret, byteswap_int(checksum, 4), 32);
  }
  // gzip
  else if (header_mode >= 2) {
    bits_align_to_byte(&ret);
    bits_push(&ret, checksum, 32);
    bits_push(&ret, input_len & 0xFFFFFFFF, 32);
  }
  // printf("-- addr %08llX\n", (unsigned long long)ret.byte_index);

  // puts("-- wrote final block!");

  DEFL_FREE(hashmap.hashtable);
  DEFL_FREE(hashmap.prevlink);

  return ret;
}

#endif // INCL_DEFLATE
