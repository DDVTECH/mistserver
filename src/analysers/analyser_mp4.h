#include "analyser.h"
#include <mist/mp4.h>
#include <mist/mp4_stream.h>

class AnalyserMP4 : public Analyser{
public:
  AnalyserMP4(Util::Config &conf);
  static void init(Util::Config &conf);
  bool parsePacket();

private:
  Util::ResizeablePointer moof;
  Util::ResizeablePointer moov;
  Util::ResizeablePointer mdat;
  uint64_t moovPos;
  uint64_t moofPos;
  uint64_t mdatPos;
  uint64_t neededBytes();
  void analyseData(MP4::Box & mdatBox);
  std::string mp4Buffer;
  MP4::Box mp4Data;
  uint64_t curPos;
  uint64_t prePos;
  std::map<uint64_t, MP4::TrackHeader> hdrs;
};
