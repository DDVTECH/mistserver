#include "pixels.h"

#include "defines.h"

/// Public domain IBM 8x8 pixel font for characters 0x20-0x7E
uint8_t font8x8_basic[95][8] = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0020 (space)
  {0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00}, // U+0021 (!)
  {0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0022 (")
  {0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00}, // U+0023 (#)
  {0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00}, // U+0024 ($)
  {0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00}, // U+0025 (%)
  {0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00}, // U+0026 (&)
  {0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0027 (')
  {0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00}, // U+0028 (()
  {0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00}, // U+0029 ())
  {0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00}, // U+002A (*)
  {0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00}, // U+002B (+)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+002C (,)
  {0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00}, // U+002D (-)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+002E (.)
  {0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00}, // U+002F (/)
  {0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00}, // U+0030 (0)
  {0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00}, // U+0031 (1)
  {0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00}, // U+0032 (2)
  {0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00}, // U+0033 (3)
  {0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00}, // U+0034 (4)
  {0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00}, // U+0035 (5)
  {0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00}, // U+0036 (6)
  {0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00}, // U+0037 (7)
  {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+0038 (8)
  {0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00}, // U+0039 (9)
  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00}, // U+003A (:)
  {0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06}, // U+003B (;)
  {0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00}, // U+003C (<)
  {0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00}, // U+003D (=)
  {0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00}, // U+003E (>)
  {0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00}, // U+003F (?)
  {0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00}, // U+0040 (@)
  {0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00}, // U+0041 (A)
  {0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00}, // U+0042 (B)
  {0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00}, // U+0043 (C)
  {0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00}, // U+0044 (D)
  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00}, // U+0045 (E)
  {0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00}, // U+0046 (F)
  {0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00}, // U+0047 (G)
  {0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00}, // U+0048 (H)
  {0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0049 (I)
  {0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00}, // U+004A (J)
  {0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00}, // U+004B (K)
  {0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00}, // U+004C (L)
  {0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00}, // U+004D (M)
  {0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00}, // U+004E (N)
  {0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00}, // U+004F (O)
  {0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00}, // U+0050 (P)
  {0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00}, // U+0051 (Q)
  {0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00}, // U+0052 (R)
  {0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00}, // U+0053 (S)
  {0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0054 (T)
  {0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00}, // U+0055 (U)
  {0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0056 (V)
  {0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00}, // U+0057 (W)
  {0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00}, // U+0058 (X)
  {0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+0059 (Y)
  {0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00}, // U+005A (Z)
  {0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00}, // U+005B ([)
  {0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00}, // U+005C (\)
  {0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00}, // U+005D (])
  {0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00}, // U+005E (^)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF}, // U+005F (_)
  {0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+0060 (`)
  {0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00}, // U+0061 (a)
  {0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00}, // U+0062 (b)
  {0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00}, // U+0063 (c)
  {0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00}, // U+0064 (d)
  {0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00}, // U+0065 (e)
  {0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00}, // U+0066 (f)
  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0067 (g)
  {0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00}, // U+0068 (h)
  {0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+0069 (i)
  {0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E}, // U+006A (j)
  {0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00}, // U+006B (k)
  {0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+006C (l)
  {0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00}, // U+006D (m)
  {0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00}, // U+006E (n)
  {0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+006F (o)
  {0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+0070 (p)
  {0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78}, // U+0071 (q)
  {0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00}, // U+0072 (r)
  {0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00}, // U+0073 (s)
  {0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00}, // U+0074 (t)
  {0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00}, // U+0075 (u)
  {0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00}, // U+0076 (v)
  {0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00}, // U+0077 (w)
  {0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00}, // U+0078 (x)
  {0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+0079 (y)
  {0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00}, // U+007A (z)
  {0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00}, // U+007B ({)
  {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+007C (|)
  {0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00}, // U+007D (})
  {0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+007E (~)
};

/// Public domain IBM 8x8 pixel font for extended latin characters 0xA0-0xFF
uint8_t font8x8_ext_latin[96][8] = {
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+00A0 (no break space)
  {0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x18, 0x00}, // U+00A1 (inverted !)
  {0x18, 0x18, 0x7E, 0x03, 0x03, 0x7E, 0x18, 0x18}, // U+00A2 (dollarcents)
  {0x1C, 0x36, 0x26, 0x0F, 0x06, 0x67, 0x3F, 0x00}, // U+00A3 (pound sterling)
  {0x00, 0x00, 0x63, 0x3E, 0x36, 0x3E, 0x63, 0x00}, // U+00A4 (currency mark)
  {0x33, 0x33, 0x1E, 0x3F, 0x0C, 0x3F, 0x0C, 0x0C}, // U+00A5 (yen)
  {0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00}, // U+00A6 (broken pipe)
  {0x7C, 0xC6, 0x1C, 0x36, 0x36, 0x1C, 0x33, 0x1E}, // U+00A7 (paragraph)
  {0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+00A8 (diaeresis)
  {0x3C, 0x42, 0x99, 0x85, 0x85, 0x99, 0x42, 0x3C}, // U+00A9 (copyright symbol)
  {0x3C, 0x36, 0x36, 0x7C, 0x00, 0x00, 0x00, 0x00}, // U+00AA (superscript a)
  {0x00, 0xCC, 0x66, 0x33, 0x66, 0xCC, 0x00, 0x00}, // U+00AB (<<)
  {0x00, 0x00, 0x00, 0x3F, 0x30, 0x30, 0x00, 0x00}, // U+00AC (gun pointing left)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+00AD (soft hyphen)
  {0x3C, 0x42, 0x9D, 0xA5, 0x9D, 0xA5, 0x42, 0x3C}, // U+00AE (registered symbol)
  {0x7E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+00AF (macron)
  {0x1C, 0x36, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00}, // U+00B0 (degree)
  {0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x7E, 0x00}, // U+00B1 (plusminus)
  {0x1C, 0x30, 0x18, 0x0C, 0x3C, 0x00, 0x00, 0x00}, // U+00B2 (superscript 2)
  {0x1C, 0x30, 0x18, 0x30, 0x1C, 0x00, 0x00, 0x00}, // U+00B2 (superscript 3)
  {0x18, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // U+00B2 (aigu)
  {0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x03}, // U+00B5 (mu)
  {0xFE, 0xDB, 0xDB, 0xDE, 0xD8, 0xD8, 0xD8, 0x00}, // U+00B6 (pilcrow)
  {0x00, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00}, // U+00B7 (central dot)
  {0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x30, 0x1E}, // U+00B8 (cedille)
  {0x08, 0x0C, 0x08, 0x1C, 0x00, 0x00, 0x00, 0x00}, // U+00B9 (superscript 1)
  {0x1C, 0x36, 0x36, 0x1C, 0x00, 0x00, 0x00, 0x00}, // U+00BA (superscript 0)
  {0x00, 0x33, 0x66, 0xCC, 0x66, 0x33, 0x00, 0x00}, // U+00BB (>>)
  {0xC3, 0x63, 0x33, 0xBD, 0xEC, 0xF6, 0xF3, 0x03}, // U+00BC (1/4)
  {0xC3, 0x63, 0x33, 0x7B, 0xCC, 0x66, 0x33, 0xF0}, // U+00BD (1/2)
  {0x03, 0xC4, 0x63, 0xB4, 0xDB, 0xAC, 0xE6, 0x80}, // U+00BE (3/4)
  {0x0C, 0x00, 0x0C, 0x06, 0x03, 0x33, 0x1E, 0x00}, // U+00BF (inverted ?)
  {0x07, 0x00, 0x1C, 0x36, 0x63, 0x7F, 0x63, 0x00}, // U+00C0 (A grave)
  {0x70, 0x00, 0x1C, 0x36, 0x63, 0x7F, 0x63, 0x00}, // U+00C1 (A aigu)
  {0x1C, 0x36, 0x00, 0x3E, 0x63, 0x7F, 0x63, 0x00}, // U+00C2 (A circumflex)
  {0x6E, 0x3B, 0x00, 0x3E, 0x63, 0x7F, 0x63, 0x00}, // U+00C3 (A ~)
  {0x63, 0x1C, 0x36, 0x63, 0x7F, 0x63, 0x63, 0x00}, // U+00C4 (A umlaut)
  {0x0C, 0x0C, 0x00, 0x1E, 0x33, 0x3F, 0x33, 0x00}, // U+00C5 (A ring)
  {0x7C, 0x36, 0x33, 0x7F, 0x33, 0x33, 0x73, 0x00}, // U+00C6 (AE)
  {0x1E, 0x33, 0x03, 0x33, 0x1E, 0x18, 0x30, 0x1E}, // U+00C7 (C cedille)
  {0x07, 0x00, 0x3F, 0x06, 0x1E, 0x06, 0x3F, 0x00}, // U+00C8 (E grave)
  {0x38, 0x00, 0x3F, 0x06, 0x1E, 0x06, 0x3F, 0x00}, // U+00C9 (E aigu)
  {0x0C, 0x12, 0x3F, 0x06, 0x1E, 0x06, 0x3F, 0x00}, // U+00CA (E circumflex)
  {0x36, 0x00, 0x3F, 0x06, 0x1E, 0x06, 0x3F, 0x00}, // U+00CB (E umlaut)
  {0x07, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00CC (I grave)
  {0x38, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00CD (I aigu)
  {0x0C, 0x12, 0x00, 0x1E, 0x0C, 0x0C, 0x1E, 0x00}, // U+00CE (I circumflex)
  {0x33, 0x00, 0x1E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00CF (I umlaut)
  {0x3F, 0x66, 0x6F, 0x6F, 0x66, 0x66, 0x3F, 0x00}, // U+00D0 (Eth)
  {0x3F, 0x00, 0x33, 0x37, 0x3F, 0x3B, 0x33, 0x00}, // U+00D1 (N ~)
  {0x0E, 0x00, 0x18, 0x3C, 0x66, 0x3C, 0x18, 0x00}, // U+00D2 (O grave)
  {0x70, 0x00, 0x18, 0x3C, 0x66, 0x3C, 0x18, 0x00}, // U+00D3 (O aigu)
  {0x3C, 0x66, 0x18, 0x3C, 0x66, 0x3C, 0x18, 0x00}, // U+00D4 (O circumflex)
  {0x6E, 0x3B, 0x00, 0x3E, 0x63, 0x63, 0x3E, 0x00}, // U+00D5 (O ~)
  {0xC3, 0x18, 0x3C, 0x66, 0x66, 0x3C, 0x18, 0x00}, // U+00D6 (O umlaut)
  {0x00, 0x36, 0x1C, 0x08, 0x1C, 0x36, 0x00, 0x00}, // U+00D7 (multiplicative x)
  {0x5C, 0x36, 0x73, 0x7B, 0x6F, 0x36, 0x1D, 0x00}, // U+00D8 (O stroke)
  {0x0E, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U+00D9 (U grave)
  {0x70, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U+00DA (U aigu)
  {0x3C, 0x66, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x00}, // U+00DB (U circumflex)
  {0x33, 0x00, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x00}, // U+00DC (U umlaut)
  {0x70, 0x00, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x00}, // U+00DD (Y aigu)
  {0x0F, 0x06, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x0F}, // U+00DE (Thorn)
  {0x00, 0x1E, 0x33, 0x1F, 0x33, 0x1F, 0x03, 0x03}, // U+00DF (beta)
  {0x07, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x7E, 0x00}, // U+00E0 (a grave)
  {0x38, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x7E, 0x00}, // U+00E1 (a aigu)
  {0x7E, 0xC3, 0x3C, 0x60, 0x7C, 0x66, 0xFC, 0x00}, // U+00E2 (a circumflex)
  {0x6E, 0x3B, 0x1E, 0x30, 0x3E, 0x33, 0x7E, 0x00}, // U+00E3 (a ~)
  {0x33, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x7E, 0x00}, // U+00E4 (a umlaut)
  {0x0C, 0x0C, 0x1E, 0x30, 0x3E, 0x33, 0x7E, 0x00}, // U+00E5 (a ring)
  {0x00, 0x00, 0xFE, 0x30, 0xFE, 0x33, 0xFE, 0x00}, // U+00E6 (ae)
  {0x00, 0x00, 0x1E, 0x03, 0x03, 0x1E, 0x30, 0x1C}, // U+00E7 (c cedille)
  {0x07, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // U+00E8 (e grave)
  {0x38, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // U+00E9 (e aigu)
  {0x7E, 0xC3, 0x3C, 0x66, 0x7E, 0x06, 0x3C, 0x00}, // U+00EA (e circumflex)
  {0x33, 0x00, 0x1E, 0x33, 0x3F, 0x03, 0x1E, 0x00}, // U+00EB (e umlaut)
  {0x07, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00EC (i grave)
  {0x1C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00ED (i augu)
  {0x3E, 0x63, 0x1C, 0x18, 0x18, 0x18, 0x3C, 0x00}, // U+00EE (i circumflex)
  {0x33, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00}, // U+00EF (i umlaut)
  {0x1B, 0x0E, 0x1B, 0x30, 0x3E, 0x33, 0x1E, 0x00}, // U+00F0 (eth)
  {0x00, 0x1F, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x00}, // U+00F1 (n ~)
  {0x00, 0x07, 0x00, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+00F2 (o grave)
  {0x00, 0x38, 0x00, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+00F3 (o aigu)
  {0x1E, 0x33, 0x00, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+00F4 (o circumflex)
  {0x6E, 0x3B, 0x00, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+00F5 (o ~)
  {0x00, 0x33, 0x00, 0x1E, 0x33, 0x33, 0x1E, 0x00}, // U+00F6 (o umlaut)
  {0x18, 0x18, 0x00, 0x7E, 0x00, 0x18, 0x18, 0x00}, // U+00F7 (division)
  {0x00, 0x60, 0x3C, 0x76, 0x7E, 0x6E, 0x3C, 0x06}, // U+00F8 (o stroke)
  {0x00, 0x07, 0x00, 0x33, 0x33, 0x33, 0x7E, 0x00}, // U+00F9 (u grave)
  {0x00, 0x38, 0x00, 0x33, 0x33, 0x33, 0x7E, 0x00}, // U+00FA (u aigu)
  {0x1E, 0x33, 0x00, 0x33, 0x33, 0x33, 0x7E, 0x00}, // U+00FB (u circumflex)
  {0x00, 0x33, 0x00, 0x33, 0x33, 0x33, 0x7E, 0x00}, // U+00FC (u umlaut)
  {0x00, 0x38, 0x00, 0x33, 0x33, 0x3E, 0x30, 0x1F}, // U+00FD (y aigu)
  {0x00, 0x00, 0x06, 0x3E, 0x66, 0x3E, 0x06, 0x00}, // U+00FE (thorn)
  {0x00, 0x33, 0x00, 0x33, 0x33, 0x3E, 0x30, 0x1F} // U+00FF (y umlaut)
};

PixFmt::aspectMode PixFmt::parseAspect(const std::string & aspectStr) {
  if (aspectStr == "crop") { return PixFmt::CROP; }
  if (aspectStr == "stretch") { return PixFmt::STRETCH; }
  if (aspectStr == "pattern") { return PixFmt::PATTERN; }
  return PixFmt::LETTERBOX;
}

PixFmt::scaleMode PixFmt::parseScaling(const std::string & scaleStr) {
  if (scaleStr == "nearest") { return PixFmt::NEAREST; }
  if (scaleStr == "bilinear") { return PixFmt::BILINEAR; }
  return PixFmt::INTEGER;
}

namespace PixFmtUYVY {

  void DestMatrix::blitFromPtr(const Pixels *ptr) {
    size_t rowSize = totWidth / 2;
    size_t start = (cellY + blankT) * rowSize + (cellX + blankL) / 2;
    size_t W = (cellWidth - blankL - blankR) / 2;
    size_t H = cellHeight - blankT - blankB;
    for (size_t x = 0; x < W; ++x) {
      for (size_t y = 0; y < H; ++y) { pix[start + x + y * rowSize] = ptr[x + y * W]; }
    }
  }

  void DestMatrix::blitToPtr(Pixels *ptr) {
    size_t rowSize = totWidth / 2;
    size_t start = (cellY + blankT) * rowSize + (cellX + blankL) / 2;
    size_t W = (cellWidth - blankL - blankR) / 2;
    size_t H = cellHeight - blankT - blankB;
    for (size_t x = 0; x < W; ++x) {
      for (size_t y = 0; y < H; ++y) { ptr[x + y * W] = pix[start + x + y * rowSize]; }
    }
  }

  /// Sets destination area to greyscale, returns true if any pixels were updated
  bool DestMatrix::greyscale() {
    bool changed = false;
    size_t gridPairsPerRow = totWidth / 2;
    size_t scalePairsPerRow = cellWidth / 2;
    for (size_t y = blankT; y < cellHeight - blankB; y++) {
      size_t gridRowStart = (cellY + y) * gridPairsPerRow + cellX / 2;
      for (size_t x = blankL / 2; x < scalePairsPerRow - blankR / 2; x++) {
        if (!pix[gridRowStart + x].isGrey()) {
          pix[gridRowStart + x].uncolor();
          changed = true;
        }
      }
    }
    return changed;
  }

  /// Sets destination area to black, returns true if any pixels were updated
  bool DestMatrix::blacken() {
    bool changed = false;
    size_t gridPairsPerRow = totWidth / 2;
    size_t scalePairsPerRow = cellWidth / 2;
    for (size_t y = blankT; y < cellHeight - blankB; y++) {
      size_t gridRowStart = (cellY + y) * gridPairsPerRow + cellX / 2;
      for (size_t x = blankL / 2; x < scalePairsPerRow - blankR / 2; x++) {
        if (!pix[gridRowStart + x].isBlack()) {
          pix[gridRowStart + x].clear();
          changed = true;
        }
      }
    }
    return changed;
  }

  void calculateScaling(const size_t W, const size_t H, PixFmtUYVY::DestMatrix & L, size_t & scaleWidth,
                        size_t & scaleHeight, int16_t & offsetX, int16_t & offsetY, size_t & intScale, size_t & cropL,
                        size_t & cropR, size_t & cropU, size_t & cropD) {
    // Handle aspect ratio preservation
    if (L.scale == PixFmt::INTEGER || L.aspect == PixFmt::LETTERBOX || L.aspect == PixFmt::CROP) {
      float srcAspect = (float)W / H;
      float cellAspect = (float)L.cellWidth / L.cellHeight;

      if (L.aspect == PixFmt::LETTERBOX) {
        // Letterbox: Scale to fit inside cell, add black bars as needed
        if (L.scale == PixFmt::INTEGER) {
          // Integer scaler - divide by two while still too big
          scaleHeight = H;
          scaleWidth = W;
          while (scaleHeight > L.cellHeight || scaleWidth > L.cellWidth) {
            ++intScale;
            scaleHeight = H / intScale;
            scaleWidth = W / intScale;
          }
        } else {
          if (srcAspect > cellAspect) {
            // Source is wider - fit width
            scaleWidth = L.cellWidth;
            scaleHeight = (size_t)(L.cellWidth / srcAspect);
          } else {
            // Source is taller - fit height
            scaleHeight = L.cellHeight;
            scaleWidth = (size_t)(L.cellHeight * srcAspect);
          }
        }
      } else if (L.aspect == PixFmt::CROP || L.aspect == PixFmt::STRETCH) {
        // Crop: Extract center region from source to match cell aspect ratio
        if (L.scale == PixFmt::INTEGER) {
          // Integer scaler - divide by two while next step still big enough
          // (thus selecting the smallest integer scale that is still too big)
          scaleHeight = H;
          scaleWidth = W;
          while (H / (intScale + 1) >= L.cellHeight && W / (intScale + 1) >= L.cellWidth) {
            ++intScale;
            scaleHeight = H / intScale;
            scaleWidth = W / intScale;
          }
        } else {
          if (srcAspect > cellAspect) {
            // Source is wider - fit height
            scaleHeight = H;
            scaleWidth = (size_t)(H * srcAspect);
          } else {
            // Source is taller - fit width
            scaleWidth = W;
            scaleHeight = (size_t)(W / srcAspect);
          }
        }
      }
    }
    if (L.aspect == PixFmt::PATTERN) {
      scaleWidth = L.cellWidth;
      scaleHeight = L.cellHeight;
    }
    // Ensure even dimensions for UYVY alignment
    scaleWidth &= ~1;
    scaleHeight &= ~1;
    offsetY = (L.cellHeight - scaleHeight) / 2;
    offsetX = (L.cellWidth - scaleWidth) / 2;
    offsetX = (int16_t(offsetX / 2)) * 2;
    if (offsetY < 0) {
      cropU = -offsetY;
      cropU &= ~1;
      cropD = scaleHeight - L.cellHeight - cropU;
      L.blankT = L.blankB = 0;
    } else {
      L.blankT = offsetY;
      L.blankB = L.cellHeight - offsetY - scaleHeight;
    }
    if (offsetX < 0) {
      cropL = -offsetX;
      cropL &= ~1;
      cropR = scaleWidth - L.cellWidth - cropL;
      L.blankL = L.blankR = 0;
    } else {
      L.blankL = offsetX;
      L.blankR = L.cellWidth - offsetX - scaleWidth;
    }
  }

  /// Copies UYVY image src to fit within DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const SrcMatrix & src, DestMatrix & L) {
    // Calculate pixel pair dimensions (UYVY stores 2 pixels per 4-byte pair)
    size_t srcPairsPerRow = src.width / 2;
    size_t gridPairsPerRow = L.totWidth / 2;

    // Default scaling dimensions (may be adjusted for aspect ratio handling)
    size_t scaleWidth = L.cellWidth;
    size_t scaleHeight = L.cellHeight;
    int16_t offsetX = 0, offsetY = 0;
    size_t intScale = 1;
    size_t cropL = 0, cropR = 0, cropU = 0, cropD = 0;
    calculateScaling(src.width, src.height, L, scaleWidth, scaleHeight, offsetX, offsetY, intScale, cropL, cropR, cropU, cropD);

    // Calculate final grid position including any letterbox offsets
    int16_t destX = L.cellX + offsetX;
    int16_t destY = L.cellY + offsetY;
    int16_t destPairX = destX / 2;
    size_t scalePairsPerRow = scaleWidth / 2;

    if (L.aspect == PixFmt::PATTERN) {
      // Pattern fill, no scaling at all
      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) break; // Bounds check
        size_t srcY = y % src.height;
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;
        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destX + y < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) break; // Bounds check
          size_t srcX = x % src.width;
          L.pix[gridRowStart + x] = src.pix[srcY * srcPairsPerRow + srcX];
        }
      }
      return;
    }
    if (L.scale == PixFmt::BILINEAR) {
      uint32_t xRatio_fp = (uint32_t)((srcPairsPerRow << 16) / scalePairsPerRow);
      uint32_t yRatio_fp = (uint32_t)((src.height << 16) / scaleHeight);

      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        size_t destYPos = destY + y;
        if (destYPos >= L.totHeight) break; // Bounds check

        uint32_t srcY_fp = y * yRatio_fp;
        size_t y1 = srcY_fp >> 16;
        size_t y2 = std::min(y1 + 1, src.height - 1);
        uint32_t wy = (srcY_fp >> 8) & 0xFF;
        uint32_t iwy = 256 - wy;

        size_t gridRowStart = destYPos * gridPairsPerRow + destPairX;
        size_t row1 = y1 * srcPairsPerRow;
        size_t row2 = y2 * srcPairsPerRow;

        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destPairX + x < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) break; // Bounds check

          uint32_t srcX_fp = x * xRatio_fp;
          size_t x1 = srcX_fp >> 16;
          size_t x2 = std::min(x1 + 1, srcPairsPerRow - 1);
          uint32_t wx = (srcX_fp >> 8) & 0xFF;
          uint32_t iwx = 256 - wx;

          // Weights
          uint32_t w11 = iwx * iwy;
          uint32_t w12 = wx * iwy;
          uint32_t w21 = iwx * wy;
          uint32_t w22 = wx * wy;

          const Pixels & p11 = src.pix[row1 + x1];
          const Pixels & p12 = src.pix[row1 + x2];
          const Pixels & p21 = src.pix[row2 + x1];
          const Pixels & p22 = src.pix[row2 + x2];

          Pixels & destPx = L.pix[gridRowStart + x];
          destPx.u = (uint8_t)((p11.u * w11 + p12.u * w12 + p21.u * w21 + p22.u * w22 + 32768) >> 16);
          destPx.v = (uint8_t)((p11.v * w11 + p12.v * w12 + p21.v * w21 + p22.v * w22 + 32768) >> 16);
          destPx.y1 = (uint8_t)((p11.y1 * w11 + p12.y1 * w12 + p21.y1 * w21 + p22.y1 * w22 + 32768) >> 16);
          destPx.y2 = (uint8_t)((p11.y2 * w11 + p12.y2 * w12 + p21.y2 * w21 + p22.y2 * w22 + 32768) >> 16);
        }
      }
      return;
    }
    if (L.scale == PixFmt::NEAREST) {
      // Nearest neighbour: Fast, uses closest pixel without interpolation
      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) { break; } // Bounds check

        // Find nearest source row (integer math for speed)
        size_t srcY = (y * src.height) / scaleHeight;
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;

        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destPairX + x < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) { break; } // Bounds check

          // Find nearest source pixel pair and copy directly
          size_t srcX = (x * srcPairsPerRow) / scalePairsPerRow;
          L.pix[gridRowStart + x] = src.pix[srcY * srcPairsPerRow + srcX];
        }
      }
      return;
    }
    if (L.scale == PixFmt::INTEGER) {
      // Integer scaling, only divides by 2-multiples, quick and good quality but inflexible
      size_t div = (intScale * intScale);
      size_t rightSide = intScale / 2;
      size_t middlePixel = 0;
      if (intScale % 2) { middlePixel = rightSide; }

      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) { break; } // Bounds check

        // Find source row
        size_t srcY = y * intScale;
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;

        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destPairX + x < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) break; // Bounds check

          // Average the values of the pixels we represent with this pixel
          size_t srcX = x * intScale;
          if (intScale == 1) {
            L.pix[gridRowStart + x] = src.pix[srcY * srcPairsPerRow + srcX];
          } else {
            uint16_t U = 0, V = 0, Y1 = 0, Y2 = 0;
            for (size_t iY = 0; iY < intScale; ++iY) {
              for (size_t iX = 0; iX < intScale; ++iX) {
                const Pixels & S = src.pix[(srcY + iY) * srcPairsPerRow + srcX + iX];
                U += S.u;
                V += S.v;
                if (iX < rightSide) {
                  Y1 += S.y1 + S.y2;
                } else if (iX == middlePixel) {
                  Y1 += S.y1;
                  Y2 += S.y2;
                } else {
                  Y2 += S.y1 + S.y2;
                }
              }
            }
            Pixels & R = L.pix[gridRowStart + x];
            R.u = U / div;
            R.v = V / div;
            R.y1 = Y1 / div;
            R.y2 = Y2 / div;
          }
        }
      }
      return;
    }
  }

  /// Returns byte size for a UTF-8 character at given offset.
  size_t utf8CodeSize(const std::string & txt, size_t offset) {
    // Single byte
    if (!(txt[offset] & 0x80)) { return 1; }
    if ((txt[offset] & 0xE0) == 0xC0) { return 2; }
    // Three byte char
    if ((txt[offset] & 0xF0) == 0xE0) { return 3; }
    // Four byte char
    if ((txt[offset] & 0xF8) == 0xF0) { return 4; }
    // Error
    return 0;
  }

  /// Calculates length in characters for a UTF-8 string
  size_t utf8Len(const std::string & txt) {
    size_t l = 0;
    size_t pos = 0;
    while (pos < txt.size()) {
      size_t s = utf8CodeSize(txt, pos);
      if (s) { // Valid char, count and skip
        pos += s;
        ++l;
      } else { // Error char, don't count
        ++pos;
      }
    }
    return l;
  }

  /// Writes a single UTF-8 code point by modifying the luma components. Chroma is not altered.
  /// Increments offset by the number of bytes used in the UTF-8 string txt.
  /// If a code point is not printable, only increments offset to next code point in the string.
  /// Returns true if a letter was written.
  bool writeCodePoint(const std::string & txt, size_t & offset, DestMatrix & L, size_t X, size_t Y) {
    size_t s = utf8CodeSize(txt, offset);
    uint8_t *letter = 0;
    if (s == 1) {
      uint32_t codePoint = txt[offset];
      if (codePoint >= 0x20 && codePoint <= 0x7E) { letter = font8x8_basic[codePoint - 0x20]; }
    }
    if (s == 2 && txt.size() > offset + 1) {
      uint32_t codePoint = ((txt[offset] & 0x1F) << 6) | (txt[offset + 1] & 0x3F);
      if (codePoint >= 0xA0 && codePoint <= 0xFF) { letter = font8x8_ext_latin[codePoint - 0xA0]; }
    }
    if (letter) {
      for (size_t i = 0; i < 8; ++i) {
        size_t pixOff = (Y + i) * (L.totWidth / 2) + X / 2;
        L.pix[pixOff].y1 = (letter[i] & 1) ? 240 : 16;
        L.pix[pixOff].y2 = (letter[i] & 2) ? 240 : 16;
        L.pix[pixOff + 1].y1 = (letter[i] & 4) ? 240 : 16;
        L.pix[pixOff + 1].y2 = (letter[i] & 8) ? 240 : 16;
        L.pix[pixOff + 2].y1 = (letter[i] & 16) ? 240 : 16;
        L.pix[pixOff + 2].y2 = (letter[i] & 32) ? 240 : 16;
        L.pix[pixOff + 3].y1 = (letter[i] & 64) ? 240 : 16;
        L.pix[pixOff + 3].y2 = (letter[i] & 128) ? 240 : 16;
      }
    }
    offset += s;
    return letter;
  }

  /// Writes a line of UTF-8 text to fit within the given destination.
  void writeText(DestMatrix & L, const std::string & txt) {
    size_t len = utf8Len(txt);

    // Cut off any text that doesn't fit in the cell width
    if (len > L.cellWidth / 8) { len = L.cellWidth / 8; }

    // Bottom center position
    size_t X = L.cellX + (L.cellWidth / 2) - (len * 4);
    size_t Y = L.cellY + L.cellHeight - 8;
    if (L.blankB >= 12) { Y = L.cellY + L.cellHeight - L.blankB + 4; }

    // Write actual text, one code point at a time.
    size_t p = 0;
    size_t offset = 0;
    while (p < len && offset < txt.size()) {
      if (writeCodePoint(txt, offset, L, X + p * 8, Y)) { ++p; }
    }
  }

} // namespace PixFmtUYVY

namespace PixFmt {
  /// Copies Y image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtY::SrcMatrix & src, PixFmtUYVY::DestMatrix & L) {}

  /// Copies YA image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtYA::SrcMatrix & src, PixFmtUYVY::DestMatrix & L) {}

  /// Copies RGB image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtRGB::SrcMatrix & src, PixFmtUYVY::DestMatrix & L) {}

  /// Copies RGBA image src to fit within UYVY DestMatrix L, using given
  /// scaling algoritm and aspect ratio algorithm. Uses no intermediary buffers and copies in one go directly.
  void copyScaled(const PixFmtRGBA::SrcMatrix & src, PixFmtUYVY::DestMatrix & L) {
    // Calculate pixel pair dimensions (UYVY stores 2 pixels per 4-byte pair)
    const size_t srcEntriesPerRow = src.width;
    const size_t gridPairsPerRow = L.totWidth / 2;

    // Default scaling dimensions (may be adjusted for aspect ratio handling)
    size_t scaleWidth = L.cellWidth;
    size_t scaleHeight = L.cellHeight;
    int16_t offsetX = 0, offsetY = 0;
    size_t intScale = 1;
    size_t cropL = 0, cropR = 0, cropU = 0, cropD = 0;
    calculateScaling(src.width, src.height, L, scaleWidth, scaleHeight, offsetX, offsetY, intScale, cropL, cropR, cropU, cropD);

    // Calculate final grid position including any letterbox offsets
    const int16_t destX = L.cellX + offsetX;
    const int16_t destY = L.cellY + offsetY;
    const int16_t destPairX = destX / 2;
    const size_t scalePairsPerRow = scaleWidth / 2;

    if (L.aspect == PixFmt::PATTERN) {
      // Pattern fill, no scaling at all
      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) break; // Bounds check
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;
        size_t srcY = y % src.height;
        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destX + y < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) break; // Bounds check
          PixFmtUYVY::Pixels & destPx = L.pix[gridRowStart + x];

          auto & left = src.pix[srcY * srcEntriesPerRow + ((x * 2 - cropL) % src.width)];
          auto & right = src.pix[srcY * srcEntriesPerRow + ((x * 2 - cropL + 1) % src.width)];
          if (left.a == 255 && right.a == 255) {
            destPx.y1 = left.Y();
            destPx.y2 = right.Y();
            destPx.u = (left.U() + right.U()) / 2;
            destPx.v = (left.V() + right.V()) / 2;
          } else {
            destPx.y1 = (((left.Y() * left.a + destPx.y1 * (255 - left.a)) + 128) * 257) >> 16;
            destPx.y2 = (((right.Y() * right.a + destPx.y2 * (255 - right.a)) + 128) * 257) >> 16;
            uint8_t avgA = (left.a + right.a) / 2;
            destPx.u = ((((right.U() + left.U()) / 2 * avgA + destPx.u * (255 - avgA)) + 128) * 257) >> 16;
            destPx.v = ((((right.V() + left.V()) / 2 * avgA + destPx.v * (255 - avgA)) + 128) * 257) >> 16;
          }
        }
      }
      return;
    }
    if (L.scale == BILINEAR) {
      uint32_t xRatio = ((src.width << 16) / scaleWidth);
      uint32_t yRatio = ((src.height << 16) / scaleHeight);

      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        size_t destYPos = destY + y;
        if (destY + y < 0) { continue; }
        if (destYPos >= L.totHeight) break; // Bounds check

        uint32_t srcY_fp = y * yRatio;
        size_t y1 = srcY_fp >> 16;
        size_t y2 = std::min(y1 + 1, src.height - 1);
        uint32_t wy = (srcY_fp >> 8) & 0xFF; // Fractional part (0-255)
        uint32_t iwy = 256 - wy;

        size_t gridRowStart = destYPos * gridPairsPerRow + destPairX;
        size_t srcRow1 = y1 * src.width;
        size_t srcRow2 = y2 * src.width;

        for (size_t x = cropL / 2; x < scaleWidth / 2 - cropR / 2; x++) {
          if (destX + y < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) break; // Bounds check

          uint32_t srcX1_fp = (2 * x) * xRatio;
          uint32_t srcX2_fp = (2 * x + 1) * xRatio;

          size_t x1_y1 = srcX1_fp >> 16;
          size_t x2_y1 = std::min(x1_y1 + 1, (size_t)src.width - 1);
          uint32_t wx1 = (srcX1_fp >> 8) & 0xFF;
          uint32_t iwx1 = 256 - wx1;

          size_t x1_y2 = srcX2_fp >> 16;
          size_t x2_y2 = std::min(x1_y2 + 1, (size_t)src.width - 1);
          uint32_t wx2 = (srcX2_fp >> 8) & 0xFF;
          uint32_t iwx2 = 256 - wx2;

          const auto & p11_y1 = src.pix[srcRow1 + x1_y1];
          const auto & p12_y1 = src.pix[srcRow1 + x2_y1];
          const auto & p21_y1 = src.pix[srcRow2 + x1_y1];
          const auto & p22_y1 = src.pix[srcRow2 + x2_y1];
          const auto & p11_y2 = src.pix[srcRow1 + x1_y2];
          const auto & p12_y2 = src.pix[srcRow1 + x2_y2];
          const auto & p21_y2 = src.pix[srcRow2 + x1_y2];
          const auto & p22_y2 = src.pix[srcRow2 + x2_y2];

          auto interp = [&](uint8_t v11, uint8_t v12, uint8_t v21, uint8_t v22, uint32_t wx, uint32_t iwx) {
            return (v11 * iwx * iwy + v12 * wx * iwy + v21 * iwx * wy + v22 * wx * wy + 32768) >> 16;
          };
          uint8_t sY1 = interp(p11_y1.Y(), p12_y1.Y(), p21_y1.Y(), p22_y1.Y(), wx1, iwx1);
          uint8_t sY2 = interp(p11_y2.Y(), p12_y2.Y(), p21_y2.Y(), p22_y2.Y(), wx2, iwx2);
          uint8_t sU = interp(p11_y1.U(), p12_y1.U(), p21_y1.U(), p22_y1.U(), wx1, iwx1);
          uint8_t sV = interp(p11_y1.V(), p12_y1.V(), p21_y1.V(), p22_y1.V(), wx1, iwx1);
          uint8_t sA = interp(p11_y1.a, p12_y1.a, p21_y1.a, p22_y1.a, wx1, iwx1);

          PixFmtUYVY::Pixels & destPx = L.pix[gridRowStart + x];
          if (sA == 255) {
            destPx.y1 = sY1;
            destPx.y2 = sY2;
            destPx.u = sU;
            destPx.v = sV;
          } else if (sA > 0) {
            uint32_t invA = 255 - sA;
            destPx.y1 = ((sY1 * sA + destPx.y1 * invA + 128) * 257 >> 16);
            destPx.y2 = ((sY2 * sA + destPx.y2 * invA + 128) * 257 >> 16);
            destPx.u = ((sU * sA + destPx.u * invA + 128) * 257 >> 16);
            destPx.v = ((sV * sA + destPx.v * invA + 128) * 257 >> 16);
          }
        }
      }
      return;
    }
    if (L.scale == PixFmt::NEAREST) {
      // Nearest neighbour: Fast, uses closest pixel without interpolation
      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) { break; } // Bounds check

        // Find nearest source row (integer math for speed)
        size_t srcY = (y * src.height) / scaleHeight;
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;

        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destX + y < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) { break; } // Bounds check

          // Find nearest source pixel pair and copy directly
          auto & left = src.pix[srcY * srcEntriesPerRow + (x * 2 * src.width) / scaleWidth];
          auto & right = src.pix[srcY * srcEntriesPerRow + ((x * 2 + 1) * src.width) / scaleWidth];
          PixFmtUYVY::Pixels & destPx = L.pix[gridRowStart + x];
          if (left.a == 255 && right.a == 255) {
            destPx.y1 = left.Y();
            destPx.y2 = right.Y();
            destPx.u = (left.U() + right.U()) / 2;
            destPx.v = (left.V() + right.V()) / 2;
          } else {
            destPx.y1 = (((left.Y() * left.a + destPx.y1 * (255 - left.a)) + 128) * 257) >> 16;
            destPx.y2 = (((right.Y() * right.a + destPx.y2 * (255 - right.a)) + 128) * 257) >> 16;
            uint8_t avgA = (left.a + right.a) / 2;
            destPx.u = ((((right.U() + left.U()) / 2 * avgA + destPx.u * (255 - avgA)) + 128) * 257) >> 16;
            destPx.v = ((((right.V() + left.V()) / 2 * avgA + destPx.v * (255 - avgA)) + 128) * 257) >> 16;
          }
        }
      }
      return;
    }
    if (L.scale == PixFmt::INTEGER) {
      // Integer scaling, only divides by 2-multiples, quick and good quality but inflexible
      size_t div = (intScale * intScale);

      for (size_t y = cropU; y < scaleHeight - cropD; y++) {
        if (destY + y < 0) { continue; }
        if (destY + y >= L.totHeight) { break; } // Bounds check
        size_t gridRowStart = (destY + y) * gridPairsPerRow + destPairX;
        size_t srcY = y * intScale;
        for (size_t x = cropL / 2; x < scalePairsPerRow - cropR / 2; x++) {
          if (destX + y < 0) { continue; }
          if (destPairX + x >= gridPairsPerRow) { break; } // Bounds check
          PixFmtUYVY::Pixels & destPx = L.pix[gridRowStart + x];

          // Average the values of the pixels we represent with this pixel
          if (intScale == 1) {
            auto & left = src.pix[srcY * srcEntriesPerRow + (x * 2 - cropL)];
            auto & right = src.pix[srcY * srcEntriesPerRow + (x * 2 - cropL + 1)];
            if (left.a == 255 && right.a == 255) {
              destPx.y1 = left.Y();
              destPx.y2 = right.Y();
              destPx.u = (left.U() + right.U()) / 2;
              destPx.v = (left.V() + right.V()) / 2;
            } else {
              destPx.y1 = (((left.Y() * left.a + destPx.y1 * (255 - left.a)) + 128) * 257) >> 16;
              destPx.y2 = (((right.Y() * right.a + destPx.y2 * (255 - right.a)) + 128) * 257) >> 16;
              uint8_t avgA = (left.a + right.a) / 2;
              destPx.u = ((((right.U() + left.U()) / 2 * avgA + destPx.u * (255 - avgA)) + 128) * 257) >> 16;
              destPx.v = ((((right.V() + left.V()) / 2 * avgA + destPx.v * (255 - avgA)) + 128) * 257) >> 16;
            }
          } else {
            uint16_t Y1 = 0, Y2 = 0, U = 0, V = 0, AL = 0, AR = 0;
            for (size_t iY = 0; iY < intScale; ++iY) {
              for (size_t iX = 0; iX < intScale; ++iX) {
                auto & left = src.pix[(srcY + iY) * srcEntriesPerRow + (x * 2 - cropL) * intScale + iX];
                auto & right = src.pix[(srcY + iY) * srcEntriesPerRow + (x * 2 - cropL + 1) * intScale + iX];
                U += left.U() + right.U();
                V += left.V() + right.V();
                Y1 += left.Y();
                Y2 += right.Y();
                AL += left.a;
                AR += right.a;
              }
            }
            AL /= div;
            AR /= div;
            if (AL == 255 && AR == 255) {
              destPx.u = U / div / 2;
              destPx.v = V / div / 2;
              destPx.y1 = Y1 / div;
              destPx.y2 = Y2 / div;
            } else {
              destPx.y1 = (((Y1 / div * AL + destPx.y1 * (255 - AL)) + 128) * 257) >> 16;
              destPx.y2 = (((Y2 / div * AR + destPx.y2 * (255 - AR)) + 128) * 257) >> 16;
              uint8_t avgA = (AL + AR) / 2;
              destPx.u = (((U / div / 2 * avgA + destPx.u * (255 - avgA)) + 128) * 257) >> 16;
              destPx.v = (((V / div / 2 * avgA + destPx.v * (255 - avgA)) + 128) * 257) >> 16;
            }
          }
        }
      }
      return;
    }
  }

} // namespace PixFmt
