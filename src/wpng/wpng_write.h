#ifndef WPNG_WRITE_INCLUDED
#define WPNG_WRITE_INCLUDED

// you probably want:
// static byte_buffer wpng_write(uint32_t width, uint32_t height, uint8_t bpp, uint8_t is_16bit, uint8_t * image_data, size_t bytes_per_scanline, uint32_t flags, int8_t compression_quality)

#include "buffers.h"
#include "deflate.h"
#include "wpng_common.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>

inline static int luma_compare(const void *_a, const void *_b) {
  uint32_t a = (*((uint32_t *)_a)) >> 8;
  uint32_t b = (*((uint32_t *)_b)) >> 8;
  uint32_t luma_a = (a & 0xFF) * 2 + (a >> 16) * 3 + ((a >> 8) & 0xFF) * 5;
  uint32_t luma_b = (b & 0xFF) * 2 + (b >> 16) * 3 + ((b >> 8) & 0xFF) * 5;
  return luma_a > luma_b ? 1 : luma_a < luma_b ? -1 : 0;
}

inline static uint32_t pal_val_expand(uint32_t val, uint8_t bpp) {
  if (bpp == 1) {
    val |= val << 8;
    val |= val << 16;
    val |= 0xFF;
  }
  if (bpp == 2) {
    val |= val >> 8 << 16;
    val |= val >> 8 << 24;
  }
  if (bpp == 3) {
    val <<= 8;
    val |= 0xFF;
  }
  return val;
}

// image must be 8-bit y, ya, rgb, or rgba. does not support 16-bit.
// returns null on failure, pointer on success
// `pal` must have space for 256 entries
static uint8_t *palettize(uint32_t width, uint32_t height, uint8_t bpp, uint8_t *image_data, size_t bytes_per_scanline,
                          size_t *size, uint32_t *pal, uint32_t *pal_count, uint32_t *arg_depth) {
  uint32_t palette[256] = {0};
  uint32_t palette_i = 0;

  for (size_t y = 0; y < height; y += 1) {
    for (size_t x = 0; x < width; x += 1) {
      uint32_t val = 0;
      for (size_t j = 0; j < bpp; j += 1) {
        val <<= 8;
        val |= image_data[y * bytes_per_scanline + x * bpp + j];
      }
      val = pal_val_expand(val, bpp);

      size_t index = 0;
      while (index < palette_i && palette[index] != val) index += 1;

      if (index == palette_i || palette_i == 0) {
        if (palette_i == 256)
          return 0;
        else {
          palette[palette_i] = val;
          palette_i += 1;
        }
      }
    }
  }
  // sort palette by luma to help png's byte filter compress better
  if (palette_i > 0)
    qsort(palette, palette_i, sizeof(uint32_t), luma_compare);
  else
    return 0;

  // for (size_t i = 0; i < palette_i; i += 1)
  //     printf("%08X\n", palette[i]);

  uint8_t depth = 0;
  if (palette_i <= 2)
    depth = 1;
  else if (palette_i <= 4)
    depth = 2;
  else if (palette_i <= 16)
    depth = 4;
  else if (palette_i <= 256)
    depth = 8;
  else
    return 0;

  assert(palette_i <= 256);

  size_t out_bps = (width * depth + 7) / 8;

  uint8_t *output = (uint8_t *)malloc(out_bps * height);
  assert(output);
  memset(output, 0, out_bps * height);

  for (size_t y = 0; y < height; y += 1) {
    for (size_t x = 0; x < width; x += 1) {
      uint32_t val = 0;
      for (size_t j = 0; j < bpp; j += 1) {
        val <<= 8;
        val |= image_data[y * bytes_per_scanline + x * bpp + j];
      }
      val = pal_val_expand(val, bpp);

      size_t index = 0;
      while (index < palette_i && palette[index] != val) index += 1;

      assert(index < palette_i);

      uint8_t n = x % (8 / depth);
      size_t i = x / (8 / depth);
      uint32_t orval = index << (uint32_t)(8 - ((n + 1) * depth));
      assert(orval < 256);
      output[y * out_bps + i] |= orval;
    }
  }

  if (size) *size = out_bps * height;
  if (arg_depth) *arg_depth = depth;
  if (pal_count) *pal_count = palette_i;
  if (pal) {
    for (size_t i = 0; i < palette_i; i += 1) pal[i] = palette[i];
  }

  return output;
}

