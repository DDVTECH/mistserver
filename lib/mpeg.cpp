#include "mpeg.h"

namespace Mpeg{

  MP2Info parseMP2Header(const std::string &hdr){return parseMP2Header(hdr.c_str());}

  MP2Info parseMP2Header(const char *hdr){
    MP2Info res;
    // mpeg version is on the bits 0x18 of header[1], but only 0x08 is important --> 0 is version 2,
    // 1 is version 1
    // leads to 2 - value == version, -1 to get the right index for the array
    int mpegVersion = 1 - ((hdr[1] >> 3) & 0x01);
    // samplerate is encoded in bits 0x0C of header[2];
    res.sampleRate = sampleRates[mpegVersion][((hdr[2] >> 2) & 0x03)] * 1000;
    res.channels = 2 - (hdr[3] >> 7);
    return res;
  }

  void MPEG2Info::clear(){
    width = 0;
    height = 0;
    fps = 0;
    tempSeq = 0;
    frameType = 0;
    isHeader = false;
  }

  MPEG2Info parseMPEG2Header(const std::string &hdr){return parseMPEG2Header(hdr.data());}

  MPEG2Info parseMPEG2Header(const char *hdr){
    MPEG2Info res;
    parseMPEG2Header(hdr, res);
    return res;
  }

  bool parseMPEG2Header(const char *hdr, MPEG2Info &mpInfo){
    // Check for start code
    if (hdr[0] != 0 || hdr[1] != 0 || hdr[2] != 1){return false;}
    if (hdr[3] == 0xB3){// Sequence header
      mpInfo.isHeader = true;
      mpInfo.width = (hdr[4] << 4) | ((hdr[5] >> 4) & 0x0F);
      mpInfo.height = ((hdr[5] & 0x0F) << 8) | hdr[6];
      mpInfo.fps = frameRate[hdr[7] & 0x0F];
      return true;
    }
    if (hdr[3] == 0x00){// Picture header
      mpInfo.tempSeq = (((uint16_t)hdr[4]) << 2) || (hdr[5] >> 6);
      mpInfo.frameType = (hdr[5] & 0x38) >> 3;
      return true;
    }
    // Not parsed
    return false;
  }

  void parseMPEG2Headers(const char *hdr, uint32_t len, MPEG2Info &mpInfo){
    mpInfo.clear();
    const char *offset = hdr;
    char *maxData = (char*)hdr + len - 5;
    if (maxData - hdr > 250){maxData = (char*)hdr + 250;}
    while (offset < maxData){
      if (offset[2] > 1){
        // We have no zero in the third byte, so we need to skip at least 3 bytes forward
        offset += 3;
        continue;
      }
      if (!offset[2]){
        // We skip forward 1 or 2 bytes depending on contents of the second byte
        offset += (offset[1] ? 2 : 1);
        continue;
      }
      if (!offset[0] && !offset[1]){parseMPEG2Header(offset, mpInfo);}
      // We have no zero in the third byte, so we need to skip at least 3 bytes forward
      offset += 3;
    }
  }

  MPEG2Info parseMPEG2Headers(const char *hdr, uint32_t len){
    MPEG2Info res;
    parseMPEG2Headers(hdr, len, res);
    return res;
  }
}// namespace Mpeg

