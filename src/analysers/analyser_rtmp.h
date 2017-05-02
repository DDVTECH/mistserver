#include "analyser.h"
#include <fstream>
#include <mist/flv_tag.h> //FLV support
#include <mist/rtmpchunks.h>

class AnalyserRTMP : public Analyser{
private:
  RTMPStream::Chunk next; ///< Holds the most recently parsed RTMP chunk
  FLV::Tag F;///< Holds the most recently created FLV packet
  unsigned int read_in; ///< Amounts of bytes read to fill 'strbuf' so far
  Socket::Buffer strbuf;///< Internal buffer from where 'next' is filled
  AMF::Object amfdata;///< Last read AMF object
  AMF::Object3 amf3data;///<Last read AMF3 object
  std::ofstream reconstruct;///< If reconstructing, a valid file handle

public:
  AnalyserRTMP(Util::Config & conf);
  static void init(Util::Config & conf);
  bool parsePacket();
  virtual bool open(const std::string &filename);
};

