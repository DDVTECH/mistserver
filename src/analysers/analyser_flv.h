#pragma once
#include "analyser.h"
#include <mist/flv_tag.h> //FLV support
#include <mist/util.h>

class AnalyserFLV : public Analyser, public Util::DataCallback{
public:
  AnalyserFLV(Util::Config &conf);
  bool parsePacket();
  static void init(Util::Config &conf);
  void dataCallback(const char * ptr, size_t size);

private:
  FLV::Tag flvData;
  Util::ResizeablePointer p;
  Socket::Buffer buffer;
  long long filter;
};
