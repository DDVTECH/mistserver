#include "output_hds.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <unistd.h>

#include <mist/amf.h>
#include <mist/mp4_adobe.h>

namespace Mist {
  
  void OutHDS::getTracks(){
    /// \todo Why do we have only one audio track option?
    /// \todo We should really support all Flash-supported codecs in HDS. These lists are too short now.
    videoTracks.clear();
    audioTrack = 0;
    for (std::map<int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      if (it->second.codec == "H264" || it->second.codec == "H263" || it->second.codec == "VP6"){
        videoTracks.insert(it->first);
      }
      if (it->second.codec == "AAC" || it->second.codec == "MP3"){
        audioTrack = it->first;
      }
    }
  }
  
  ///\brief Builds a bootstrap for use in HTTP Dynamic streaming.
  ///\param tid The track this bootstrap is generated for.
  ///\return The generated bootstrap.
  std::string OutHDS::dynamicBootstrap(int tid){
    updateMeta();
    std::string empty;
    
    MP4::ASRT asrt;
    asrt.setUpdate(false);
    asrt.setVersion(1);
    //asrt.setQualityEntry(empty, 0);
    if (myMeta.live){
      asrt.setSegmentRun(1, 4294967295ul, 0);
    }else{
      asrt.setSegmentRun(1, myMeta.tracks[tid].fragments.size(), 0);
    }
    
    MP4::AFRT afrt;
    afrt.setUpdate(false);
    afrt.setVersion(1);
    afrt.setTimeScale(1000);
    //afrt.setQualityEntry(empty, 0);
    MP4::afrt_runtable afrtrun;
    int i = 0;
    int j = 0;
    if (myMeta.tracks[tid].fragments.size()){
      unsigned int firstTime = myMeta.tracks[tid].getKey(myMeta.tracks[tid].fragments.begin()->getNumber()).getTime();
      for (std::deque<DTSC::Fragment>::iterator it = myMeta.tracks[tid].fragments.begin(); it != myMeta.tracks[tid].fragments.end(); it++){
        if (myMeta.vod || it->getDuration() > 0){
          afrtrun.firstFragment = myMeta.tracks[tid].missedFrags + j + 1;
          afrtrun.firstTimestamp = myMeta.tracks[tid].getKey(it->getNumber()).getTime() - firstTime;
          if (it->getDuration() > 0){
            afrtrun.duration = it->getDuration();
          }else{
            afrtrun.duration = myMeta.tracks[tid].lastms - afrtrun.firstTimestamp;
          }
          afrt.setFragmentRun(afrtrun, i);
          i++;
        }
        j++;
      }
    }
    
    MP4::ABST abst;
    abst.setVersion(1);
    abst.setBootstrapinfoVersion(1);
    abst.setProfile(0);
    abst.setUpdate(false);
    abst.setTimeScale(1000);
    abst.setLive(myMeta.live);
    abst.setCurrentMediaTime(myMeta.tracks[tid].lastms);
    abst.setSmpteTimeCodeOffset(0);
    abst.setMovieIdentifier(streamName);
    abst.setSegmentRunTable(asrt, 0);
    abst.setFragmentRunTable(afrt, 0);
    
    DEBUG_MSG(DLVL_VERYHIGH, "Sending bootstrap: %s", abst.toPrettyString(0).c_str());
    return std::string((char*)abst.asBox(), (int)abst.boxedSize());
  }
  
  ///\brief Builds an index file for HTTP Dynamic streaming.
  ///\return The index file for HTTP Dynamic Streaming.
  std::string OutHDS::dynamicIndex(){
    getTracks();
    std::stringstream Result;
    Result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
    Result << "  <manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">" << std::endl;
    Result << "  <id>" << streamName << "</id>" << std::endl;
    Result << "  <mimeType>video/mp4</mimeType>" << std::endl;
    Result << "  <deliveryType>streaming</deliveryType>" << std::endl;
    if (myMeta.vod){
      Result << "  <duration>" << myMeta.tracks[*videoTracks.begin()].lastms / 1000 << ".000</duration>" << std::endl;
      Result << "  <streamType>recorded</streamType>" << std::endl;
    }else{
      Result << "  <duration>0.00</duration>" << std::endl;
      Result << "  <streamType>live</streamType>" << std::endl;
    }
    for (std::set<int>::iterator it = videoTracks.begin(); it != videoTracks.end(); it++){
      Result << "  <bootstrapInfo "
      "profile=\"named\" "
      "id=\"boot" << (*it) << "\" "
      "url=\"" << (*it) << ".abst\">"
      "</bootstrapInfo>" << std::endl;
      Result << "  <media "
      "url=\"" << (*it) << "-\" "
      "bitrate=\"" << myMeta.tracks[(*it)].bps * 8 << "\" "
      "bootstrapInfoId=\"boot" << (*it) << "\" "
      "width=\"" << myMeta.tracks[(*it)].width << "\" "
      "height=\"" << myMeta.tracks[(*it)].height << "\">" << std::endl;
      Result << "    <metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>" << std::endl;
      Result << "  </media>" << std::endl;
    }
    Result << "</manifest>" << std::endl;
    DEBUG_MSG(DLVL_HIGH, "Sending manifest: %s", Result.str().c_str());
    return Result.str();
  } //BuildManifest
  
  OutHDS::OutHDS(Socket::Connection & conn) : Output(conn) {
    audioTrack = 0;
    playUntil = 0;
    myConn.setHost(config->getString("ip"));
    streamName = config->getString("streamname");
  }

