#include "output_wav.h"
#include <mist/riff.h>

namespace Mist{
  OutWAV::OutWAV(Socket::Connection &conn) : HTTPOutput(conn){
    if (config->getString("target").size()){
      initialize();
      if (!streamName.size()){
        WARN_MSG("Recording unconnected WAV output to file! Cancelled.");
        conn.close();
        return;
      }
      if (config->getString("target") == "-"){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Outputting %s to stdout in WAV format", streamName.c_str());
        return;
      }
      if (!myMeta.tracks.size()){
        INFO_MSG("Stream not available - aborting");
        conn.close();
        return;
      }
      if (connectToFile(config->getString("target"))){
        parseData = true;
        wantRequest = false;
        INFO_MSG("Recording %s to %s in WAV format", streamName.c_str(),
                 config->getString("target").c_str());
        return;
      }
      conn.close();
    }
  }

  void OutWAV::init(Util::Config *cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "WAV";
    capa["desc"] = "Enables HTTP protocol progressive WAV streaming";
    capa["url_rel"] = "/$.wav";
    capa["url_match"] = "/$.wav";
    capa["codecs"][0u][0u].append("ALAW");
    capa["codecs"][0u][0u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/audio/wav";
    capa["methods"][0u]["priority"] = 1ll;
    capa["push_urls"].append("/*.wav");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1ll;
    opt["help"] = "Target filename to store WAV file as, or - for stdout.";
    cfg->addOption("target", opt);
  }

  bool OutWAV::isRecording(){return config->getString("target").size();}

  void OutWAV::sendNext(){
    char *dataPointer = 0;
    unsigned int len = 0;
    thisPacket.getString("data", dataPointer, len);
    myConn.SendNow(dataPointer, len);
  }

  void OutWAV::sendHeader(){
    if (!isRecording()){
      H.Clean();
      H.SetHeader("Content-Type", "audio/wav");
      H.protocol = "HTTP/1.0";
      H.setCORSHeaders();
      H.SendResponse("200", "OK", myConn);
    }
    DTSC::Track &Trk = myMeta.tracks[getMainSelectedTrack()];
    // Send WAV header
    char riffHeader[] = "RIFF\377\377\377\377WAVE";
    // For live we send max allowed size
    // VoD size of the whole thing is RIFF(4)+fmt(26)+fact(12)+LIST(30)+data(8)+data itself
    uint32_t total_data = 0xFFFFFFFFul - 80;
    if (!myMeta.live){
      total_data = 0;
      for (std::deque<unsigned long>::iterator it = Trk.keySizes.begin(); it != Trk.keySizes.end();
           ++it){
        total_data += *it;
      }
    }
    Bit::htobl_le(riffHeader + 4, 80 + total_data);
    myConn.SendNow(riffHeader, 12);
    // Send format details
    uint16_t fmt = 0;
    if (Trk.codec == "ALAW"){fmt = 6;}
    if (Trk.codec == "MP3"){fmt = 85;}
    myConn.SendNow(RIFF::fmt::generate(fmt, Trk.channels, Trk.rate, Trk.bps,
                                       Trk.channels * (Trk.size << 3), Trk.size));
    // Send sample count per channel
    if (!myMeta.live){
      myConn.SendNow(RIFF::fact::generate(((Trk.lastms - Trk.firstms) * Trk.rate) / 1000));
    }else{
      myConn.SendNow(RIFF::fact::generate(0xFFFFFFFFul));
    }
    // Send MistServer identifier
    myConn.SendNow("LIST\026\000\000\000infoISFT\012\000\000\000MistServer", 30);
    // Start data chunk
    char dataChunk[] = "data\377\377\377\377";
    Bit::htobl_le(dataChunk + 4, total_data);
    myConn.SendNow(dataChunk, 8);
    sentHeader = true;
  }

  void OutWAV::onHTTP(){
    std::string method = H.method;

    H.Clean();
    H.setCORSHeaders();
    if (method == "OPTIONS" || method == "HEAD"){
      H.SetHeader("Content-Type", "audio/wav");
      H.protocol = "HTTP/1.0";
      H.SendResponse("200", "OK", myConn);
      H.Clean();
      return;
    }

    parseData = true;
    wantRequest = false;
  }
}

