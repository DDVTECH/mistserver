#ifndef WPNG_READ_INCLUDED
#define WPNG_READ_INCLUDED

// you probably want:
// static void wpng_load(byte_buffer * buf, uint32_t flags, wpng_load_output * output)

#include "buffers.h"
#include "inflate.h"
#include "wpng_common.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#ifndef TEST_VS_LIBPNG
static double to_srgb(double x) {
  if (x > 0.0031308)
    return 1.055 * (pow(x, 1.0 / 2.4)) - 0.055;
  else
    return 12.92 * x;
}
#endif

static uint16_t apply_gamma_u16(uint16_t val, double gamma) {
#ifdef TEST_VS_LIBPNG
  // FIXME / FILE A BUG REPORT:
  // libpng 1.6.40's high-level interface handles the gAMA chunk wrong
  // it's supposed to generate sRGB image data, but it actually generates gamma 2.2 image data
  return round(pow(val / 65535.0, gamma / 2.2) * 65535.0);
#else
  return round(to_srgb(pow(val / 65535.0, gamma)) * 65535.0);
#endif
}
static uint8_t apply_gamma_u8(uint8_t val, double gamma) {
#ifdef TEST_VS_LIBPNG
  // FIXME / FILE A BUG REPORT:
  // libpng 1.6.40's high-level interface handles the gAMA chunk wrong
  // it's supposed to generate sRGB image data, but it actually generates gamma 2.2 image data
  return round(pow(val / 255.0, gamma / 2.2) * 255.0);
#else
  return round(to_srgb(pow(val / 255.0, gamma)) * 255.0);
#endif
}

static void apply_gamma(uint32_t width, uint32_t height, uint8_t bpp, uint8_t is_16bit, uint8_t *image_data,
                        size_t bytes_per_scanline, float gamma) {
  uint8_t components = bpp;
  if (is_16bit) components /= 2;

  for (size_t y = 0; y < height; y += 1) {
    for (size_t x = 0; x < width * components; x += 1) {
      if ((components == 2 || components == 4) && (x % components == (uint8_t)(components - 1))) continue;
      if (is_16bit) {
        uint16_t a = image_data[y * bytes_per_scanline + x * 2 + 0];
        uint16_t b = image_data[y * bytes_per_scanline + x * 2 + 1];
        uint16_t val = (a << 8) | b;
        val = apply_gamma_u16(val, gamma);
        image_data[y * bytes_per_scanline + x * 2 + 0] = val >> 8;
        image_data[y * bytes_per_scanline + x * 2 + 1] = val;
      } else {

        uint8_t val = image_data[y * bytes_per_scanline + x];
        val = apply_gamma_u8(val, gamma);
        image_data[y * bytes_per_scanline + x] = val;
      }
    }
  }
}

