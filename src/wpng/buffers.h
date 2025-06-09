#ifndef INCL_BUFFERS
#define INCL_BUFFERS

#include <assert.h> // assert
#include <stddef.h> // size_t
#include <stdint.h>
#include <string.h> // memcpy

#ifndef BUF_REALLOC
#define BUF_REALLOC (realloc)
#endif

static inline uint64_t byteswap_int(uint64_t value, uint8_t bytecount) {
  uint64_t ret = 0;
  for (uint8_t i = 0; i < bytecount; i++) ((uint8_t *)&ret)[i] = ((uint8_t *)&value)[bytecount - 1 - i];
  return ret;
}

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t cur;
} byte_buffer;

static inline void bytes_reserve(byte_buffer *buf, size_t extra) {
  if (buf->cap < 8) buf->cap = 8;
  while (buf->len + extra >= buf->cap) buf->cap <<= 1;
  uint8_t *next = (uint8_t *)BUF_REALLOC(buf->data, buf->cap);
  assert(next);
  buf->data = next;
  if (!buf->data) buf->cap = 0;
}
static inline void byte_push(byte_buffer *buf, uint8_t byte) {
  if (buf->len >= buf->cap || buf->data == 0) {
    buf->cap = buf->cap << 1;
    if (buf->cap < 8) buf->cap = 8;
    uint8_t *next = (uint8_t *)BUF_REALLOC(buf->data, buf->cap);
    assert(next);
    buf->data = next;
  }
  buf->data[buf->len] = byte;
  buf->len += 1;
}
static inline void bytes_push(byte_buffer *buf, const uint8_t *bytes, size_t count) {
  bytes_reserve(buf, count);
  assert(buf->len + count <= buf->cap);
  memcpy(&buf->data[buf->len], bytes, count);
  buf->len += count;
}
static inline void bytes_push_int(byte_buffer *buf, uint64_t value, size_t count) {
  bytes_reserve(buf, count);
  for (size_t i = 0; i < count; i++) {
    byte_push(buf, value);
    value >>= 8;
  }
}
static inline uint8_t byte_pop(byte_buffer *buf) {
  return buf->data[buf->cur++];
}
static inline uint64_t bytes_pop_int(byte_buffer *buf, size_t count) {
  uint64_t ret = 0;
  for (size_t i = 0; i < count; i++) ret |= ((uint64_t)buf->data[buf->cur + i]) << (8 * i);
  buf->cur += count;
  return ret;
}

typedef struct {
    byte_buffer buffer;
    size_t byte_index;
    uint8_t bit_index;
} bit_buffer;

static inline void bit_push(bit_buffer *buf, uint8_t data) {
  if (buf->bit_index >= 8 || buf->buffer.len == 0) {
    byte_push(&buf->buffer, 0);
    if (buf->bit_index >= 8) {
      buf->byte_index += 1;
      buf->bit_index -= 8;
    }
  }
  buf->buffer.data[buf->byte_index] |= data << buf->bit_index;
  buf->bit_index += 1;
}
static inline void bits_push(bit_buffer *buf, uint64_t data, uint8_t bits) {
  for (uint8_t n = 0; n < bits; n += 1) {
    bit_push(buf, data & 1);
    data >>= 1;
  }
  return;

  if (bits == 0) return;

  // push byte if there's nothing to write bits into
  if (buf->buffer.len == 0) byte_push(&buf->buffer, 0);

  if (buf->bit_index >= 8) {
    byte_push(&buf->buffer, 0);
    buf->byte_index += 1;
    buf->bit_index -= 8;
  }

  assert(buf->byte_index == buf->buffer.len - 1);

  // if we have more bits to push than are available, push as many bits as possible without adding new bytes all at once
  if (bits > 8 - buf->bit_index) {
    uint8_t avail = 8 - buf->bit_index;
    uint64_t mask = (1 << avail) - 1;
    buf->buffer.data[buf->byte_index] |= (uint8_t)(data & mask) << buf->bit_index;

    buf->bit_index = 8;

    bits -= avail;
    data >>= avail;

    // then push any remaining whole bytes worth of bits all at once
    while (bits >= 8) {
      byte_push(&buf->buffer, 0);
      buf->byte_index += 1;
      buf->buffer.data[buf->byte_index] |= (uint8_t)data & 0xFF;

      bits -= 8;
      data >>= 8;
    }
  }

  if (bits > 0) {
    if (buf->bit_index >= 8) {
      byte_push(&buf->buffer, 0);
      buf->byte_index += 1;
      buf->bit_index -= 8;
    }

    uint64_t mask = (1 << bits) - 1;
    buf->buffer.data[buf->byte_index] |= (data & mask) << buf->bit_index;
    buf->bit_index += bits;
    return;
  }
}
static inline uint64_t bits_pop(bit_buffer *buf, uint8_t bits) {
  if (buf->byte_index >= buf->buffer.len) return 0;
  if (bits == 0) return 0;
  uint64_t ret = 0;
  for (uint8_t n = 0; n < bits; n += 1) {
    if (buf->bit_index >= 8) {
      buf->byte_index += 1;
      buf->bit_index -= 8;
    }
    ret |= (uint64_t)((buf->buffer.data[buf->byte_index] >> buf->bit_index) & 1) << n;
    buf->bit_index += 1;
  }
  return ret;
}
static inline uint8_t bit_pop(bit_buffer *buf) {
  if (buf->byte_index >= buf->buffer.len) return 0;
  if (buf->bit_index >= 8) {
    buf->byte_index += 1;
    buf->bit_index -= 8;
  }
  uint8_t ret = (buf->buffer.data[buf->byte_index] >> buf->bit_index) & 1;
  buf->bit_index += 1;

  return ret;
}
static inline void bits_align_to_byte(bit_buffer *buf) {
  buf->bit_index = 8;
}

#endif // INCL_BUFFERS
