#include "mist/socket_srt.h"
#include "output_tssrt.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/url.h>

namespace Mist{
  OutTSSRT::OutTSSRT(Socket::Connection &conn, SRTSOCKET _srtSock) : TSOutput(conn){
    // NOTE: conn is useless for SRT, as it uses a different socket type.
    sendRepeatingHeaders = 500; // PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    pushOut = false;
    std::string tracks;
    // Push output configuration
    if (config->getString("target").size()){
      HTTP::URL target(config->getString("target"));
      if (target.protocol != "srt"){
        FAIL_MSG("Target %s must begin with srt://, aborting", target.getUrl().c_str());
        onFail("Invalid srt target: doesn't start with srt://", true);
        return;
      }
      if (!target.getPort()){
        FAIL_MSG("Target %s must contain a port, aborting", target.getUrl().c_str());
        onFail("Invalid srt target: missing port", true);
        return;
      }
      pushOut = true;
      if (targetParams.count("tracks")){tracks = targetParams["tracks"];}
      size_t connectCnt = 0;
      do{
        srtConn.connect(target.host, target.getPort(), "output");
        if (!srtConn){Util::sleep(1000);}
        ++connectCnt;
      }while (!srtConn && connectCnt < 10);
      wantRequest = false;
      parseData = true;
    }else{
      // Pull output configuration, In this case we have an srt connection in the second constructor parameter.
      srtConn = Socket::SRTConnection(_srtSock);
      parseData = true;
      wantRequest = false;

      // Handle override / append of streamname options
      std::string sName = srtConn.getStreamName();
      if (sName != ""){
        streamName = sName;
        HIGH_MSG("Requesting stream %s", streamName.c_str());
      }
    }
    initialize();
  }

  OutTSSRT::~OutTSSRT(){}

  void OutTSSRT::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "TSSRT";
    capa["friendly"] = "TS over SRT";
    capa["desc"] = "Real time streaming of TS data over SRT";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add "
                                             "this protocol multiple times using different ports, "
                                             "or use the streamid option on the srt connection";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["streamname"]["short"] = "s";
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    cfg->addConnectorOptions(8889, capa);
    config = cfg;
    capa["push_urls"].append("srt://*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target srt:// URL to push out towards.";
    cfg->addOption("target", opt);
  }

  // Buffer internally in the class, and send once we have over 1000 bytes of data.
  void OutTSSRT::sendTS(const char *tsData, size_t len){
    if (packetBuffer.size() >= 1000){
      srtConn.SendNow(packetBuffer);
      if (!srtConn){
        // Allow for proper disconnect
        parseData = false;
      }
      packetBuffer.clear();
    }
    packetBuffer.append(tsData, len);
  }

  bool OutTSSRT::setAlternateConnectionStats(Comms::Statistics &statComm){
    statComm.setUp(srtConn.dataUp());
    statComm.setDown(srtConn.dataDown());
    statComm.setTime(Util::bootSecs() - srtConn.connTime());
    return true;
  }

  void OutTSSRT::handleLossyStats(Comms::Statistics &statComm){
    statComm.setPacketCount(srtConn.packetCount());
    statComm.setPacketLostCount(srtConn.packetLostCount());
    statComm.setPacketRetransmitCount(srtConn.packetRetransmitCount());
  }
}// namespace Mist
