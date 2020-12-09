#include "analyser.h"
#include <fstream>
#include <mist/flv_tag.h> //FLV support
#include <mist/rtmpchunks.h>
#include <mist/encode.h>

class AnalyserRTMP : public Analyser, public Util::DataCallback{
private:
  // Holds the most recently parsed RTMP chunk
  RTMPStream::Chunk next;
  // Holds the most recently created FLV packet
  FLV::Tag F;
  // Amounts of bytes read to fill 'strbuf' so far
  unsigned int read_in;
  // Internal buffer from where 'next' is filled
  Socket::Buffer strbuf;
  // Last read AMF object
  AMF::Object amfdata;
  // Last read AMF3 object
  AMF::Object3 amf3data;
  // Handle to output file in reconstruction mode
  std::ofstream reconstruct;
  // Will contain URL object of host
  HTTP::URL pushUrl;
  // Send AMF commands to initiate stream
  void requestStream();
  // Performs the RTMP handshake before a stream gets initiated
  bool doHandshake();
  // Waits until the buffer contains bytesNeeded amount of bytes, then removes this from buffer and returns it
  std::string removeFromBufferBlocking(size_t bytesNeeded);
  // If reading from URI or TCPCON
  bool isFile;

public:
  AnalyserRTMP(Util::Config & conf);
  static void init(Util::Config & conf);
  // Parses RTMP chunks
  bool parsePacket();
  // Override default function to check tcpCon rather than isEOF
  virtual bool isOpen();
  // Opens a connection to the URL. Calls doHandshake and startStream
  //  to make the stream ready for parsePacket
  virtual bool open(const std::string &url);
  // Should contain an open TCP connection to the server
  Socket::Connection tcpCon;
};

