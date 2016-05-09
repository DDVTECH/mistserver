#include "output_progressive_flv.h"

namespace Mist {
  OutProgressiveFLV::OutProgressiveFLV(Socket::Connection & conn) : HTTPOutput(conn){
    if (config->getString("target").size()){
      if (!streamName.size()){
        WARN_MSG("Recording unconnected FLV output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in FLV format", streamName.c_str());
        return;
      }
      if (connectToFile(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in FLV format", streamName.c_str(), config->getString("target").c_str());
      }else{
        conn.close();
      }
    }
  }
  
  void OutProgressiveFLV::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "FLV";
    capa["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["url_rel"] = "/$.flv";
    capa["url_match"] = "/$.flv";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][0u].append("VP6Alpha");
    capa["codecs"][0u][0u].append("ScreenVideo2");
    capa["codecs"][0u][0u].append("ScreenVideo1");
    capa["codecs"][0u][0u].append("JPEG");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["codecs"][0u][1u].append("Speex");
    capa["codecs"][0u][1u].append("Nellymoser");
    capa["codecs"][0u][1u].append("PCM");
    capa["codecs"][0u][1u].append("ADPCM");
    capa["codecs"][0u][1u].append("G711a");
    capa["codecs"][0u][1u].append("G711mu");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "flash/7";
    capa["methods"][0u]["priority"] = 5ll;
    capa["methods"][0u]["player_url"] = "/oldflashplayer.swf";
    capa["push_urls"].append("/*.flv");
    
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store FLV file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutProgressiveFLV::isRecording(){
    return config->getString("target").size();
  }
  
  void OutProgressiveFLV::sendNext(){
    tag.DTSCLoader(thisPacket, myMeta.tracks[thisPacket.getTrackId()]);
    myConn.SendNow(tag.data, tag.len); 
  }

  void OutProgressiveFLV::sendHeader(){
    if (!isRecording()){
      H.Clean();
      H.SetHeader("Content-Type", "video/x-flv");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
    }

    myConn.SendNow(FLV::Header, 13);
    tag.DTSCMetaInit(myMeta, selectedTracks);
    myConn.SendNow(tag.data, tag.len);
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].type == "video" && tag.DTSCVideoInit(myMeta.tracks[*it])){
        myConn.SendNow(tag.data, tag.len);
      }
      if (myMeta.tracks[*it].type == "audio" && tag.DTSCAudioInit(myMeta.tracks[*it])){
        myConn.SendNow(tag.data, tag.len);
      }
    }
    sentHeader = true;
  }

  void OutProgressiveFLV::onHTTP(){
    std::string method = H.method;
    
    H.Clean();
    H.setCORSHeaders();
    if(method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "video/x-flv");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }
    
    parseData = true;
    wantRequest = false;
  }
}
