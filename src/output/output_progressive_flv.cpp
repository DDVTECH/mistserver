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
    capa["push_urls"].append("/*.flv");
    
    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store FLV file as, or - for stdout.";
    cfg->addOption("target", opt);


    opt.null();
    opt["short"] = "k";
    opt["long"] = "keyframe";
    opt["help"] = "Send only a single video keyframe";
    cfg->addOption("keyframeonly", opt);
  }

  bool OutProgressiveFLV::isRecording(){
    return config->getString("target").size();
  }
  
  void OutProgressiveFLV::sendNext(){
    //If there are now more selectable tracks, select the new track and do a seek to the current timestamp
    if (myMeta.live && selectedTracks.size() < 2){
      static unsigned long long lastMeta = 0;
      if (Util::epoch() > lastMeta + 5){
        lastMeta = Util::epoch();
        updateMeta();
        if (myMeta.tracks.size() > 1){
          if (selectDefaultTracks()){
            INFO_MSG("Track selection changed - resending headers and continuing");
            for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
              if (myMeta.tracks[*it].type == "video" && tag.DTSCVideoInit(myMeta.tracks[*it])){
                myConn.SendNow(tag.data, tag.len);
              }
              if (myMeta.tracks[*it].type == "audio" && tag.DTSCAudioInit(myMeta.tracks[*it])){
                myConn.SendNow(tag.data, tag.len);
              }
            }
            return;
          }
        }
      }
    }

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
    if (config->getBool("keyframeonly")){
      config->is_active = false;
    }
  }

  void OutProgressiveFLV::sendHeader(){
    if (!isRecording()){
      H.Clean();
      H.SetHeader("Content-Type", "video/x-flv");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
    }
    if (config->getBool("keyframeonly")){
      selectedTracks.clear();
      for (std::map<unsigned int, DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
        if (it->second.type =="video"){
          selectedTracks.insert(it->first);
          break;
        }
      }
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
    if (config->getBool("keyframeonly")){
      unsigned int tid = *selectedTracks.begin();
      int keyNum = myMeta.tracks[tid].keys.rbegin()->getNumber();
      int keyTime = myMeta.tracks[tid].getKey(keyNum).getTime();
      INFO_MSG("Seeking for time %d on track %d key %d", keyTime, tid, keyNum);
      seek(keyTime);
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
