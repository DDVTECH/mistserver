/// \file conn_http_dynamic.cpp
/// Contains the main code for the HTTP Dynamic Connector

#include <iostream>
#include <sstream>
#include <queue>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/base64.h>
#include <mist/amf.h>
#include <mist/mp4.h>
#include <mist/config.h>
#include <sstream>
#include <mist/stream.h>
#include <mist/timing.h>

/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  
  std::set<std::string> videoTracks;///<< Holds valid video tracks for playback
  long long int audioTrack = 0;///<< Holds audio track ID for playback
  void getTracks(JSON::Value & metadata){
    videoTracks.clear();
    for (JSON::ObjIter it = metadata["tracks"].ObjBegin(); it != metadata["tracks"].ObjEnd(); it++){
      if (it->second["type"] == "video"){
        videoTracks.insert(it->first);
      }
      if (it->second["type"] == "audio"){
        audioTrack = it->second["trackid"].asInt();
      }
    }
  }
  
  
  ///\brief Builds a bootstrap for use in HTTP Dynamic streaming.
  ///\param streamName The name of the stream.
  ///\param metadata The current metadata, used to generate the index.
  ///\param fragnum The index of the current fragment
  ///\return The generated bootstrap.
  std::string dynamicBootstrap(std::string & streamName, JSON::Value & metadata, bool isLive = false, int fragnum = 0){
    std::string empty;
    
    MP4::ASRT asrt;
    asrt.setUpdate(false);
    asrt.setVersion(1);
    //asrt.setQualityEntry(empty, 0);
    if (isLive){
      asrt.setSegmentRun(1, 4294967295ul, 0);
    }else{
      asrt.setSegmentRun(1, metadata["keys"].size(), 0);
    }
    
    MP4::AFRT afrt;
    afrt.setUpdate(false);
    afrt.setVersion(1);
    afrt.setTimeScale(1000);
    //afrt.setQualityEntry(empty, 0);
    MP4::afrt_runtable afrtrun;
    long long int currTime = 0;
    for (int i = 0; i < metadata["keys"].size(); i++){
      if (metadata["keys"][i]["len"].asInt() > 0){
        afrtrun.firstFragment = metadata["keys"][i]["num"].asInt();
        afrtrun.firstTimestamp = metadata["keys"][i]["time"].asInt();
        afrtrun.duration = metadata["keys"][i]["len"].asInt();
        currTime = afrtrun.firstTimestamp + afrtrun.duration;
        afrt.setFragmentRun(afrtrun, i);
      }
    }
    
    MP4::ABST abst;
    abst.setVersion(1);
    abst.setBootstrapinfoVersion(1);
    abst.setProfile(0);
    abst.setUpdate(false);
    abst.setTimeScale(1000);
    abst.setLive(isLive);
    abst.setCurrentMediaTime(metadata["lastms"].asInt());
    abst.setSmpteTimeCodeOffset(0);
    abst.setMovieIdentifier(streamName);
    abst.setSegmentRunTable(asrt, 0);
    abst.setFragmentRunTable(afrt, 0);
    
    #if DEBUG >= 8
    std::cout << "Sending bootstrap:" << std::endl << abst.toPrettyString(0) << std::endl;
    #endif
    return std::string((char*)abst.asBox(), (int)abst.boxedSize());
  }
  
  ///\brief Builds an index file for HTTP Dynamic streaming.
  ///\param streamName The name of the stream.
  ///\param metadata The current metadata, used to generate the index.
  ///\return The index file for HTTP Dynamic Streaming.
  std::string dynamicIndex(std::string & streamName, JSON::Value & metadata){
    if ( !audioTrack){getTracks(metadata);}
    std::stringstream Result;
    Result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>" << std::endl;
    Result << "  <manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">" << std::endl;
    Result << "  <id>" << streamName << "</id>" << std::endl;
    Result << "  <mimeType>video/mp4</mimeType>" << std::endl;
    Result << "  <deliveryType>streaming</deliveryType>" << std::endl;
    if (metadata.isMember("vod")){
      Result << "  <duration>" << metadata["length"].asInt() << ".000</duration>" << std::endl;
      Result << "  <streamType>recorded</streamType>" << std::endl;
    }else{
      Result << "  <duration>0.00</duration>" << std::endl;
      Result << "  <streamType>live</streamType>" << std::endl;
    }
    for (std::set<std::string>::iterator it = videoTracks.begin(); it != videoTracks.end(); it++){
      Result << "  <bootstrapInfo "
      "profile=\"named\" "
      "id=\"boot" << metadata["tracks"][(*it)]["trackid"].asInt() << "\" "
      "url=\"" << metadata["tracks"][(*it)]["trackid"].asInt() << ".abst\">"
      "</bootstrapInfo>" << std::endl;
    }
    for (std::set<std::string>::iterator it = videoTracks.begin(); it != videoTracks.end(); it++){
      Result << "  <media "
      "url=\"" << metadata["tracks"][(*it)]["trackid"].asInt() << "-\" "
      "bitrate=\"" << metadata["tracks"][(*it)]["bps"].asInt() * 8 << "\" "
      "bootstrapInfoId=\"boot" << metadata["tracks"][(*it)]["trackid"].asInt() << "\" "
      "width=\"" << metadata["tracks"][(*it)]["width"].asInt() << "\" "
      "height=\"" << metadata["tracks"][(*it)]["height"].asInt() << "\">" << std::endl;
      Result << "    <metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>" << std::endl;
      Result << "  </media>" << std::endl;
    }
    Result << "</manifest>" << std::endl;
    #if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result.str() << std::endl;
    #endif
    return Result.str();
  } //BuildManifest
  
  ///\brief Main function for the HTTP Dynamic Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int dynamicConnector(Socket::Connection conn){
    FLV::Tag tmp; //temporary tag
    
    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; //HTTP Receiver en HTTP Sender.
    
    Socket::Connection ss( -1);
    std::string streamname;
    bool handlingRequest = false;
    
    int Quality = 0;
    int Segment = -1;
    int ReqFragment = -1;
    long long mstime = 0;
    long long mslen = 0;
    unsigned int lastStats = 0;
    conn.setBlocking(false); //do not block on conn.spool() when no data is available
    
    while (conn.connected()){
      if ( !handlingRequest){
        if (conn.spool() || conn.Received().size()){
          if (HTTP_R.Read(conn)){
            #if DEBUG >= 5
            std::cout << "Received request: " << HTTP_R.getUrl() << std::endl;
            #endif
            conn.setHost(HTTP_R.GetHeader("X-Origin"));
            streamname = HTTP_R.GetHeader("X-Stream");
            if ( !ss){
              ss = Util::Stream::getStream(streamname);
              if ( !ss.connected()){
                HTTP_S.Clean();
                HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
                HTTP_S.SendResponse("404", "Not found", conn);
                continue;
              }
              Strm.waitForMeta(ss);
            }
            if (HTTP_R.url.find(".abst") != std::string::npos){
              std::string streamID = HTTP_R.url.substr(streamname.size() + 10);
              streamID = streamID.substr(0, streamID.find(".abst"));
              HTTP_S.Clean();
              HTTP_S.SetBody(dynamicBootstrap(streamname, Strm.getTrackById(atoll(streamID.c_str())),Strm.metadata.isMember("live")));
              HTTP_S.SetHeader("Content-Type", "binary/octet");
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              HTTP_S.SendResponse("200", "OK", conn);
              HTTP_R.Clean(); //clean for any possible next requests
              continue;
            }
            if (HTTP_R.url.find("f4m") == std::string::npos){
              std::string tmp_qual = HTTP_R.url.substr(HTTP_R.url.find("/", 10) + 1);
              Quality = atoi(tmp_qual.substr(0, tmp_qual.find("Seg") - 1).c_str());
              int temp;
              temp = HTTP_R.url.find("Seg") + 3;
              Segment = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find("-", temp) - temp).c_str());
              temp = HTTP_R.url.find("Frag") + 4;
              ReqFragment = atoi(HTTP_R.url.substr(temp).c_str());
              #if DEBUG >= 5
              printf("Video track %d, segment %d, fragment %d\n", Quality, Segment, ReqFragment);
              #endif
              if (!audioTrack){getTracks(Strm.metadata);}
              JSON::Value & vidTrack = Strm.getTrackById(Quality);
              mstime = 0;
              mslen = 0;
              if (vidTrack.isMember("keys")){
                for (JSON::ArrIter it = vidTrack["keys"].ArrBegin(); it != vidTrack["keys"].ArrEnd(); it++){
                  if ((*it)["num"].asInt() >= ReqFragment){
                    mstime = (*it)["time"].asInt();
                    mslen = (*it)["len"].asInt();
                    if (Strm.metadata.isMember("live")){
                      if (it == vidTrack["keys"].ArrEnd() - 2){
                        HTTP_S.Clean();
                        HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
                        HTTP_S.SendResponse("208", "Ask again later", conn);
                        HTTP_R.Clean(); //clean for any possible next requests
                        std::cout << "Fragment after fragment " << ReqFragment << " not available yet" << std::endl;
                        if (ss.spool()){
                          while (Strm.parsePacket(ss.Received())){}
                        }
                      }
                    }
                    break;
                  }
                }
              }
              if (HTTP_R.url == "/"){continue;}//Don't continue, but continue instead.
              if (Strm.metadata.isMember("live")){
                if (mstime == 0 && ReqFragment > 1){
                  HTTP_S.Clean();
                  HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
                  HTTP_S.SendResponse("412", "Fragment out of range", conn);
                  HTTP_R.Clean(); //clean for any possible next requests
                  std::cout << "Fragment " << ReqFragment << " too old" << std::endl;
                  continue;
                }
              }
              std::stringstream sstream;
              sstream << "t " << Quality << " " << audioTrack << "\ns " << mstime << "\np " << (mstime + mslen) << "\n";
              ss.SendNow(sstream.str().c_str());
              
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type", "video/mp4");
              HTTP_S.StartResponse(HTTP_R, conn);
              //send the bootstrap
              std::string bootstrap = dynamicBootstrap(streamname, Strm.getTrackById(Quality), Strm.metadata.isMember("live"), ReqFragment);
              HTTP_S.Chunkify(bootstrap, conn);
              //send a zero-size mdat, meaning it stretches until end of file.
              HTTP_S.Chunkify("\000\000\000\000mdat", 8, conn);
              //send init data, if needed.
              if (audioTrack > 0 && Strm.getTrackById(audioTrack).isMember("init")){
                tmp.DTSCAudioInit(Strm.getTrackById(audioTrack));
                tmp.tagTime(mstime);
                HTTP_S.Chunkify(tmp.data, tmp.len, conn);
              }
              if (Quality > 0 && Strm.getTrackById(Quality).isMember("init")){
                tmp.DTSCVideoInit(Strm.getTrackById(Quality));
                tmp.tagTime(mstime);
                HTTP_S.Chunkify(tmp.data, tmp.len, conn);
              }
              handlingRequest = true;
            }else{
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type", "text/xml");
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              HTTP_S.SetBody(dynamicIndex(streamname, Strm.metadata));
              HTTP_S.SendResponse("200", "OK", conn);
            }
            HTTP_R.Clean(); //clean for any possible next requests
          }
        }else{
          //sleep for 250ms before next attempt
          Util::sleep(250);
        }
      }
      if (ss.connected()){
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Dynamic").c_str());
        }
        if (handlingRequest && ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              //send an empty chunk to signify request is done
              HTTP_S.Chunkify("", 0, conn);
              handlingRequest = false;
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              //send a chunk with the new data
              tmp.DTSCLoader(Strm);
              HTTP_S.Chunkify(tmp.data, tmp.len, conn);
            }
          }
        }
        if ( !ss.connected()){
          break;
        }
      }
    }
    conn.close();
    ss.SendNow(conn.getStats("HTTP_Dynamic").c_str());
    ss.close();
    return 0;
  } //Connector_HTTP_Dynamic main function
  
} //Connector_HTTP_Dynamic namespace

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["desc"] = "Enables HTTP protocol Adobe-specific dynamic streaming (also known as HDS).";
  capa["deps"] = "HTTP";
  capa["url_rel"] = "/dynamic/$/manifest.f4m";
  capa["url_prefix"] = "/dynamic/$/";
  capa["socket"] = "http_dynamic";
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][0u].append("H263");
  capa["codecs"][0u][0u].append("VP6");
  capa["codecs"][0u][1u].append("AAC");
  capa["codecs"][0u][1u].append("MP3");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "flash/11";
  capa["methods"][0u]["priority"] = 7ll;
  conf.addBasicConnectorOptions(capa);
  conf.parseArgs(argc, argv);
  
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  
  Socket::Server server_socket = Socket::Server(Util::getTmpFolder() + capa["socket"].asStringRef());
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();
  
  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::dynamicConnector(S);
      }else{ //otherwise, do nothing or output debugging text
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
        #endif
      }
    }
  } //while connected
  server_socket.close();
  return 0;
} //main
