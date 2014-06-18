#include "output_progressive_flv.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutProgressiveFLV::OutProgressiveFLV(Socket::Connection & conn) : Output(conn) { 
    myConn.setHost(config->getString("ip"));
    streamName = config->getString("streamname");
  }
  
  OutProgressiveFLV::~OutProgressiveFLV() {}
  
  void OutProgressiveFLV::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "HTTP_Progressive_FLV";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/$.flv";
    capa["url_match"] = "/$.flv";
    capa["socket"] = "http_progressive_flv";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "flash/7";
    capa["methods"][0u]["priority"] = 5ll;

    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutProgressiveFLV::sendNext(){
    FLV::Tag tag;
    bool tmp = tag.DTSCLoader(currentPacket, myMeta.tracks[currentPacket.getTrackId()]);
    if (!tmp){
      DEBUG_MSG(DLVL_DEVEL, "Invalid JSON");
    }
    myConn.SendNow(tag.data, tag.len); 
  }

  void OutProgressiveFLV::sendHeader(){
    HTTP::Parser HTTP_S;
    FLV::Tag tag;
    HTTP_S.SetHeader("Content-Type", "video/x-flv");
    HTTP_S.protocol = "HTTP/1.0";
    myConn.SendNow(HTTP_S.BuildResponse("200", "OK"));
    myConn.SendNow(FLV::Header, 13);
    tag.DTSCMetaInit(myMeta, selectedTracks);
    myConn.SendNow(tag.data, tag.len);

    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].type == "video"){
        tag.DTSCVideoInit(myMeta.tracks[*it]);
        myConn.SendNow(tag.data, tag.len);
      }
      if (myMeta.tracks[*it].type == "audio"){
        tag.DTSCAudioInit(myMeta.tracks[*it]);
        myConn.SendNow(tag.data, tag.len);
      }
    }
    sentHeader = true;
  }

  void OutProgressiveFLV::onFail(){
    HTTP::Parser HTTP_S;
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetBody("Stream not found. Sorry, we tried.");
    HTTP_S.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  void OutProgressiveFLV::onRequest(){
    HTTP::Parser HTTP_R;
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request %s", HTTP_R.getUrl().c_str());
      if (HTTP_R.GetVar("audio") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("audio")).asInt());
      }
      if (HTTP_R.GetVar("video") != ""){
        selectedTracks.insert(JSON::Value(HTTP_R.GetVar("video")).asInt());
      }
      parseData = true;
      wantRequest = false;
      HTTP_R.Clean();
    }
  }

}
