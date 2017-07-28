#include <stdint.h>
#include <string>

namespace Mpeg{
  const static double sampleRates[2][3] ={{44.1, 48.0, 32.0},{22.05, 24.0, 16.0}};
  const static int sampleCounts[2][3] ={{374, 1152, 1152},{384, 1152, 576}};
  const static int bitRates[2][3][16] ={
      {{0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, -1},
       {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, -1},
       {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1}},
      {{0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, -1},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1},
       {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, -1}}};

  struct MP2Info{
    uint64_t sampleRate;
    uint8_t channels;
  };

  MP2Info parseMP2Header(const std::string &hdr);
  MP2Info parseMP2Header(const char *hdr);

  const static double frameRate[16] ={
      0, 24000.0 / 1001, 24, 25, 30000.0 / 1001, 30, 50, 60000.0 / 1001, 60, 0, 0, 0, 0, 0, 0, 0};

  class MPEG2Info{
  public:
    MPEG2Info(){clear();}
    void clear();
    uint64_t width;
    uint64_t height;
    double fps;
    uint16_t tempSeq;
    uint8_t frameType;
    bool isHeader;
  };

  MPEG2Info parseMPEG2Header(const std::string &hdr);
  MPEG2Info parseMPEG2Header(const char *hdr);
  bool parseMPEG2Header(const char *hdr, MPEG2Info &mpInfo);
  void parseMPEG2Headers(const char *hdr, uint32_t len, MPEG2Info &mpInfo);
  MPEG2Info parseMPEG2Headers(const char *hdr, uint32_t len);
}

