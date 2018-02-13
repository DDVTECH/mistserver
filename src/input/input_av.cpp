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
    capa["name"] = "AV";
    capa["desc"] = "This input uses libavformat to read any type of file. Unfortunately this input cannot be redistributed, but it is a great tool for testing the other file-based inputs against.";
    capa["source_match"] = "/*";
    capa["priority"] = 1ll;
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
  
  bool inputAV::checkArguments() {
    if (config->getString("input") == "-") {
      std::cerr << "Input from stdin not yet supported" << std::endl;
      return false;
    }
    if (!config->getString("streamname").size()){
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
    return true;
  }

  bool inputAV::preRun(){
    //make sure all av inputs are registered properly, just in case
    //the constructor already does this, but under windows it doesn't remember that it has.
    //Very sad, that. We may need to get windows some medication for it.
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
    myMeta.tracks.clear();
    for(unsigned int i=0; i < pFormatCtx->nb_streams; ){
      AVStream * strm = pFormatCtx->streams[i++];
      myMeta.tracks[i].trackID = i;
      switch (strm->codecpar->codec_id){
        case AV_CODEC_ID_HEVC:
          myMeta.tracks[i].codec = "HEVC";
          break;
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_MPEG2VIDEO:
          myMeta.tracks[i].codec = "MPEG2";
          break;
        case AV_CODEC_ID_MP2:
          myMeta.tracks[i].codec = "MP2";
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
        case AV_CODEC_ID_AC3:
        case AV_CODEC_ID_EAC3:
          myMeta.tracks[i].codec = "AC3";
          break;  
        default:
          const AVCodecDescriptor *desc = avcodec_descriptor_get(strm->codecpar->codec_id);
          if (desc && desc->name){
            myMeta.tracks[i].codec = desc->name;
          }else{
            myMeta.tracks[i].codec = "?";
          }
          break;
      }
      if (strm->codecpar->extradata_size){
        myMeta.tracks[i].init = std::string((char*)strm->codecpar->extradata, strm->codecpar->extradata_size);
      }
      if(strm->codecpar->codec_type == AVMEDIA_TYPE_VIDEO){
        myMeta.tracks[i].type = "video";
        if (strm->avg_frame_rate.den && strm->avg_frame_rate.num){
          myMeta.tracks[i].fpks = (strm->avg_frame_rate.num * 1000) / strm->avg_frame_rate.den;
        }else{
          myMeta.tracks[i].fpks = 0;
        }
        myMeta.tracks[i].width = strm->codecpar->width;
        myMeta.tracks[i].height = strm->codecpar->height;
      }
      if(strm->codecpar->codec_type == AVMEDIA_TYPE_AUDIO){
        myMeta.tracks[i].type = "audio";
        myMeta.tracks[i].rate = strm->codecpar->sample_rate;
        myMeta.tracks[i].size = strm->codecpar->frame_size;
        myMeta.tracks[i].channels = strm->codecpar->channels;
      }
    }
    
    AVPacket packet;
    while(av_read_frame(pFormatCtx, &packet)>=0){
      AVStream * strm = pFormatCtx->streams[packet.stream_index];
      long long packTime = (packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      long long packOffset = 0;
      bool isKey = false;
      if (packTime < 0){
        packTime = 0;
      }
      if (packet.flags & AV_PKT_FLAG_KEY && myMeta.tracks[(long long)packet.stream_index + 1].type != "audio"){
        isKey = true;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        packOffset = ((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      myMeta.update(packTime, packOffset, packet.stream_index + 1, packet.size, packet.pos, isKey);
      av_packet_unref(&packet);
    }
    
    myMeta.toFile(config->getString("input") + ".dtsh");
    
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
      long long packTime = (packet.dts * 1000 * strm->time_base.num / strm->time_base.den);
      long long packOffset = 0;
      bool isKey = false;
      if (packTime < 0){
        packTime = 0;
      }
      if (packet.flags & AV_PKT_FLAG_KEY && myMeta.tracks[(long long)packet.stream_index + 1].type != "audio"){
        isKey = true;
      }
      if (packet.pts != AV_NOPTS_VALUE && packet.pts != packet.dts){
        packOffset = ((packet.pts - packet.dts) * 1000 * strm->time_base.num / strm->time_base.den);
      }
      thisPacket.genericFill(packTime, packOffset, packet.stream_index + 1, (const char*)packet.data, packet.size, 0, isKey);
      av_packet_unref(&packet);
      return;//success!
    }
    thisPacket.null();
    preRun();
    //failure :-(
    DEBUG_MSG(DLVL_FAIL, "getNext failed");
  }

  void inputAV::seek(int seekTime) {
    int stream_index = av_find_default_stream_index(pFormatCtx);
    //Convert ts to frame
    unsigned long long reseekTime = av_rescale(seekTime, pFormatCtx->streams[stream_index]->time_base.den, pFormatCtx->streams[stream_index]->time_base.num);
    reseekTime /= 1000;
    unsigned long long seekStreamDuration = pFormatCtx->streams[stream_index]->duration;
    int flags = AVSEEK_FLAG_BACKWARD;
    if (reseekTime > 0 && reseekTime < seekStreamDuration){
      flags |= AVSEEK_FLAG_ANY; // H.264 I frames don't always register as "key frames" in FFmpeg
    }
    int ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, flags);
    if (ret < 0){
      ret = av_seek_frame(pFormatCtx, stream_index, reseekTime, AVSEEK_FLAG_ANY);
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

