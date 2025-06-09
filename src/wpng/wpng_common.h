#ifndef WPNG_COMMON_INCLUDED
#define WPNG_COMMON_INCLUDED

// you probably want:
// wpng_write.h
// wpng_read.h

#include <math.h>
#include <stdint.h>

static inline uint8_t paeth_get_ref_raw(int16_t left, int16_t up, int16_t upleft) {
  int16_t lin = left + up - upleft;
  int16_t diff_left = abs(lin - left);
  int16_t diff_up = abs(lin - up);
  int16_t diff_upleft = abs(lin - upleft);

  int16_t ref = 0;
  if (diff_left <= diff_up && diff_left <= diff_upleft)
    ref = left;
  else if (diff_up <= diff_upleft)
    ref = up;
  else
    ref = upleft;

  return ref;
}
static inline uint8_t paeth_get_ref(uint8_t *image_data, uint32_t bytes_per_scanline, uint32_t x, uint32_t y, uint8_t bpp) {
  int16_t left = x >= bpp ? image_data[y * bytes_per_scanline + x - bpp] : 0;
  int16_t up = y > 0 ? image_data[(y - 1) * bytes_per_scanline + x] : 0;
  int16_t upleft = y > 0 && x >= bpp ? image_data[(y - 1) * bytes_per_scanline + x - bpp] : 0;

  return paeth_get_ref_raw(left, up, upleft);
}

#endif // WPNG_COMMON_INCLUDED
