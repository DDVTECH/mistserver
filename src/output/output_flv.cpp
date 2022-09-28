#include "output_flv.h"
#include <mist/h264.h>

namespace Mist{
  OutFLV::OutFLV(Socket::Connection &conn) : HTTPOutput(conn){}

  void OutFLV::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "FLV";
    capa["friendly"] = "Flash progressive over HTTP (FLV)";
    capa["desc"] = "Pseudostreaming in Adobe Flash FLV format over HTTP";
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
    capa["methods"][0u]["hrn"] = "FLV progressive";
    capa["methods"][0u]["priority"] = 5;
    capa["methods"][0u]["player_url"] = "/oldflashplayer.swf";
    capa["push_urls"].append("*.flv");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Target filename to store FLV file as, or - for stdout.";
    cfg->addOption("target", opt);

    opt.null();
    opt["short"] = "k";
    opt["long"] = "keyframe";
    opt["help"] = "Send only a single video keyframe";
    cfg->addOption("keyframeonly", opt);
  }

  bool OutFLV::isRecording(){return config->getString("target").size();}

  void OutFLV::sendNext(){
    // If there are now more selectable tracks, select the new track and do a seek to the current
    // timestamp
    if (M.getLive() && userSelect.size() < 2){
      static uint64_t lastMeta = 0;
      if (Util::epoch() > lastMeta + 5){
        lastMeta = Util::epoch();
        std::set<size_t> validTracks = getSupportedTracks();
        if (validTracks.size() > 1){
          if (selectDefaultTracks()){
            INFO_MSG("Track selection changed - resending headers and continuing");
            for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin();
                 it != userSelect.end(); it++){
              if (M.getType(it->first) == "video" && tag.DTSCVideoInit(meta, it->first)){
                myConn.SendNow(tag.data, tag.len);
              }
              if (M.getType(it->first) == "audio" && tag.DTSCAudioInit(meta.getCodec(it->first), meta.getRate(it->first), meta.getSize(it->first), meta.getChannels(it->first), meta.getInit(it->first))){
                myConn.SendNow(tag.data, tag.len);
              }
            }
            return;
          }
        }
      }
    }
    tag.DTSCLoader(thisPacket, M, thisIdx);
    if (M.getCodec(thisIdx) == "PCM" && M.getSize(thisIdx) == 16){
      char *ptr = tag.getData();
      uint32_t ptrSize = tag.getDataLen();
      for (uint32_t i = 0; i < ptrSize; i += 2){
        char tmpchar = ptr[i];
        ptr[i] = ptr[i + 1];
        ptr[i + 1] = tmpchar;
      }
    }
    myConn.SendNow(tag.data, tag.len);
    if (config->getBool("keyframeonly")){config->is_active = false;}
  }

  void OutFLV::sendHeader(){
    if (!isRecording()){
      H.Clean();
      H.SetHeader("Content-Type", "video/x-flv");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
    }
    if (config->getBool("keyframeonly")){
      userSelect.clear();
      std::set<size_t> validTracks = M.getValidTracks();
      for (std::set<size_t>::iterator it = validTracks.begin(); it != validTracks.end(); it++){
        if (M.getType(*it) == "video"){
          userSelect[*it].reload(streamName, *it);
          break;
        }
      }
    }

    myConn.SendNow(FLV::Header, 13);
    std::set<size_t> selectedTracks;
    for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
      selectedTracks.insert(it->first);
    }
    tag.DTSCMetaInit(M, selectedTracks);
    myConn.SendNow(tag.data, tag.len);
    for (std::set<size_t>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (M.getType(*it) == "video" && tag.DTSCVideoInit(meta, *it)){
        myConn.SendNow(tag.data, tag.len);
      }
      if (M.getType(*it) == "audio" && tag.DTSCAudioInit(meta.getCodec(*it), meta.getRate(*it), meta.getSize(*it), meta.getChannels(*it), meta.getInit(*it))){
        myConn.SendNow(tag.data, tag.len);
      }
    }
    if (config->getBool("keyframeonly")){
      size_t tid = userSelect.begin()->first;
      DTSC::Keys keys(M.keys(tid));
      uint32_t endKey = keys.getEndValid();
      uint64_t keyTime = keys.getTime(endKey - 1);
      INFO_MSG("Seeking for time %" PRIu64 " on track %zu key %" PRIu32, keyTime, tid, endKey - 1);
      seek(keyTime);
    }
    sentHeader = true;
  }

  void OutFLV::onHTTP(){
    std::string method = H.method;

    H.Clean();
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "video/x-flv");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }

    parseData = true;
    wantRequest = false;
  }
}// namespace Mist