  void OutHDS::onFail(){
    HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
    HTTP_S.SetBody("Stream not found. Sorry, we tried.");
    HTTP_S.SendResponse("404", "Stream not found", myConn);
    Output::onFail();
  }
  
  OutHDS::~OutHDS() {}
  
  void OutHDS::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "HDS";
    capa["desc"] = "Enables HTTP protocol Adobe-specific dynamic streaming (also known as HDS).";
    capa["deps"] = "HTTP";
    capa["url_rel"] = "/dynamic/$/manifest.f4m";
    capa["url_prefix"] = "/dynamic/$/";
    capa["socket"] = "http_hds";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][0u].append("H263");
    capa["codecs"][0u][0u].append("VP6");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "flash/11";
    capa["methods"][0u]["priority"] = 7ll;
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }
  
  void OutHDS::sendNext(){
    if (currentPacket.getTime() >= playUntil){
      DEBUG_MSG(DLVL_DEVEL, "(%d) Done sending fragment", getpid() );
      stop();
      wantRequest = true;
      HTTP_S.Chunkify("", 0, myConn);
      return;
    }
    tag.DTSCLoader(currentPacket, myMeta.tracks[currentPacket.getTrackId()]);
    HTTP_S.Chunkify(tag.data, tag.len, myConn);
  }

  void OutHDS::onRequest(){
    HTTP_R.Clean();
    while (HTTP_R.Read(myConn)){
      DEBUG_MSG(DLVL_DEVEL, "Received request: %s", HTTP_R.getUrl().c_str());
      if (HTTP_R.url.find(".abst") != std::string::npos){
        initialize();
        std::string streamID = HTTP_R.url.substr(streamName.size() + 10);
        streamID = streamID.substr(0, streamID.find(".abst"));
        HTTP_S.Clean();
        HTTP_S.SetBody(dynamicBootstrap(atoll(streamID.c_str())));
        HTTP_S.SetHeader("Content-Type", "binary/octet");
        HTTP_S.SetHeader("Cache-Control", "no-cache");
        HTTP_S.SendResponse("200", "OK", myConn);
        HTTP_R.Clean(); //clean for any possible next requests
        continue;
      }
      if (HTTP_R.url.find("f4m") == std::string::npos){
        initialize();
        std::string tmp_qual = HTTP_R.url.substr(HTTP_R.url.find("/", 10) + 1);
        unsigned int tid;
        unsigned int fragNum;
        tid = atoi(tmp_qual.substr(0, tmp_qual.find("Seg") - 1).c_str());
        int temp;
        temp = HTTP_R.url.find("Seg") + 3;
        temp = HTTP_R.url.find("Frag") + 4;
        fragNum = atoi(HTTP_R.url.substr(temp).c_str()) - 1;
        DEBUG_MSG(DLVL_MEDIUM, "Video track %d, fragment %d\n", tid, fragNum);
        if (!audioTrack){getTracks();}
        unsigned int mstime = 0;
        unsigned int mslen = 0;
        if (fragNum < (unsigned int)myMeta.tracks[tid].missedFrags){
          HTTP_S.Clean();
          HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
          HTTP_S.SendResponse("412", "Fragment out of range", myConn);
          HTTP_R.Clean(); //clean for any possible next requests
          std::cout << "Fragment " << fragNum << " too old" << std::endl;
          continue;
        }
        if (fragNum > myMeta.tracks[tid].missedFrags + myMeta.tracks[tid].fragments.size() - 1){
          HTTP_S.Clean();
          HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
          HTTP_S.SendResponse("208", "Ask again later", myConn);
          HTTP_R.Clean(); //clean for any possible next requests
          std::cout << "Fragment after fragment " << fragNum << " not available yet" << std::endl;
          continue;
        }
        mstime = myMeta.tracks[tid].getKey(myMeta.tracks[tid].fragments[fragNum - myMeta.tracks[tid].missedFrags].getNumber()).getTime();
        mslen = myMeta.tracks[tid].fragments[fragNum - myMeta.tracks[tid].missedFrags].getDuration();
        
        selectedTracks.clear();
        selectedTracks.insert(tid);
        if (audioTrack){
          selectedTracks.insert(audioTrack);
        }
        seek(mstime);
        playUntil = mstime + mslen;
        
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "video/mp4");
        HTTP_S.StartResponse(HTTP_R, myConn);
        //send the bootstrap
        std::string bootstrap = dynamicBootstrap(tid);
        HTTP_S.Chunkify(bootstrap, myConn);
        //send a zero-size mdat, meaning it stretches until end of file.
        HTTP_S.Chunkify("\000\000\000\000mdat", 8, myConn);
        //send init data, if needed.
        if (audioTrack > 0 && myMeta.tracks[audioTrack].init != ""){
          if (tag.DTSCAudioInit(myMeta.tracks[audioTrack])){
            tag.tagTime(mstime);
            HTTP_S.Chunkify(tag.data, tag.len, myConn);
          }
        }
        if (tid > 0){
          if (tag.DTSCVideoInit(myMeta.tracks[tid])){
            tag.tagTime(mstime);
            HTTP_S.Chunkify(tag.data, tag.len, myConn);
          }
        }
        parseData = true;
        wantRequest = false;
      }else{
        initialize();
        std::stringstream tmpstr;
        myMeta.toPrettyString(tmpstr);
        HTTP_S.Clean();
        HTTP_S.SetHeader("Content-Type", "text/xml");
        HTTP_S.SetHeader("Cache-Control", "no-cache");
        HTTP_S.SetBody(dynamicIndex());
        HTTP_S.SendResponse("200", "OK", myConn);
      }
      HTTP_R.Clean(); //clean for any possible next requests
    }
  }
}
