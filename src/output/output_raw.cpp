#include "output_raw.h"

namespace Mist {
  OutRaw::OutRaw(Socket::Connection & conn) : Output(conn) {
    streamName = config->getString("streamname");
    initialize();
    selectedTracks.clear();
    std::string tracks = config->getString("tracks");
    unsigned int currTrack = 0;
    //loop over tracks, add any found track IDs to selectedTracks
    if (tracks != ""){
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
    parseData = true;
    seek(config->getInteger("seek"));
  }
  
  OutRaw::~OutRaw() {}
  
  void OutRaw::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "RAW";
    capa["desc"] = "Enables raw DTSC over TCP.";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["optional"]["tracks"]["name"] = "Tracks";
    capa["optional"]["tracks"]["help"] = "The track IDs of the stream that this connector will transmit separated by spaces";
    capa["optional"]["tracks"]["type"] = "str";
    capa["optional"]["tracks"]["option"] = "--tracks";
    capa["optional"]["seek"]["name"] = "Seek point";
    capa["optional"]["seek"]["help"] = "The time in milliseconds to seek to, 0 by default.";
    capa["optional"]["seek"]["type"] = "int";
    capa["optional"]["seek"]["option"] = "--seek";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    cfg->addOption("streamname",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("tracks",
                   JSON::fromString("{\"arg\":\"string\",\"value\":[\"\"],\"short\": \"t\",\"long\":\"tracks\",\"help\":\"The track IDs of the stream that this connector will transmit separated by spaces.\"}"));
    cfg->addOption("seek",
                   JSON::fromString("{\"arg\":\"integer\",\"value\":[0],\"short\": \"S\",\"long\":\"seek\",\"help\":\"The time in milliseconds to seek to, 0 by default.\"}"));
    cfg->addConnectorOptions(666, capa);
    config = cfg;
  }
  
  void OutRaw::sendNext(){
    myConn.SendNow(currentPacket.getData(), currentPacket.getDataLen());
  }

  void OutRaw::sendHeader(){
    myMeta.send(myConn);
    sentHeader = true;
  }

}
