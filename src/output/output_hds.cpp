#include "output_hds.h"
#include <mist/stream.h>
#include <unistd.h>
#include <mist/amf.h>
#include <mist/mp4_adobe.h>

namespace Mist {
  
  void OutHDS::getTracks(){
    /// \todo Why do we have only one audio track option?
    videoTracks.clear();
    audioTrack = 0;
    JSON::Value & vidCapa = capa["codecs"][0u][0u];
    JSON::Value & audCapa = capa["codecs"][0u][1u];
    for (std::map<unsigned int,DTSC::Track>::iterator it = myMeta.tracks.begin(); it != myMeta.tracks.end(); it++){
      jsonForEach(vidCapa, itb) {
        if (it->second.codec == (*itb).asStringRef()){
          videoTracks.insert(it->first);
          break;
        }
      }
      if (!audioTrack){
        jsonForEach(audCapa, itb) { 
          if (it->second.codec == (*itb).asStringRef()){
            audioTrack = it->first;
            break;
          }
        }
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
      std::deque<DTSC::Fragment>::iterator fragIt = myMeta.tracks[tid].fragments.begin();
      /*LTS-START*/
      if (myMeta.live){
        unsigned int skip = (( myMeta.tracks[tid].fragments.size()-1) * config->getInteger("startpos")) / 1000u;
        for (unsigned int z = 0; z < skip; ++z){
          ++fragIt;
          ++j;
        }
        if (skip && fragIt == myMeta.tracks[tid].fragments.end()){
          --fragIt;
          --j;
        }
      }
      /*LTS-END*/
      unsigned int firstTime = myMeta.tracks[tid].getKey(fragIt->getNumber()).getTime();
      while (fragIt != myMeta.tracks[tid].fragments.end()){
        if (myMeta.vod || fragIt->getDuration() > 0){
          afrtrun.firstFragment = myMeta.tracks[tid].missedFrags + j + 1;
          afrtrun.firstTimestamp = myMeta.tracks[tid].getKey(fragIt->getNumber()).getTime() - firstTime;
          if (fragIt->getDuration() > 0){
            afrtrun.duration = fragIt->getDuration();
          }else{
            afrtrun.duration = myMeta.tracks[tid].lastms - afrtrun.firstTimestamp;
          }
          afrt.setFragmentRun(afrtrun, i);
          ++i;
        }
        ++j;
        ++fragIt;
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
  
  OutHDS::OutHDS(Socket::Connection & conn) : HTTPOutput(conn) {
    audioTrack = 0;
    playUntil = 0;
  }

  OutHDS::~OutHDS() {}
  
  void OutHDS::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HDS";
    capa["desc"] = "Enables HTTP protocol Adobe-specific dynamic streaming (also known as HDS).";
    capa["url_rel"] = "/dynamic/$/manifest.f4m";
    capa["url_prefix"] = "/dynamic/$/";
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
    capa["methods"][0u]["type"] = "flash/11";
    capa["methods"][0u]["priority"] = 7ll;
    capa["methods"][0u]["player_url"] = "/flashplayer.swf";
    cfg->getOption("startpos", true)[0u] = 0ll;
  }
  
  void OutHDS::sendNext(){
    if (thisPacket.getTime() >= playUntil){
      VERYHIGH_MSG("Done sending fragment (%llu >= %llu)", thisPacket.getTime(), playUntil);
      stop();
      wantRequest = true;
      H.Chunkify("", 0, myConn);
      return;
    }
    tag.DTSCLoader(thisPacket, myMeta.tracks[thisPacket.getTrackId()]);
    if (tag.len){
      H.Chunkify(tag.data, tag.len, myConn);
    }
  }

  void OutHDS::onHTTP(){

    if (H.url.find(".abst") != std::string::npos){
      initialize();
      std::string streamID = H.url.substr(streamName.size() + 10);
      streamID = streamID.substr(0, streamID.find(".abst"));
      H.Clean();
      H.SetBody(dynamicBootstrap(atoll(streamID.c_str())));
      H.SetHeader("Content-Type", "binary/octet");
      H.SetHeader("Cache-Control", "no-cache");
      H.SendResponse("200", "OK", myConn);
      H.Clean(); //clean for any possible next requests
      return;
    }
    if (H.url.find("f4m") == std::string::npos){
      initialize();
      std::string tmp_qual = H.url.substr(H.url.find("/", 10) + 1);
      unsigned int tid;
      unsigned int fragNum;
      tid = atoi(tmp_qual.substr(0, tmp_qual.find("Seg") - 1).c_str());
      int temp;
      temp = H.url.find("Seg") + 3;
      temp = H.url.find("Frag") + 4;
      fragNum = atoi(H.url.substr(temp).c_str()) - 1;
      DEBUG_MSG(DLVL_MEDIUM, "Video track %d, fragment %d", tid, fragNum);
      if (!audioTrack){getTracks();}
      unsigned int mstime = 0;
      unsigned int mslen = 0;
      if (fragNum < (unsigned int)myMeta.tracks[tid].missedFrags){
        H.Clean();
        H.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
        H.SendResponse("412", "Fragment out of range", myConn);
        H.Clean(); //clean for any possible next requests
        std::cout << "Fragment " << fragNum << " too old" << std::endl;
        return;
      }
      //delay if we don't have the next fragment available yet
      unsigned int timeout = 0;
      while (myConn && fragNum >= myMeta.tracks[tid].missedFrags + myMeta.tracks[tid].fragments.size() - 1){
        //time out after 21 seconds
        if (++timeout > 42){
          myConn.close();
          break;
        }
        Util::sleep(500);
        updateMeta();
      }
      mstime = myMeta.tracks[tid].getKey(myMeta.tracks[tid].fragments[fragNum - myMeta.tracks[tid].missedFrags].getNumber()).getTime();
      mslen = myMeta.tracks[tid].fragments[fragNum - myMeta.tracks[tid].missedFrags].getDuration();
      VERYHIGH_MSG("Playing from %llu for %llu ms", mstime, mslen);
      
      selectedTracks.clear();
      selectedTracks.insert(tid);
      if (audioTrack){
        selectedTracks.insert(audioTrack);
      }
      seek(mstime);
      playUntil = mstime + mslen;
      
      H.Clean();
      H.SetHeader("Content-Type", "video/mp4");
      H.StartResponse(H, myConn);
      //send the bootstrap
      std::string bootstrap = dynamicBootstrap(tid);
      H.Chunkify(bootstrap, myConn);
      //send a zero-size mdat, meaning it stretches until end of file.
      H.Chunkify("\000\000\000\000mdat", 8, myConn);
      //send init data, if needed.
      if (audioTrack > 0 && myMeta.tracks[audioTrack].init != ""){
        if (tag.DTSCAudioInit(myMeta.tracks[audioTrack])){
          tag.tagTime(mstime);
          H.Chunkify(tag.data, tag.len, myConn);
        }
      }
      if (tid > 0){
        if (tag.DTSCVideoInit(myMeta.tracks[tid])){
          tag.tagTime(mstime);
          H.Chunkify(tag.data, tag.len, myConn);
        }
      }
      parseData = true;
      wantRequest = false;
    }else{
      initialize();
      std::stringstream tmpstr;
      myMeta.toPrettyString(tmpstr);
      H.Clean();
      H.SetHeader("Content-Type", "text/xml");
      H.SetHeader("Cache-Control", "no-cache");
      H.SetBody(dynamicIndex());
      H.SendResponse("200", "OK", myConn);
    }
  }
}
