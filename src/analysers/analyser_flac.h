#pragma once
#include "analyser.h"

class AnalyserFLAC : public Analyser{

public:
  AnalyserFLAC(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  bool readMagicPacket();
  bool readFrame();
  bool readMeta();

private:
  uint64_t neededBytes();
  void newFrame(char *data);
  bool headerParsed;
  std::string flacBuffer;
  uint64_t bufferSize;
  uint64_t curPos;
  size_t utfBytes(char p);

  bool forceFill;

  char *ptr;
  uint64_t sampleNo;
  uint64_t sampleRate;
  bool stopProcessing;

  char *start; // = &flacBuffer[0];
  char *end;   // = &flacBuffer[flacBuffer.size()];
  char *pos;   // = start;
  int prev_header_size;
  FILE *inFile;
};
