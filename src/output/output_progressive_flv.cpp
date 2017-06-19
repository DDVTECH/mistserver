#include "output_progressive_flv.h"

namespace Mist {
  OutProgressiveFLV::OutProgressiveFLV(Socket::Connection & conn) : HTTPOutput(conn){}
  
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
    capa["codecs"][0u][1u].append("ALAW");
    capa["codecs"][0u][1u].append("ULAW");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "flash/7";
    capa["methods"][0u]["priority"] = 5ll;
    capa["methods"][0u]["player_url"] = "/oldflashplayer.swf";
  }
  
  void OutProgressiveFLV::sendNext(){
    DTSC::Track & trk = myMeta.tracks[thisPacket.getTrackId()];
    tag.DTSCLoader(thisPacket, trk);
    if (trk.codec == "PCM" && trk.size == 16){
      char * ptr = tag.getData();
      uint32_t ptrSize = tag.getDataLen();
      for (uint32_t i = 0; i < ptrSize; i+=2){
        char tmpchar = ptr[i];
        ptr[i] = ptr[i+1];
        ptr[i+1] = tmpchar;
      }
    }
    myConn.SendNow(tag.data, tag.len); 
  }

  void OutProgressiveFLV::sendHeader(){
    
    
    H.Clean();
    H.SetHeader("Content-Type", "video/x-flv");
    H.protocol = "HTTP/1.0";
    H.setCORSHeaders();
    H.SendResponse("200", "OK", myConn);
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
