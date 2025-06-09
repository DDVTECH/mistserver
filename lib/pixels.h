#include <stdint.h>
#include <string>

namespace PixFmt {
  enum aspectMode { STRETCH, CROP, LETTERBOX, PATTERN };

  aspectMode parseAspect(const std::string & aspectStr);

  enum scaleMode { INTEGER, BILINEAR, NEAREST };

  scaleMode parseScaling(const std::string & scaleStr);

  class SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(size_t w, size_t h) : width(w), height(h) {}
      size_t width{0};
      size_t height{0};
      inline size_t bytes() const { return 0; }
  };
} // namespace PixFmt

namespace PixFmtY {
  /// Represents a single Y pixel (1 byte = 1 pixel).
  /// Y format stores luma (Y) samples only.
  struct Pixels {
      uint8_t y;
  };

  /// Source pixel matrix
  class SrcMatrix : public PixFmt::SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(void *ptr, size_t w, size_t h) : PixFmt::SrcMatrix(w, h), pix((Pixels *)ptr) {}
      Pixels *pix{0};
      inline size_t bytes() const { return 1; }
  };

} // namespace PixFmtY

namespace PixFmtYA {
  /// Represents a single Y/A pixel (2 bytes = 1 pixel).
  /// YA format stores luma (Y) and alpha (A) samples.
  struct Pixels {
      uint8_t y;
      uint8_t a;
  };

  /// Source pixel matrix
  class SrcMatrix : public PixFmt::SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(void *ptr, size_t w, size_t h) : PixFmt::SrcMatrix(w, h), pix((Pixels *)ptr) {}
      Pixels *pix{0};
      inline size_t bytes() const { return 2; }
  };

} // namespace PixFmtYA

namespace PixFmtRGB {
  /// Represents a single R/G/B pixel (3 bytes = 1 pixel).
  /// RGB format stores red (R), green (G) and blue (B) samples.
  struct Pixels {
      uint8_t r;
      uint8_t g;
      uint8_t b;

      inline uint8_t Y() const { return ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16; }
      inline uint8_t U() const { return ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128; }
      inline uint8_t V() const { return ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128; }
  };

  /// Source pixel matrix
  class SrcMatrix : public PixFmt::SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(void *ptr, size_t w, size_t h) : PixFmt::SrcMatrix(w, h), pix((Pixels *)ptr) {}
      Pixels *pix{0};
      inline size_t bytes() const { return 3; }
  };

} // namespace PixFmtRGB

namespace PixFmtRGBA {
  /// Represents a single RGBA pixel (4 bytes = 1 pixel).
  /// RGB format stores red (R), green (G), blue (B) and alpha (A) samples.
  struct Pixels {
      uint8_t r;
      uint8_t g;
      uint8_t b;
      uint8_t a;

      inline uint8_t Y() const { return ((66 * r + 129 * g + 25 * b + 128) >> 8) + 16; }
      inline uint8_t U() const { return ((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128; }
      inline uint8_t V() const { return ((112 * r - 94 * g - 18 * b + 128) >> 8) + 128; }
  };

  /// Source pixel matrix
  class SrcMatrix : public PixFmt::SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(void *ptr, size_t w, size_t h) : PixFmt::SrcMatrix(w, h), pix((Pixels *)ptr) {}
      Pixels *pix{0};
      inline size_t bytes() const { return 4; }
  };

} // namespace PixFmtRGBA

namespace PixFmtUYVY {

  /// Represents a single UYVY pixel pair (4 bytes = 2 pixels).
  /// UYVY format stores chroma (U,V) shared between two adjacent luma (Y) samples.
  /// Luma scales from 16 (black) to 240 (white)
  struct Pixels {
      uint8_t u; ///< U chroma component (shared)
      uint8_t y1; ///< First luma sample
      uint8_t v; ///< V chroma component (shared)
      uint8_t y2; ///< Second luma sample

      /// Returns true if the pixels are black
      inline bool isBlack() { return u == 128 && y1 == 16 && v == 128 && y2 == 16; }

      /// Returns true if the pixels are greyscale
      inline bool isGrey() { return u == 128 && v == 128; }

      /// Sets the pixels to black
      inline void clear() {
        u = v = 128;
        y1 = y2 = 16;
      }

      /// Sets the pixels to greyscale
      inline void uncolor() { u = v = 128; }
  };

  /// Destination pixel matrix
  struct DestMatrix {
      Pixels *pix; ///< Pointer to raw pixels
      size_t totWidth; ///< Total width
      size_t totHeight; ///< Total height
      size_t cellX; ///< cell X start
      size_t cellY; ///< cell Y start
      size_t cellWidth; ///< cell width
      size_t cellHeight; ///< cell height
      size_t blankT; ///< blank space on top
      size_t blankB; ///< blank space on bottom
      size_t blankL; ///< blank space on left
      size_t blankR; ///< blank space on right
      PixFmt::aspectMode aspect;
      PixFmt::scaleMode scale;
      void blitFromPtr(const Pixels *ptr);
      void blitToPtr(Pixels *ptr);
      bool blacken();
      bool greyscale();
  };

  /// Source pixel matrix
  class SrcMatrix : public PixFmt::SrcMatrix {
    public:
      SrcMatrix() {}
      SrcMatrix(void *ptr, size_t w, size_t h) : PixFmt::SrcMatrix(w, h), pix((Pixels *)ptr) {}
      Pixels *pix{0};
      inline size_t bytes() const { return 4; }
  };

  void calculateScaling(const size_t W, const size_t H, PixFmtUYVY::DestMatrix & L, size_t & scaleWidth,
                        size_t & scaleHeight, int16_t & offsetX, int16_t & offsetY, size_t & intScale, size_t & cropL,
                        size_t & cropR, size_t & cropU, size_t & cropD);

  /// Copies UYVY image src to fit within DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const SrcMatrix & src, DestMatrix & L);

  /// Returns byte size for a UTF-8 character at given offset.
  size_t utf8CodeSize(const std::string & txt, size_t offset = 0);

  /// Calculates length in characters for a UTF-8 string
  size_t utf8Len(const std::string & txt);

  /// Writes a single UTF-8 code point by modifying the luma components. Chroma is not altered.
  /// Increments offset by the number of bytes used in the UTF-8 string txt.
  /// If a code point is not printable, only increments offset to next code point in the string.
  /// Returns true if a letter was written.
  bool writeCodePoint(const std::string & txt, size_t & offset, DestMatrix & L, size_t X, size_t Y);

  /// Writes a line of UTF-8 text to fit within the given destination.
  void writeText(DestMatrix & L, const std::string & txt);

} // namespace PixFmtUYVY

namespace PixFmt {
  /// Copies Y image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtY::SrcMatrix & src, PixFmtUYVY::DestMatrix & L);

  /// Copies YA image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtYA::SrcMatrix & src, PixFmtUYVY::DestMatrix & L);

  /// Copies RGB image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtRGB::SrcMatrix & src, PixFmtUYVY::DestMatrix & L);

  /// Copies RGBA image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtRGBA::SrcMatrix & src, PixFmtUYVY::DestMatrix & L);

} // namespace PixFmt
