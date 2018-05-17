#include "output_ts.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTS::OutTS(Socket::Connection & conn) : TSOutput(conn){
    sendRepeatingHeaders = 500;//PAT/PMT every 500ms (DVB spec)
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
    pushOut = false;
    initialize();
    std::string tracks = config->getString("tracks");
    if (config->getString("target").size()){
      HTTP::URL target(config->getString("target"));
      if (target.protocol != "tsudp"){
        FAIL_MSG("Target %s must begin with tsudp://, aborting", target.getUrl().c_str());
        parseData = false;
        myConn.close();
        return;
      }
      if (!target.getPort()){
        FAIL_MSG("Target %s must contain a port, aborting", target.getUrl().c_str());
        parseData = false;
        myConn.close();
        return;
      }
      pushOut = true;
      udpSize = 5;
      if (targetParams.count("tracks")){tracks = targetParams["tracks"];}
      if (targetParams.count("pkts")){udpSize = atoi(targetParams["pkts"].c_str());}
      packetBuffer.reserve(188*udpSize);
      if (target.path.size()){
        if (!pushSock.bind(0, target.path)){
          disconnect();
          streamName = "";
          selectedTracks.clear();
          config->is_active = false;
          return;
        }
      }
      pushSock.SetDestination(target.host, target.getPort());
    }
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
    capa["required"]["streamname"]["name"] = "Source stream";
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
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("MPEG2");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("AC3");
    capa["codecs"][0u][1u].append("MP2");
    cfg->addConnectorOptions(8888, capa);
    config = cfg;
    capa["push_urls"].append("tsudp://*");
    
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target tsudp:// URL to push out towards.";
    cfg->addOption("target", opt);
  }

  void OutTS::initialSeek(){
    //Adds passthrough support to the regular initialSeek function
    if (targetParams.count("passthrough")){
      selectedTracks.clear();
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin();
           it != myMeta.tracks.end(); it++){
        selectedTracks.insert(it->first);
      }
    }
    Output::initialSeek();
  }

  void OutTS::sendTS(const char * tsData, unsigned int len){
    if (pushOut){
      static int curFilled = 0;
      if (curFilled == udpSize){
        pushSock.SendNow(packetBuffer);
        myConn.addUp(packetBuffer.size());
        packetBuffer.clear();
        packetBuffer.reserve(udpSize * len);
        curFilled = 0;
      }
      packetBuffer.append(tsData, len);
      curFilled ++;
    }else{
      myConn.SendNow(tsData, len);
    }
  }

  bool OutTS::listenMode(){
    return !(config->getString("target").size());
  }

}
