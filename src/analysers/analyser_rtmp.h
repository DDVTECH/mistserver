#include <mist/flv_tag.h> //FLV support
#include <mist/config.h>
#include "analyser.h"

#include <string>
#include <fstream>
#include <iostream>
#include <mist/amf.h>
#include <mist/rtmpchunks.h>
#include <mist/config.h>
#include <mist/socket.h>

class rtmpAnalyser : public analysers 
{
  FLV::Tag flvData;
  long long filter;

  std::string inbuffer;
  RTMPStream::Chunk next;
  FLV::Tag F; //FLV holder
  unsigned int read_in ;
  Socket::Buffer strbuf;
  AMF::Object amfdata;
  AMF::Object3 amf3data;

  int Detail;

  public:
    rtmpAnalyser(Util::Config config);
    bool packetReady();
    void PreProcessing();
    //int Analyse();
    int doAnalyse();
//    void doValidate();
};