enum {
  WPNG_WRITE_ALLOW_PALLETIZATION = 1,
};
// image_data must refer to at least `bytes_per_scanline * height * bpp` bytes. if is_16bit is zero, bpp must be 1, 2,
// 3, or 4. if is_16bit is nonzero, bpp must be 2, 4, 6, or 8. compression_quality affects DEFLATE compression speed;
// lower numbers are faster, except 0, which is fastest (uses DEFLATE's `store` mode).
static byte_buffer wpng_write(uint32_t width, uint32_t height, uint8_t bpp, uint8_t is_16bit, uint8_t *image_data,
                              size_t bytes_per_scanline, uint32_t flags, int8_t compression_quality) {
  byte_buffer out;
  memset(&out, 0, sizeof(byte_buffer));

  bytes_push(&out, (const uint8_t *)"\x89\x50\x4E\x47\x0D\x0A\x1A\x0A", 8);

  // uint8_t * orig_image_data = image_data;
  // uint8_t orig_bpp = bpp;
  // size_t orig_bytes_per_scanline = bytes_per_scanline;

  size_t palettized_size = 0;
  uint32_t palette[256] = {0};
  uint32_t pal_count = 0;
  uint32_t pal_depth = 0;
  uint8_t *palettized = !(flags & WPNG_WRITE_ALLOW_PALLETIZATION) || is_16bit
    ? 0
    : palettize(width, height, bpp, image_data, bytes_per_scanline, &palettized_size, palette, &pal_count, &pal_depth);

  // write header chunk

  bytes_push_int(&out, byteswap_int(13, 4), 4);
  size_t chunk_start = out.len;
  bytes_push(&out, (const uint8_t *)"IHDR", 4);
  bytes_push_int(&out, byteswap_int(width, 4), 4);
  bytes_push_int(&out, byteswap_int(height, 4), 4);

  if (!palettized) {
    byte_push(&out, is_16bit ? 16 : 8);
    uint8_t components = is_16bit ? bpp / 2 : bpp;
    byte_push(&out, components == 1 ? 0 : components == 2 ? 4 : components == 3 ? 2 : components == 4 ? 6 : 0);
  } else {
    byte_push(&out, pal_depth);
    byte_push(&out, 3);
    image_data = palettized;
    bpp = 1;
    bytes_per_scanline = palettized_size / height;
  }

  byte_push(&out, 0); // compression method (deflate)
  byte_push(&out, 0); // filter method (adaptive x5)
  byte_push(&out, 0); // interlacing method (none)

  size_t chunk_size = out.len - chunk_start;
  bytes_push_int(&out, byteswap_int(defl_compute_crc32(&out.data[chunk_start], chunk_size, 0), 4), 4);

  bytes_push(&out, (uint8_t *)"\0\0\0\1sRGB\0\xAE\xCE\x1C\xE9", 13); // sRGB

  if (palettized) {
    // palette chunk
    bytes_push_int(&out, byteswap_int(pal_count * 3, 4), 4);

    chunk_start = out.len;
    bytes_push(&out, (const uint8_t *)"PLTE", 4);
    for (size_t i = 0; i < pal_count; i++) {
      byte_push(&out, palette[i] >> 24);
      byte_push(&out, palette[i] >> 16);
      byte_push(&out, palette[i] >> 8);
    }

    size_t chunk_size = out.len - chunk_start;
    bytes_push_int(&out, byteswap_int(defl_compute_crc32(&out.data[chunk_start], chunk_size, 0), 4), 4);

    // transparency chunk
    bytes_push_int(&out, byteswap_int(pal_count, 4), 4);

    chunk_start = out.len;
    bytes_push(&out, (const uint8_t *)"tRNS", 4);
    for (size_t i = 0; i < pal_count; i++) byte_push(&out, palette[i]);

    chunk_size = out.len - chunk_start;
    bytes_push_int(&out, byteswap_int(defl_compute_crc32(&out.data[chunk_start], chunk_size, 0), 4), 4);
  }

  // write IDAT chunks
  // first, collect pixel data
  byte_buffer pixel_data;
  memset(&pixel_data, 0, sizeof(pixel_data));
  uint64_t num_unfiltered = 0;
  for (size_t y = 0; y < height; y += 1) {
    // pick a filter based on the sum-of-absolutes heuristic
    size_t start = bytes_per_scanline * y;

    uint64_t sum_abs_null = 0;
    uint64_t sum_abs_left = 0;
    uint64_t sum_abs_top = 0;
    uint64_t sum_abs_avg = 0;
    uint64_t sum_abs_paeth = 0;

    uint64_t hit_vals[256] = {0};
    uint8_t most_common_val = 0;
    uint64_t most_common_count = 0;

    if (compression_quality > 0) {
      for (size_t x = 0; x < bytes_per_scanline; x++) {
        uint8_t val = image_data[start + x];
        hit_vals[val] += 1;
        if (hit_vals[val] > most_common_count) {
          most_common_count = hit_vals[val];
          most_common_val = val;
        }
      }

      for (size_t x = 0; x < bytes_per_scanline; x++) {
        sum_abs_null += abs((int32_t)(int8_t)(image_data[start + x] - most_common_val));

        uint16_t up = y > 0 ? image_data[start - bytes_per_scanline + x] : 0;
        uint16_t left = x >= bpp ? image_data[start + x - bpp] : 0;
        uint8_t avg = (up + left) / 2;
        sum_abs_avg += abs((int32_t)(image_data[start + x]) - (int32_t)(avg));

        sum_abs_left += abs((int32_t)(image_data[start + x]) - (int32_t)left);

        sum_abs_top += abs((int32_t)(image_data[start + x]) - (int32_t)up);

        uint8_t ref = paeth_get_ref(image_data, bytes_per_scanline, x, y, bpp);
        sum_abs_paeth += abs((int32_t)(image_data[start + x]) - (int32_t)(ref));
      }
    }

    if (y == 0) sum_abs_top = -1;

    // bias in favor of storing unfiltered if enough (25%) earlier scanlines are stored unfiltered
    // this helps with deflate's lz77 pass
    if (num_unfiltered > y / height / 4) sum_abs_null /= 3;

    if (compression_quality == 0 ||
        (sum_abs_null <= sum_abs_left && sum_abs_null <= sum_abs_top && sum_abs_null <= sum_abs_avg && sum_abs_null <= sum_abs_paeth)) {
      num_unfiltered += 1;
      // puts("picked filter mode 0");
      byte_push(&pixel_data, 0); // no filter
      bytes_push(&pixel_data, &image_data[start], bytes_per_scanline);
    } else if (sum_abs_left <= sum_abs_top && sum_abs_left <= sum_abs_avg && sum_abs_left <= sum_abs_paeth) {
      // puts("picked filter mode 1");
      byte_push(&pixel_data, 1); // left filter
      for (size_t x = 0; x < bpp; x++) byte_push(&pixel_data, image_data[start + x]);
      for (size_t x = bpp; x < bytes_per_scanline; x++)
        byte_push(&pixel_data, image_data[start + x] - image_data[start + x - bpp]);
    } else if (sum_abs_top <= sum_abs_avg && sum_abs_top <= sum_abs_paeth) {
      // puts("picked filter mode 2");
      byte_push(&pixel_data, 2); // top filter
      for (size_t x = 0; x < bytes_per_scanline; x++)
        byte_push(&pixel_data, image_data[start + x] - image_data[start - bytes_per_scanline + x]);
    } else if (sum_abs_avg <= sum_abs_paeth) {
      // puts("picked filter mode 3");
      byte_push(&pixel_data, 3); // avg filter
      for (size_t x = 0; x < bytes_per_scanline; x++) {
        uint16_t up = y > 0 ? image_data[start - bytes_per_scanline + x] : 0;
        uint16_t left = x >= bpp ? image_data[start + x - bpp] : 0;
        uint8_t avg = (up + left) / 2;
        byte_push(&pixel_data, image_data[start + x] - avg);
      }
    } else {
      // puts("picked filter mode 4");
      byte_push(&pixel_data, 4); // paeth filter
      for (size_t x = 0; x < bytes_per_scanline; x++) {
        uint8_t ref = paeth_get_ref(image_data, bytes_per_scanline, x, y, bpp);
        byte_push(&pixel_data, image_data[start + x] - ref);
      }
    }
  }

  bit_buffer pixel_data_comp = do_deflate(pixel_data.data, pixel_data.len, compression_quality, 1);

  bytes_push_int(&out, byteswap_int(pixel_data_comp.buffer.len, 4), 4);
  chunk_start = out.len;
  bytes_push(&out, (const uint8_t *)"IDAT", 4);
  bytes_push(&out, pixel_data_comp.buffer.data, pixel_data_comp.buffer.len);
  chunk_size = out.len - chunk_start;
  bytes_push_int(&out, byteswap_int(defl_compute_crc32(&out.data[chunk_start], chunk_size, 0), 4), 4);

  if (pixel_data_comp.buffer.data) free(pixel_data_comp.buffer.data);

  bytes_push_int(&out, 0, 4);
  bytes_push(&out, (const uint8_t *)"IEND\xAE\x42\x60\x82", 8);

  free(palettized);

  return out;
}

#endif // WPNG_WRITE_INCLUDED
