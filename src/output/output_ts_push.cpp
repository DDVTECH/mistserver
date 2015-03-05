#include "output_ts_push.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTSPush::OutTSPush(Socket::Connection & conn) : TSOutput(conn){
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
    sendRepeatingHeaders = true;
    initialize();
    std::string tracks = config->getString("tracks");
    unsigned int currTrack = 0;
    //loop over tracks, add any found track IDs to selectedTracks
    if (tracks != ""){
      selectedTracks.clear();
      for (unsigned int i = 0; i < tracks.size(); ++i){
        if (tracks[i] >= '0' && tracks[i] <= '9'){
          currTrack = currTrack*10 + (tracks[i] - '0');
        }else{
          if (currTrack > 0){
            selectedTracks.insert(currTrack);
          }
          currTrack = 0;
        }
      }
      if (currTrack > 0){
        selectedTracks.insert(currTrack);
      }
    }

    //For udp pushing, 7 ts packets a time
    packetBuffer.reserve(config->getInteger("udpsize") * 188);
    std::string host = config->getString("destination");
    if (host.substr(0, 6) == "udp://"){
      host = host.substr(6);
    }
    int port = atoi(host.substr(host.find(":") + 1).c_str());
    host = host.substr(0, host.find(":"));
    pushSock.SetDestination(host, port);
  }
  
  OutTSPush::~OutTSPush() {}
  
  void OutTSPush::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "TSPush";
    capa["desc"] = "Push raw MPEG/TS over a TCP or UDP socket.";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["destination"]["name"] = "Destination";
    capa["required"]["destination"]["help"] = "Where to push to, in the format protocol://hostname:port. Ie: udp://127.0.0.1:9876";
    capa["required"]["destination"]["type"] = "str";
    capa["required"]["destination"]["option"] = "--destination";
    capa["required"]["udpsize"]["name"] = "UDP Size";
    capa["required"]["udpsize"]["help"] = "The number of TS packets to push in a single UDP datagram";
    capa["required"]["udpsize"]["type"] = "uint";
    capa["required"]["udpsize"]["default"] = 5;
    capa["required"]["udpsize"]["option"] = "--udpsize";
    capa["optional"]["tracks"]["name"] = "Tracks";
    capa["optional"]["tracks"]["help"] = "The track IDs of the stream that this connector will transmit separated by spaces";
    capa["optional"]["tracks"]["type"] = "str";
    capa["optional"]["tracks"]["option"] = "--tracks";
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    cfg->addBasicConnectorOptions(capa);
    cfg->addOption("streamname",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("destination",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"D\",\"long\":\"destination\",\"help\":\"Where to push to, in the format protocol://hostname:port. Ie: udp://127.0.0.1:9876\"}"));
    cfg->addOption("tracks",
                   JSON::fromString("{\"arg\":\"string\",\"value\":[\"\"],\"short\": \"t\",\"long\":\"tracks\",\"help\":\"The track IDs of the stream that this connector will transmit separated by spaces.\"}"));
    cfg->addOption("udpsize",
                   JSON::fromString("{\"arg\":\"integer\",\"value\":5,\"short\": \"u\",\"long\":\"udpsize\",\"help\":\"The number of TS packets to push in a single UDP datagram.\"}"));
    config = cfg;
  }
  
  void OutTSPush::fillBuffer(const char * data, size_t dataLen){
    static int curFilled = 0;
    if (curFilled == config->getInteger("udpsize")){
      pushSock.SendNow(packetBuffer);
      packetBuffer.clear();
      packetBuffer.reserve(config->getInteger("udpsize") * 188);
      curFilled = 0;
    }
    packetBuffer += std::string(data, 188);
    curFilled ++;
  }

  void OutTSPush::sendTS(const char * tsData, unsigned int len){
    fillBuffer(tsData, len);
  }

}
