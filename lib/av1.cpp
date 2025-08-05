#include "av1.h"

#include "defines.h"

#include <sstream>

namespace AV1 {

  std::string leb128(size_t i) {
    std::string r;
    while (i >= 0x80) {
      // Take the second-last 7 bits, make them the last 7 bits, add the 8th bit to indicate not-last
      r += ((char)((i & 0x7F) | 0x80));
      i >>= 7;
    }
    // Last byte is now guaranteed to be < 0x80
    r += (char)(i);
    return r;
  }

  void OBU::init(const void *ptr, size_t len) {
    p = (char *)ptr;
    l = len;
    hdr_size = 0;
    pl_size = 0;
    if (p && l) {
      if (p[0] & 4) { // extension field present, 1 extra byte
        hdr_size = 2;
      } else {
        hdr_size = 1;
      }
      if (p[0] & 2) { // size field present, variable extra bytes
        pl_size = 0;
        size_t i = 0;
        do {
          // Abort if we run out of bytes, set to invalid
          if (len < hdr_size + 1) {
            p = 0;
            l = 0;
            return;
          }
          pl_size |= (p[hdr_size] & 0x7F) << (i * 7);
          ++i;
          ++hdr_size;
        } while (p[hdr_size - 1] & 0x80);
        // Abort if we don't have the payload, set to invalid
        if (len < pl_size + hdr_size) {
          p = 0;
          l = 0;
          return;
        }
        // Correct length to actual length, if actual is less than given
        if (pl_size + hdr_size < l) { l = pl_size + hdr_size; }
      } else {
        pl_size = l - hdr_size;
      }
      if (((p[0] & 0x78) >> 3) == 2) {
        // Temporal delimiter, no payload
        if (pl_size) {
          WARN_MSG("Temporal delimiter has non-zero payload?!");
          pl_size = 0;
          l = hdr_size;
        }
        return;
      }
    }
  }

  bool OBU::isKeyframe() {
    // An OBU is a keyframe if a Sequence Header sets the reduced_still_picture_header flag,
    // Or if a Frame Header has the first nibble equal to 1
    // (meaning: show_existing_frame=0, frame_type==0, show_frame==1)
    // If there is a header extension, it must set temporal_id==0 as well.
    if (!*this) { return false; } // Invalid data
    // Check header extension and temporal_id field. If non-zero, return false.
    if ((p[0] & 4) && (l > 1) && (p[1] & 0xE0)) { return false; }
    // Check OBU type
    uint8_t obuType = ((p[0] & 0x78) >> 3);
    // If Sequence header and reduced_still_picture_header flag is set, return true.
    if (obuType == 1 && (l > hdr_size) && (p[hdr_size] & 0x08)) { return true; }
    // If Frame Header and first nibble is one, return true.
    if ((obuType == 3 || obuType == 6 || obuType == 7) && (l > hdr_size) && (p[hdr_size] & 0xF0) == 0x10) {
      return true;
    }
    // All other cases, false.
    return false;
  }

  std::string OBU::getType() {
    if (!*this) { return "Invalid"; }
    switch ((p[0] & 0x78) >> 3) {
      case 1: return "Sequence Header";
      case 2: return "Temporal Delimiter";
      case 3: return "Frame Header";
      case 4: return "Tile Group";
      case 5: return "Metadata";
      case 6: return "Frame";
      case 7: return "Redundant Frame Header";
      case 8: return "Tile List";
      case 15: return "Padding";
      default: return "Reserved";
    }
  }

  std::string OBU::toString() {
    if (!*this) { return "!!INVALID OBU!!"; }
    std::stringstream o;
    o << getType() << " OBU";
    if (isKeyframe()) { o << " (keyframe)"; }
    o << ", " << getSize() << "b (header=" << getHeaderSize() << "b, payload=" << getPayloadSize() << "b)";
    if (!hdr_size) { return o.str(); }

    if (p[0] & 4) {
      o << " [EXT: temporal_id=" << ((p[1] & 0xE0) >> 5) << " spatial_id=" << ((p[1] & 0x18) >> 3) << "]";
    }
    uint8_t obuType = ((p[0] & 0x78) >> 3);
    if (obuType == 1 && pl_size) {
      // Sequence Header
      o << " [SeqHdr: seq_profile=" << ((p[hdr_size] & 0xE0) >> 5) << " still_picture=" << ((p[hdr_size] & 0x10) >> 4)
        << " reduced_still_picture=" << ((p[hdr_size] & 0x08) >> 3) << "]";
    }
    if ((obuType == 3 || obuType == 6 || obuType == 7) && pl_size) {
      // Frame Header
      o << " [FrmHdr:";
      o << " show_existing_frame=" << ((p[hdr_size] & 0x80) >> 7);
      if (!(p[hdr_size] & 0x80)) {
        o << " frame_type=";
        switch ((p[hdr_size] & 0x60) >> 5) {
          case 0: o << "key"; break;
          case 1: o << "inter"; break;
          case 2: o << "intra"; break;
          case 3: o << "switch"; break;
        }
        o << " show_frame=" << ((p[hdr_size] & 0x10) >> 4);
      }
      o << "]";
    }

    if (!(p[0] & 2)) { o << " [NO SIZE FIELD]"; }
    return o.str();
  }

}; // namespace AV1
