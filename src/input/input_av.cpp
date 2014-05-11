#include <iostream>
#include <fstream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_av.h"

namespace Mist {
  inputAV::inputAV(Util::Config * cfg) : Input(cfg) {
    pFormatCtx = 0;
    
    capa["decs"] = "Enables generic avformat/avcodec based input";
    
    capa["codecs"][0u][0u].null();
    capa["codecs"][0u][1u].null();
    capa["codecs"][0u][2u].null();
    
    
    av_register_all();
    AVCodec * cInfo = 0;
    while ((cInfo = av_codec_next(cInfo)) != 0){
      if (cInfo->type == AVMEDIA_TYPE_VIDEO){
        capa["codecs"][0u][0u].append(cInfo->name);
      }
      if (cInfo->type == AVMEDIA_TYPE_AUDIO){
        capa["codecs"][0u][1u].append(cInfo->name);
      }
      if (cInfo->type == AVMEDIA_TYPE_SUBTITLE){
        capa["codecs"][0u][3u].append(cInfo->name);
      }
    }
    
    
    
  }

  inputAV::~inputAV(){
    if (pFormatCtx){
      avformat_close_input(&pFormatCtx);
    }
  }
  
  bool inputAV::setup() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getBool("player")){
      if (config->getString("output") == "-") {
        std::cerr << "Output to stdout not yet supported" << std::endl;
        return false;
      }
    }else{
      if (config->getString("output") != "-") {
        std::cerr << "File output in player mode not supported" << std::endl;
        return false;
      }
    }

    //make sure all av inputs are registered properly, just in case
    //setup() already does this, but under windows it doesn't remember it. Very sad, that.
    av_register_all();
    
    //close any already open files
    if (pFormatCtx){
      avformat_close_input(&pFormatCtx);
      pFormatCtx = 0;
    }
    
    //Open video file
    int ret = avformat_open_input(&pFormatCtx, config->getString("input").c_str(), NULL, NULL);
    if(ret != 0){
      char errstr[300];
      av_strerror(ret, errstr, 300);
      DEBUG_MSG(DLVL_FAIL, "Could not open file: %s", errstr);
      return false; // Couldn't open file
    }
      
    //Retrieve stream information
    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if(ret < 0){
      char errstr[300];
      av_strerror(ret, errstr, 300);
      DEBUG_MSG(DLVL_FAIL, "Could not find stream info: %s", errstr);
      return false;
    }
    return true;
  }

  bool inputAV::readHeader() {
    //See whether a separate header file exists.
    DTSC::File tmp(config->getString("input") + ".dtsh");
    if (tmp){
      myMeta = tmp.getMeta();
      return true;
    }
    
    myMeta.tracks.clear();
    myMeta.live = false;
    myMeta.vod = true;
    for(unsigned int i=0; i < pFormatCtx->nb_streams; ){
      AVStream * strm = pFormatCtx->streams[i++];
      myMeta.tracks[i].trackID = i;
      switch (strm->codec->codec_id){
        case AV_CODEC_ID_HEVC:
          myMeta.tracks[i].codec = "HEVC";
          break;
        case AV_CODEC_ID_H264:
          myMeta.tracks[i].codec = "H264";
          break;
        case AV_CODEC_ID_THEORA:
          myMeta.tracks[i].codec = "theora";
          break;
        case AV_CODEC_ID_VORBIS:
          myMeta.tracks[i].codec = "vorbis";
          break;
        case AV_CODEC_ID_OPUS:
          myMeta.tracks[i].codec = "opus";
          break;
        case AV_CODEC_ID_AAC:
          myMeta.tracks[i].codec = "AAC";
          break;
        case AV_CODEC_ID_MP3:
          myMeta.tracks[i].codec = "MP3";
          break;
        default:
          myMeta.tracks[i].codec = strm->codec->codec_name;
          break;
      }
      if (strm->codec->extradata_size){
        myMeta.tracks[i].init = std::string((char*)strm->codec->extradata, strm->codec->extradata_size);
      }
      if(strm->codec->codec_type == AVMEDIA_TYPE_VIDEO){
        myMeta.tracks[i].type = "video";
        if (strm->avg_frame_rate.den && strm->avg_frame_rate.num){
          myMeta.tracks[i].fpks = (strm->avg_frame_rate.num * 1000) / strm->avg_frame_rate.den;
        }else{
          myMeta.tracks[i].fpks = 0;
        }
        myMeta.tracks[i].width = strm->codec->width;
        myMeta.tracks[i].height = strm->codec->height;
      }
      if(strm->codec->codec_type == AVMEDIA_TYPE_AUDIO){
        myMeta.tracks[i].type = "audio";
        myMeta.tracks[i].rate = strm->codec->sample_rate;
        switch (strm->codec->sample_fmt){
          case AV_SAMPLE_FMT_U8:
          case AV_SAMPLE_FMT_U8P:
            myMeta.tracks[i].size = 8;
            break;
          case AV_SAMPLE_FMT_S16:
          case AV_SAMPLE_FMT_S16P:
            myMeta.tracks[i].size = 16;
            break;
          case AV_SAMPLE_FMT_S32:
          case AV_SAMPLE_FMT_S32P:
          case AV_SAMPLE_FMT_FLT:
          case AV_SAMPLE_FMT_FLTP:
            myMeta.tracks[i].size = 32;
            break;
          case AV_SAMPLE_FMT_DBL:
          case AV_SAMPLE_FMT_DBLP:
            myMeta.tracks[i].size = 64;
            break;
          default:
            myMeta.tracks[i].size = 0;
            break;
        }
        myMeta.tracks[i].channels = strm->codec->channels;
      }
    }
    
    AVPacket packet;
    while(av_read_frame(pFormatCtx, &packet)>=0){
      AVStream * strm = pFormatCtx->streams[packet.stream_index];
      JSON::Value pkt;
      pkt["trackid"] = (long long)packet.stream_index + 1;
      pkt["data"] = std::string((char*)packet.data, packet.size);
      pkt["time"] = (long long)(packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      if (pkt["time"].asInt() < 0){
        pkt["time"] = 0ll;
      }
      if (packet.flags & AV_PKT_FLAG_KEY && myMeta.tracks[(long long)packet.stream_index + 1].type != "audio"){
        pkt["keyframe"] = 1ll;
        pkt["bpos"] = (long long)packet.pos;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        pkt["offset"] = (long long)((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      myMeta.update(pkt);
      av_free_packet(&packet);
    }
    myMeta.live = false;
    myMeta.vod = true;
    
    //store dtsc-style header file for faster processing, later
    std::ofstream oFile(std::string(config->getString("input") + ".dtsh").c_str());
    oFile << myMeta.toJSON().toNetPacked();
    oFile.close();
    
    seek(0);
    return true;
  }
  
  void inputAV::getNext(bool smart) {
    AVPacket packet;
    while (av_read_frame(pFormatCtx, &packet)>=0){
      //filter tracks we don't care about
      if (!selectedTracks.count(packet.stream_index + 1)){
        DEBUG_MSG(DLVL_HIGH, "Track %u not selected", packet.stream_index + 1);
        continue;
      }
      AVStream * strm = pFormatCtx->streams[packet.stream_index];
      JSON::Value pkt;
      pkt["trackid"] = (long long)packet.stream_index + 1;
      pkt["data"] = std::string((char*)packet.data, packet.size);
      pkt["time"] = (long long)(packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      if (pkt["time"].asInt() < 0){
        pkt["time"] = 0ll;
      }
      if (packet.flags & AV_PKT_FLAG_KEY && myMeta.tracks[(long long)packet.stream_index + 1].type != "audio"){
        pkt["keyframe"] = 1ll;
        pkt["bpos"] = (long long)packet.pos;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        pkt["offset"] = (long long)((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      pkt.netPrepare();
      lastPack.reInit(pkt.toNetPacked().data(), pkt.toNetPacked().size());
      av_free_packet(&packet);
      return;//success!
    }
    lastPack.null();
    setup();
    //failure :-(
    DEBUG_MSG(DLVL_FAIL, "getNext failed");
  }

  void inputAV::seek(int seekTime) {
    
    DEBUG_MSG(DLVL_FAIL, "Seeking to %d from %ld", seekTime, pFormatCtx->pb->pos);

    int stream_index = av_find_default_stream_index(pFormatCtx);
    //Convert ts to frame
    unsigned long long reseekTime = av_rescale(seekTime, pFormatCtx->streams[stream_index]->time_base.den, pFormatCtx->streams[stream_index]->time_base.num);
    seekTime /= 1000;
   
    
    unsigned long long seekStreamDuration = pFormatCtx->streams[stream_index]->duration;
    
    int flags = AVSEEK_FLAG_BACKWARD;
    if (reseekTime > 0 && reseekTime < seekStreamDuration){
      flags |= AVSEEK_FLAG_ANY; // H.264 I frames don't always register as "key frames" in FFmpeg
    }
    int ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, flags);
    if (ret < 0){
      ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, AVSEEK_FLAG_ANY);
    }
    
    if (ret < 0) {
      DEBUG_MSG(DLVL_FAIL, "Unable to seek");
    } else {
      DEBUG_MSG(DLVL_FAIL, "Success: %ld", pFormatCtx->pb->pos);
      avcodec_flush_buffers(pFormatCtx->streams[stream_index]->codec);
    }
    
    
  }

  void inputAV::trackSelect(std::string trackSpec) {
    selectedTracks.clear();
    long long unsigned int index;
    while (trackSpec != "") {
      index = trackSpec.find(' ');
      selectedTracks.insert(atoi(trackSpec.substr(0, index).c_str()));
      if (index != std::string::npos) {
        trackSpec.erase(0, index + 1);
      } else {
        trackSpec = "";
      }
    }
    //inFile.selectTracks(selectedTracks);
  }
}

