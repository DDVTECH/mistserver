#include "output_ts.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTS::OutTS(Socket::Connection & conn) : TSOutput(conn){
    sendRepeatingHeaders = 500;//PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
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
  }
  
  OutTS::~OutTS() {}
  
  void OutTS::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "TS";
    capa["desc"] = "Enables the raw MPEG Transport Stream protocol over TCP.";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["streamname"]["short"] = "s";
    capa["optional"]["tracks"]["name"] = "Tracks";
    capa["optional"]["tracks"]["help"] = "The track IDs of the stream that this connector will transmit separated by spaces";
    capa["optional"]["tracks"]["type"] = "str";
    capa["optional"]["tracks"]["option"] = "--tracks";
    capa["optional"]["tracks"]["short"] = "t";
    capa["optional"]["tracks"]["default"] = "";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    cfg->addConnectorOptions(8888, capa);
    config = cfg;
  }

  void OutTS::sendTS(const char * tsData, unsigned int len){
    myConn.SendNow(tsData, len);
  }
}