static void defilter(uint8_t *image_data, size_t data_size, byte_buffer *dec, uint32_t width, uint32_t height,
                     uint8_t interlace_layer, uint8_t bit_depth, uint8_t components, uint8_t *error) {
#define WPNG_ASSERT(COND, ERRVAL) \
  {                               \
    if (!(COND)) {                \
      *error = (ERRVAL);          \
      return;                     \
    }                             \
  }

  // interlace_layer:
  // 0: not interlaced
  // 1~7: adam7 interlace layers
  //  1 6 4 6 2 6 4 6
  //  7 7 7 7 7 7 7 7
  //  5 6 5 6 5 6 5 6
  //  7 7 7 7 7 7 7 7
  //  3 6 4 6 3 6 4 6
  //  7 7 7 7 7 7 7 7
  //  5 6 5 6 5 6 5 6
  //  7 7 7 7 7 7 7 7

  uint8_t y_inits[] = {0, 0, 0, 4, 0, 2, 0, 1};
  uint8_t y_gaps[] = {1, 8, 8, 8, 4, 4, 2, 2};
  uint8_t x_inits[] = {0, 0, 4, 0, 2, 0, 1, 0};
  uint8_t x_gaps[] = {1, 8, 8, 4, 4, 2, 2, 1};

  size_t y_init = y_inits[interlace_layer];
  size_t y_gap = y_gaps[interlace_layer];
  size_t x_init = x_inits[interlace_layer];
  size_t x_gap = x_gaps[interlace_layer];

  // note: bit_depth can only be less than 8 if there is only one component

  size_t bytes_per_scanline = (((size_t)width - x_init + x_gap - 1) / x_gap * bit_depth + 7) / 8 * components;
  // printf("%d %d %d %lld\n", width, bit_depth, components, bytes_per_scanline);
  size_t min_bytes = (bit_depth + 7) / 8;
  size_t output_bps = min_bytes * components * width;
  // printf("%lld %d %d %d\n", dec->len, output_bps, output_bps * (height - y_init + y_gap - 1) / y_gap, height);

  // printf("!!! %lld %d %d %lld %lld\n", height, interlace_layer, bytes_per_scanline, min_bytes, output_bps);

  uint8_t *y_prev = (uint8_t *)malloc(bytes_per_scanline);
  uint8_t *y_prev_next = (uint8_t *)malloc(bytes_per_scanline);
  WPNG_ASSERT(y_prev, 100);
  WPNG_ASSERT(y_prev_next, 100);
  memset(y_prev, 0, bytes_per_scanline);
  memset(y_prev_next, 0, bytes_per_scanline);

  // printf("%d\n", min_bytes * components);

  size_t scanline_count = ((size_t)height - y_init + y_gap - 1) / y_gap;
  // printf("%lld %lld %lld\n", scanline_count, scanline_count * (bytes_per_scanline + 1), dec->len);
  if (scanline_count * (bytes_per_scanline + 1) > dec->len) {
    *error = 2;
    return;
  }

  for (size_t y = y_init; bytes_per_scanline > 0 && y < height; y += y_gap) {
    // double buffered y_prev
    uint8_t *temp = y_prev;
    y_prev = y_prev_next;
    y_prev_next = temp;

    uint8_t filter_type = byte_pop(dec);
    // puts("--");
    // printf("%lld %lld\n", dec->cur + bytes_per_scanline, dec->len);
    // printf("%lld %d %d %lld\n", y, height, interlace_layer, bytes_per_scanline);
    WPNG_ASSERT(dec->cur + bytes_per_scanline <= dec->len, 1);

    // printf("at y %d ", y);
    // if (filter_type == 0)
    //     puts("filter mode 0");
    // if (filter_type == 1)
    //     puts("filter mode 1");
    // if (filter_type == 2)
    //     puts("filter mode 2");
    // if (filter_type == 3)
    //     puts("filter mode 3");
    // if (filter_type == 4)
    //     puts("filter mode 4");

    for (size_t x = 0; x < bytes_per_scanline; x += 1) {
      uint8_t byte = byte_pop(dec);
      int16_t left = x >= min_bytes * components ? y_prev_next[x - min_bytes * components] : 0;
      int16_t up = y_prev[x];
      int16_t upleft = x >= min_bytes * components ? y_prev[x - min_bytes * components] : 0;

      if (filter_type == 1) byte += left;
      if (filter_type == 2) byte += up;
      if (filter_type == 3) byte += (up + left) / 2;
      if (filter_type == 4) byte += paeth_get_ref_raw(left, up, upleft);

      // note: bits_per_pixel can only be less than 8 if there is only one component
      if (bit_depth < 8) {
        size_t start = y * output_bps;
        size_t x_out = (x_gap * x) * 8 / bit_depth + x_init;
        for (size_t i = 0; i < 8 / bit_depth; i++) {
          if (i * x_gap + x_out >= width) {
            // printf("breaking at %d\n", i);
            break;
          }
          uint8_t val = byte >> (8 - (bit_depth * (i + 1)));
          val &= (1 << bit_depth) - 1;
          // if (interlace_layer == 6)
          //     printf("asdf %d %d\n", start + x_out + x_gap * i, val);
          assert(start + x_out + x_gap * i < data_size);
          image_data[start + x_out + x_gap * i] = val;
        }
      } else {
        size_t true_x = x / (min_bytes * components) * min_bytes * components;
        size_t leftover_x = x - true_x;
        size_t x_out = true_x * x_gap + leftover_x + x_init * min_bytes * components;
        assert(y * output_bps + x_out < data_size);
        image_data[y * output_bps + x_out] = byte;
      }

      y_prev_next[x] = byte;
    }
  }

  free(y_prev);
  free(y_prev_next);

#undef WPNG_ASSERT
}

enum {
  WPNG_READ_SKIP_CRC = 1, // don't check chunk CRCs
  WPNG_READ_SKIP_CRITICAL_CHUNKS = 2, // skip unknown critical chunks
  WPNG_READ_SKIP_GAMMA_CORRECTION = 4, // don't apply gamma correction
  WPNG_READ_SKIP_IDAT_CRC = 8, // like chrome
  WPNG_READ_ERROR_ON_BAD_ANCILLARY_CRC = 16, // treat chunks with bad CRCs like unknown chunks
  WPNG_READ_SKIP_ADLER32 = 32, // don't check zlib checksums
  WPNG_READ_FORCE_8BIT = 256, // convert 16-bit images to 8-bit on load
};

// output:

typedef struct {
    uint8_t *data; // u8 array
    size_t size; // size of array
    size_t bytes_per_scanline; // width
    uint32_t width; // height
    uint32_t height; // bytes per pixel
    float gamma; // is 16 bit or not (decoder output)
    uint8_t bytes_per_pixel; // was originally 16 bit or not (original png file)
    uint8_t is_16bit; // png file specified that it was srgb or not
    uint8_t was_16bit; // scanline byte count
    uint8_t was_srgb; // gamma (-1 if unset or srgb)
    uint8_t error;
} wpng_load_output;
// NOTE: the decoder ALWAYS output srgb data, even if was_srgb is unset!

static inline uint8_t u16_to_u8(uint16_t val) {
  // return round((double)val / 257.0); // seems to be what libpng does
  //  pure integer equivalent
  uint16_t rem = val % 257;
  val /= 257;
  val += rem > 128;
  return val;
}

// on error, writes nonzero to the "error" value of output and leaves the rest untouched
// on success, writes zero to the "error" value of output and writes every other value
static void wpng_load(byte_buffer *buf, uint32_t flags, wpng_load_output *output) {
  assert(output);
  assert(buf);
  assert(buf->data);

#define WPNG_ASSERT(COND, ERRVAL) \
  {                               \
    if (!(COND)) {                \
      output->error = (ERRVAL);   \
      return;                     \
    }                             \
  }
  // 1 - chunk size error
  // 2 - buffer overflow
  // 3 - invalid chunk name
  // 4 - failed crc
  // 5 - chunk syntax error or invalid value within chunk
  // 6 - chunk ordering error
  // 7 - unknown critical chunk
  // 8 - missing mandatory chunk (IHDR/IDAT/IEND)
  // 9 - has chunk that's forbidden for given color format
  // 10 - missing contextual mandatory chunk (PLTE on indexed images)
  // 11 - invalid zlib data
  // 100 - allocation failure
  // 255 - not a png file

  if (buf->len < 8 || memcmp(buf->data, "\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8) != 0) {
    output->error = 255;
    return;
  }
  buf->cur = 8;

  byte_buffer idat;
  memset(&idat, 0, sizeof(byte_buffer));

  uint32_t width = 0;
  uint32_t height = 0;
  uint8_t bit_depth = 0;
  uint8_t color_type = 0;
  uint8_t interlacing = 0;

  uint8_t palette[1024] = {0};
  uint16_t palette_size = 0;
  uint8_t has_idat = 0;
  uint8_t has_iend = 0;

  uint8_t has_trns = 0;
  uint8_t has_chrm = 0;
  uint8_t has_gama = 0;
  uint8_t has_iccp = 0;
  uint8_t has_sbit = 0;
  uint8_t has_bkgd = 0;
  uint8_t has_hist = 0;
  uint8_t has_phys = 0;
  uint8_t has_time = 0;

  uint32_t transparent_r = 0xFFFFFFFF;
  uint32_t transparent_g = 0xFFFFFFFF;
  uint32_t transparent_b = 0xFFFFFFFF;

  uint64_t chunk_count = 0;

  uint8_t is_srgb = 0;
  float gamma = -1.0;

  uint8_t prev_was_idat = 0; // multiple idat chunks must be consecutive
  while (buf->cur < buf->len) {
    chunk_count += 1;

    uint32_t size = byteswap_int(bytes_pop_int(buf, 4), 4);
    WPNG_ASSERT(size < 0x80000000, 1); // must be 31 bit
    WPNG_ASSERT(buf->cur + size + 8 <= buf->len, 2); // must not overflow buffer

    size_t chunk_start = buf->cur;
    char name[5] = {0};
    for (size_t i = 0; i < 4; i += 1) {
      name[i] = byte_pop(buf);
      uint8_t upcased = name[i] | 0x20;
      WPNG_ASSERT(upcased >= 97 && upcased <= 122, 3);
    }
    uint8_t critical = !(name[0] & 0x20);

    size_t cur_start = buf->cur;

    uint8_t checksum_failed = 0;
    if (!(flags & WPNG_READ_SKIP_CRC) && !((flags & WPNG_READ_SKIP_IDAT_CRC) && memcmp(name, "IDAT", 4) == 0)) {
      buf->cur = cur_start + size;
      uint32_t expected_checksum = byteswap_int(bytes_pop_int(buf, 4), 4);
      uint32_t checksum = infl_compute_crc32(&buf->data[chunk_start], size + 4, 0);
      checksum_failed = (checksum != expected_checksum);
    }

    // always check CRC for critical chunks
    if (critical) WPNG_ASSERT(!checksum_failed, 4);
    // skip ancillary chunks with bad checksums, unless the option is set to error instead
    if (checksum_failed) {
      if (flags & WPNG_READ_ERROR_ON_BAD_ANCILLARY_CRC)
        WPNG_ASSERT(!checksum_failed, 4)
      else {
        buf->cur = cur_start + size + 4;
        continue;
      }
    }

    buf->cur = cur_start;

    if (memcmp(name, "IHDR", 4) == 0) {
      WPNG_ASSERT(size == 13, 5);
      WPNG_ASSERT(chunk_count == 1, 6); // must be first
      WPNG_ASSERT(width == 0 && height == 0, 6); // must not have multiple

      width = byteswap_int(bytes_pop_int(buf, 4), 4);
      height = byteswap_int(bytes_pop_int(buf, 4), 4);
      WPNG_ASSERT(width != 0 && height != 0, 5); // must be coherent

      bit_depth = byte_pop(buf);
      WPNG_ASSERT(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8 || bit_depth == 16, 5);
      color_type = byte_pop(buf);
      WPNG_ASSERT(color_type <= 6 && color_type != 1 && color_type != 5, 5);

      if (color_type == 2 || color_type == 4 || color_type == 6)
        WPNG_ASSERT(bit_depth == 8 || bit_depth == 16, 5)
      else if (color_type == 3)
        WPNG_ASSERT(bit_depth == 1 || bit_depth == 2 || bit_depth == 4 || bit_depth == 8, 5)

      WPNG_ASSERT(byte_pop(buf) == 0, 5); // compression method, must always be 0 for PNGs
      WPNG_ASSERT(byte_pop(buf) == 0, 5); // filter method, must always be 0 for PNGs
      interlacing = byte_pop(buf);
    } else if (memcmp(name, "sRGB", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!is_srgb, 6); // can't have multiple
      is_srgb = 1;
    } else if (memcmp(name, "gAMA", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_gama, 6); // can't have multiple
      has_gama = 1;
      gamma = 100000.0 / byteswap_int(bytes_pop_int(buf, 4), 4);
    } else if (memcmp(name, "iCCP", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_iccp, 6); // can't have multiple
      has_iccp = 1;
    } else if (memcmp(name, "cHRM", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_chrm, 6); // can't have multiple
      has_chrm = 1;
    } else if (memcmp(name, "sBIT", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_sbit, 6); // can't have multiple
      has_sbit = 1;
    } else if (memcmp(name, "bKGD", 4) == 0) {
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_bkgd, 6); // can't have multiple
      has_bkgd = 1;
    } else if (memcmp(name, "hIST", 4) == 0) {
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_hist, 6); // can't have multiple
      has_hist = 1;
    } else if (memcmp(name, "pHYS", 4) == 0) {
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
      WPNG_ASSERT(!has_phys, 6); // can't have multiple
      has_phys = 1;
    } else if (memcmp(name, "tIME", 4) == 0) {
      WPNG_ASSERT(!has_time, 6); // can't have multiple
      has_time = 1;
    } else if (memcmp(name, "sBIT", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
    } else if (memcmp(name, "gAMA", 4) == 0) {
      WPNG_ASSERT(palette_size == 0, 6); // can't come after palette
      WPNG_ASSERT(!has_idat, 6); // can't come after idat
    } else if (memcmp(name, "IDAT", 4) == 0) {
      WPNG_ASSERT(!has_idat || prev_was_idat, 6); // multiple idat chunks must be consecutive
      bytes_push(&idat, &buf->data[buf->cur], size);
      has_idat = 1;
    } else if (memcmp(name, "PLTE", 4) == 0) {
      WPNG_ASSERT(!palette_size, 6); // must not have multiple
      WPNG_ASSERT(!has_idat, 6); // must precede idat chunks
      WPNG_ASSERT(!has_trns, 6); // must not come after trns chunk
      WPNG_ASSERT(!has_bkgd, 6); // must not come after bkgd chunk
      WPNG_ASSERT(!has_hist, 6); // must not come after hist chunk

      WPNG_ASSERT(size % 3 == 0, 5); // item count must be an integer
      WPNG_ASSERT(size > 0, 5); // minimum allowed item count is 1
      WPNG_ASSERT(size <= 768, 5); // maximum allowed item count is 256
      for (size_t i = 0; i < size / 3; i += 1) {
        palette[i * 4 + 0] = byte_pop(buf);
        palette[i * 4 + 1] = byte_pop(buf);
        palette[i * 4 + 2] = byte_pop(buf);
        palette[i * 4 + 3] = 0xFF;
      }
      palette_size = size / 3;
    } else if (memcmp(name, "tRNS", 4) == 0) {
      WPNG_ASSERT(!has_trns, 6); // must not have multiple
      WPNG_ASSERT(!has_idat, 6); // must precede the first idat chunk

      WPNG_ASSERT(color_type == 0 || color_type == 2 || color_type == 3, 5);
      // indexed
      if (color_type == 3) {
        WPNG_ASSERT(size <= palette_size, 5); // must not contain more entries than palette entries (but is allowed to contain less)
        for (size_t i = 0; i < size; i += 1) palette[i * 4 + 3] = byte_pop(buf);
      }
      // grayscale
      if (color_type == 0) {
        WPNG_ASSERT(size == 2, 5);
        uint16_t val = byteswap_int(bytes_pop_int(buf, 2), 2);
        // ... "(If the image bit depth is less than 16, the least significant bits are used and the others are 0.)`
        // interpreting the above language from the PNG spec as a hard requirement
        WPNG_ASSERT(val < (1 << bit_depth), 5);
        transparent_r = val;
        transparent_g = val;
        transparent_b = val;
      }
      // rgb
      if (color_type == 2) {
        WPNG_ASSERT(size == 6, 5);
        uint16_t val_r = byteswap_int(bytes_pop_int(buf, 2), 2);
        WPNG_ASSERT(val_r < (1 << bit_depth), 5);
        uint16_t val_g = byteswap_int(bytes_pop_int(buf, 2), 2);
        WPNG_ASSERT(val_g < (1 << bit_depth), 5);
        uint16_t val_b = byteswap_int(bytes_pop_int(buf, 2), 2);
        WPNG_ASSERT(val_b < (1 << bit_depth), 5);
        transparent_r = val_r;
        transparent_g = val_g;
        transparent_b = val_b;
      }
      has_trns = 1;
    } else if (memcmp(name, "IEND", 4) == 0) {
      has_iend = 1;
      break;
    } else {
      if (!(flags & WPNG_READ_SKIP_CRITICAL_CHUNKS)) WPNG_ASSERT(!critical, 7); // unknown chunks must not be critical
    }

    prev_was_idat = (memcmp(name, "IDAT", 4) == 0);

    WPNG_ASSERT(width != 0 && height != 0, 8); // header must exist and give valid width/height values

    buf->cur = cur_start + size + 4;
  }
  uint8_t was_16bit = bit_depth == 16;

  WPNG_ASSERT(has_idat, 8);
  WPNG_ASSERT(has_iend, 8);
  WPNG_ASSERT(width != 0 && height != 0, 8); // header must exist

  if (color_type == 4 || color_type == 6) WPNG_ASSERT(has_trns == 0, 9);
  if (color_type == 0 || color_type == 4) WPNG_ASSERT(palette_size == 0, 9);
  if (color_type == 3) WPNG_ASSERT(palette_size != 0, 10);

  uint8_t temp[] = {1, 0, 3, 1, 2, 0, 4};
  uint8_t components = temp[color_type];

  uint8_t bpp = components * ((bit_depth + 7) / 8);
  size_t bytes_per_scanline = width * bpp;

  idat.cur = 0;
  int error = 0;
  byte_buffer dec = do_inflate(&idat, &error, 1); // decompresses into `dec` (declared earlier)
  free(idat.data);
  dec.cur = 0;
  WPNG_ASSERT(error == 0, 11);

  uint8_t *image_data = (uint8_t *)malloc(height * bytes_per_scanline);
  WPNG_ASSERT(image_data, 100);
  memset(image_data, 0, height * bytes_per_scanline);

  uint8_t defilter_error = 0;
  if (!interlacing) {
    defilter(image_data, height * bytes_per_scanline, &dec, width, height, 0, bit_depth, components, &defilter_error);
    WPNG_ASSERT(defilter_error == 0, defilter_error);
  } else {
    for (uint8_t i = 1; i <= 7; i += 1) {
      defilter(image_data, height * bytes_per_scanline, &dec, width, height, i, bit_depth, components, &defilter_error);
      WPNG_ASSERT(defilter_error == 0, defilter_error);
    }
  }

  // convert to Y/YA/RGB/RGBA if needed

  uint8_t out_bpp = components + has_trns;
  if (color_type == 3) out_bpp = 3 + has_trns;
  if (bit_depth == 16 && !(flags & WPNG_READ_FORCE_8BIT)) out_bpp *= 2;

  // runs if any one of the following is true:
  // - image is indexed
  // - image has an alpha override (trns chunk) (whether indexed or cutoff)
  // - image is 1, 2, or 4 bits per channel (e.g. grayscale)
  // - image is 16 bits per pixel and WPNG_READ_FORCE_8BIT is enabled
  if (color_type == 3 || has_trns || bit_depth < 8 || out_bpp != bpp) {
    uint8_t *out_image_data = (uint8_t *)malloc(height * width * out_bpp);
    WPNG_ASSERT(out_image_data, 100);
    memset(out_image_data, 0, height * width * out_bpp);

    if (color_type == 3) {
      for (size_t y = 0; y < height; y += 1) {
        for (size_t x = 0; x < width; x += 1) {
          uint8_t val = image_data[y * bytes_per_scanline + x];
          out_image_data[(y * width + x) * out_bpp + 0] = palette[val * 4 + 0];
          out_image_data[(y * width + x) * out_bpp + 1] = palette[val * 4 + 1];
          out_image_data[(y * width + x) * out_bpp + 2] = palette[val * 4 + 2];
          out_image_data[(y * width + x) * out_bpp + 3] = palette[val * 4 + 3];
        }
      }
    } else if (components == 1) {
      for (size_t y = 0; y < height; y += 1) {
        for (size_t x = 0; x < width; x += 1) {
          if (bit_depth == 16) {
            size_t i = y * bytes_per_scanline;
            uint16_t val = ((uint16_t)image_data[i + x * 2] << 8) | image_data[i + x * 2 + 1];

            if (flags & WPNG_READ_FORCE_8BIT) {
              out_image_data[(y * width + x) * out_bpp + 0] = u16_to_u8(val);
              if (has_trns) out_image_data[(y * width + x) * out_bpp + 1] = val == transparent_r ? 0 : 0xFF;
            } else {
              out_image_data[(y * width + x) * out_bpp + 0] = val >> 8;
              out_image_data[(y * width + x) * out_bpp + 1] = val & 0xFF;
              if (has_trns) {
                out_image_data[(y * width + x) * out_bpp + 2] = val == transparent_r ? 0 : 0xFF;
                out_image_data[(y * width + x) * out_bpp + 3] = val == transparent_r ? 0 : 0xFF;
              }
            }
          } else {
            uint8_t val = image_data[y * bytes_per_scanline + x];

            if (has_trns) out_image_data[(y * width + x) * out_bpp + 1] = val == transparent_r ? 0 : 0xFF;

            if (bit_depth == 1)
              val *= 0xFF;
            else if (bit_depth == 2)
              val *= 0x55;
            else if (bit_depth == 4)
              val *= 0x11;

            out_image_data[(y * width + x) * out_bpp] = val;
          }
        }
      }
      if (bit_depth == 16 && (flags & WPNG_READ_FORCE_8BIT)) bit_depth = 8;
    } else if (components == 3) {
      for (size_t y = 0; y < height; y += 1) {
        for (size_t x = 0; x < width; x += 1) {
          size_t i = y * bytes_per_scanline;
          if (bit_depth == 16) {
            uint16_t val_r = ((uint16_t)image_data[i + x * 6 + 0] << 8) | image_data[i + x * 6 + 1];
            uint16_t val_g = ((uint16_t)image_data[i + x * 6 + 2] << 8) | image_data[i + x * 6 + 3];
            uint16_t val_b = ((uint16_t)image_data[i + x * 6 + 4] << 8) | image_data[i + x * 6 + 5];

            uint8_t is_transparent = (val_r == transparent_r && val_g == transparent_g && val_b == transparent_b);

            if (flags & WPNG_READ_FORCE_8BIT) {
              out_image_data[(y * width + x) * out_bpp + 0] = u16_to_u8(val_r);
              out_image_data[(y * width + x) * out_bpp + 1] = u16_to_u8(val_g);
              out_image_data[(y * width + x) * out_bpp + 2] = u16_to_u8(val_b);
              if (has_trns) out_image_data[(y * width + x) * out_bpp + 3] = is_transparent ? 0 : 0xFF;
            } else {
              out_image_data[(y * width + x) * out_bpp + 0] = val_r >> 8;
              out_image_data[(y * width + x) * out_bpp + 1] = val_r & 0xFF;
              out_image_data[(y * width + x) * out_bpp + 2] = val_g >> 8;
              out_image_data[(y * width + x) * out_bpp + 3] = val_g & 0xFF;
              out_image_data[(y * width + x) * out_bpp + 4] = val_b >> 8;
              out_image_data[(y * width + x) * out_bpp + 5] = val_b & 0xFF;
              if (has_trns) {
                out_image_data[(y * width + x) * out_bpp + 6] = is_transparent ? 0 : 0xFF;
                out_image_data[(y * width + x) * out_bpp + 7] = is_transparent ? 0 : 0xFF;
              }
            }
          } else {
            uint8_t val_r = image_data[i + x * 3 + 0];
            uint8_t val_g = image_data[i + x * 3 + 1];
            uint8_t val_b = image_data[i + x * 3 + 2];

            uint8_t is_transparent = (val_r == transparent_r && val_g == transparent_g && val_b == transparent_b);

            out_image_data[(y * width + x) * out_bpp + 0] = val_r;
            out_image_data[(y * width + x) * out_bpp + 1] = val_g;
            out_image_data[(y * width + x) * out_bpp + 2] = val_b;
            if (has_trns) out_image_data[(y * width + x) * out_bpp + 3] = is_transparent ? 0 : 0xFF;
          }
        }
      }
      if (bit_depth == 16 && (flags & WPNG_READ_FORCE_8BIT)) bit_depth = 8;
    } else // components == 2 or 4
    {
      assert(bit_depth == 16 && (flags & WPNG_READ_FORCE_8BIT));
      for (size_t y = 0; y < height; y += 1) {
        for (size_t x = 0; x < width * components; x += 1) {
          size_t i = y * bytes_per_scanline;
          uint16_t val = ((uint16_t)image_data[i + x * 2 + 0] << 8) | image_data[i + x * 2 + 1];
          out_image_data[(y * width) * out_bpp + x] = u16_to_u8(val);
        }
      }
      bit_depth = 8;
    }

    free(image_data);
    bpp = out_bpp;
    image_data = out_image_data;
    bytes_per_scanline = width * out_bpp;
  }
  if (is_srgb || (flags & WPNG_READ_SKIP_GAMMA_CORRECTION)) gamma = -1.0;

  if (gamma >= 0.0) apply_gamma(width, height, bpp, bit_depth == 16, image_data, bytes_per_scanline, gamma);

  if (dec.data) free(dec.data);

  output->error = 0;

  output->data = image_data;
  output->size = bytes_per_scanline * height;
  output->bytes_per_scanline = bytes_per_scanline;
  output->width = width;
  output->height = height;
  output->gamma = gamma;
  output->bytes_per_pixel = bpp;
  output->is_16bit = bit_depth == 16;
  output->was_16bit = was_16bit;
  output->was_srgb = is_srgb;
}

#endif // WPNG_READ_INCLUDED
