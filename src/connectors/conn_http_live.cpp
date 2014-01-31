/// \file conn_http_dynamic.cpp
/// Contains the main code for the HTTP Dynamic Connector

#include <iostream>
#include <iomanip>
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
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/config.h>
#include <sstream>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>

/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  ///\brief Builds an index file for HTTP Live streaming.
  ///\param metadata The current metadata, used to generate the index.
  ///\param isLive Whether or not the stream is live.
  ///\return The index file for HTTP Live Streaming.
  std::string liveIndex(DTSC::Meta & metadata, bool isLive){
    std::stringstream result;
    result << "#EXTM3U\r\n";
    int audioId = -1;
    std::string audioName;
    for (std::map<int,DTSC::Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
      if (it->second.codec == "AAC"){
        audioId = it->first;
        audioName = it->second.getIdentifier();
        break;
      }
    }
    for (std::map<int,DTSC::Track>::iterator it = metadata.tracks.begin(); it != metadata.tracks.end(); it++){
      if (it->second.codec == "H264"){
        int bWidth = it->second.bps * 2;
        if (audioId != -1){
          bWidth += metadata.tracks[audioId].bps * 2;
        }
        result << "#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=" << bWidth * 10 << "\r\n";
        result << it->first;
        if (audioId != -1){
          result << "_" << audioId;
        }
        result << "/index.m3u8\r\n";
      }
    }
#if DEBUG >= 8
    std::cerr << "Sending this index:" << std::endl << result.str() << std::endl;
#endif
    return result.str();
  }

  std::string liveIndex(DTSC::Track & metadata, bool isLive){
    std::stringstream result;
    //parse single track
    int longestFragment = 0;
    for (std::deque<DTSC::Fragment>::iterator it = metadata.fragments.begin(); (it + 1) != metadata.fragments.end(); it++){
      if (it->getDuration() > longestFragment){
        longestFragment = it->getDuration();
      }
    }
    result << "#EXTM3U\r\n"
        "#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n"
        "#EXT-X-MEDIA-SEQUENCE:" << metadata.missedFrags << "\r\n";
    for (std::deque<DTSC::Fragment>::iterator it = metadata.fragments.begin(); it != metadata.fragments.end(); it++){
      long long int starttime = metadata.getKey(it->getNumber()).getTime();
      
      if (it != (metadata.fragments.end() - 1)){
        result << "#EXTINF:" << ((it->getDuration() + 500) / 1000) << ", no desc\r\n" << starttime << "_" << it->getDuration() + starttime << ".ts\r\n";
      }
    }
    if ( !isLive){
      result << "#EXT-X-ENDLIST\r\n";
    }
#if DEBUG >= 8
    std::cerr << "Sending this index:" << std::endl << result.str() << std::endl;
#endif
    return result.str();
  } //liveIndex

  ///\brief Main function for the HTTP Live Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int liveConnector(Socket::Connection & conn){
    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; //HTTP Receiver en HTTP Sender.

    bool ready4data = false; //Set to true when streaming is to begin.
    bool AppleCompat = false; //Set to true when Apple device detected.
    Socket::Connection ss( -1);
    std::string streamname;
    bool handlingRequest = false;
    std::string recBuffer = "";

    TS::Packet PackData;
    int PacketNumber = 0;
    long long unsigned int TimeStamp = 0;
    unsigned int ThisNaluSize;
    char VideoCounter = 0;
    char AudioCounter = 0;
    long long unsigned int lastVid = 0;
    bool IsKeyFrame = false;
    MP4::AVCC avccbox;
    bool haveAvcc = false;

    std::vector<int> fragIndices;

    std::string manifestType;

    int Segment = -1;
    int temp;
    int trackID = 0;
    int audioTrackID = 0;
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
            AppleCompat = (HTTP_R.GetHeader("User-Agent").find("Apple") != std::string::npos);
            streamname = HTTP_R.GetHeader("X-Stream");
            if ( !ss){
              ss = Util::Stream::getStream(streamname);
              if ( !ss.connected()){
                #if DEBUG >= 1
                fprintf(stderr, "Could not connect to server!\n");
                #endif
                HTTP_S.Clean();
                HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
                conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
                ready4data = false;
                continue;
              }
              ss.setBlocking(false);
              Strm.waitForMeta(ss);
            }
            if (HTTP_R.url.find(".m3u") == std::string::npos){
              temp = HTTP_R.url.find("/", 5) + 1;
              std::string allTracks = HTTP_R.url.substr(temp, HTTP_R.url.find("/", temp) - temp);
              trackID = atoi(allTracks.c_str());
              audioTrackID = atoi(allTracks.substr(allTracks.find("_")+1).c_str());
              temp = HTTP_R.url.find("/", temp) + 1;
              Segment = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find("_", temp) - temp).c_str());
              lastVid = Segment * 90;
              temp = HTTP_R.url.find("_", temp) + 1;
              int frameCount = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find(".ts", temp) - temp).c_str());
              if (Strm.metadata.live){
                int seekable = Strm.canSeekms(Segment);
                if (seekable < 0){
                  HTTP_S.Clean();
                  HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
                  conn.SendNow(HTTP_S.BuildResponse("412", "Fragment out of range"));
                  HTTP_R.Clean(); //clean for any possible next requests
                  std::cout << "Fragment @ " << Segment << " too old" << std::endl;
                  continue;
                }
                if (seekable > 0){
                  HTTP_S.Clean();
                  HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
                  conn.SendNow(HTTP_S.BuildResponse("208", "Ask again later"));
                  HTTP_R.Clean(); //clean for any possible next requests
                  std::cout << "Fragment @ " << Segment << " not available yet" << std::endl;
                  continue;
                }
              }
              for (unsigned int i = 0; i < allTracks.size(); i++){
                if (allTracks[i] == '_'){
                  allTracks[i] = ' ';
                }
              }
              std::stringstream sstream;
              sstream << "t " << allTracks << "\n";
              sstream << "s " << Segment << "\n";
              sstream << "p " << frameCount << "\n";
              ss.SendNow(sstream.str().c_str());
              
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type", "video/mp2t");
              HTTP_S.StartResponse(HTTP_R, conn);
              handlingRequest = true;
            }else{
              std::string request = HTTP_R.url.substr(HTTP_R.url.find("/", 5) + 1);
              if (HTTP_R.url.find(".m3u8") != std::string::npos){
                manifestType = "audio/x-mpegurl";
              }else{
                manifestType = "audio/mpegurl";
              }
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type", manifestType);
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              std::string manifest;
              if (request.find("/") == std::string::npos){
                manifest = liveIndex(Strm.metadata, Strm.metadata.live);
              }else{
                int selectId = atoi(request.substr(0,request.find("/")).c_str());
                manifest = liveIndex(Strm.metadata.tracks[selectId], Strm.metadata.live);
              }
              HTTP_S.SetBody(manifest);
              conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
            }
            ready4data = true;
            HTTP_R.Clean(); //clean for any possible next requests
          }
        }else{
          Util::sleep(250);
        }
      }
      if (ready4data){
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Live").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              HTTP_S.Chunkify("", 0, conn);
              handlingRequest = false;
            }
            if ( !haveAvcc){
              avccbox.setPayload(Strm.metadata.tracks[trackID].init);
              haveAvcc = true;
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              Socket::Buffer ToPack;
              //write PAT and PMT TS packets
              if (PacketNumber % 42 == 0){
                PackData.DefaultPAT();
                HTTP_S.Chunkify(PackData.ToString(), 188, conn);
                PackData.DefaultPMT();
                HTTP_S.Chunkify(PackData.ToString(), 188, conn);
                PacketNumber += 2;
              }

              int PIDno = 0;
              char * ContCounter = 0;
              if (Strm.lastType() == DTSC::VIDEO){
                IsKeyFrame = Strm.getPacket().isMember("keyframe");
                if (IsKeyFrame){
                  TimeStamp = (Strm.getPacket()["time"].asInt() * 27000);
                }
                ToPack.append(avccbox.asAnnexB());
                while (Strm.lastData().size() > 4){
                  ThisNaluSize = (Strm.lastData()[0] << 24) + (Strm.lastData()[1] << 16) + (Strm.lastData()[2] << 8) + Strm.lastData()[3];
                  Strm.lastData().replace(0, 4, "\000\000\000\001", 4);
                  if (ThisNaluSize + 4 == Strm.lastData().size()){
                    ToPack.append(Strm.lastData());
                    break;
                  }else{
                    ToPack.append(Strm.lastData().c_str(), ThisNaluSize + 4);
                    Strm.lastData().erase(0, ThisNaluSize + 4);
                  }
                }
                ToPack.prepend(TS::Packet::getPESVideoLeadIn(0ul, Strm.getPacket()["time"].asInt() * 90));
                PIDno = 0x100 - 1 + Strm.getPacket()["trackid"].asInt();
                ContCounter = &VideoCounter;
              }else if (Strm.lastType() == DTSC::AUDIO){
                ToPack.append(TS::GetAudioHeader(Strm.lastData().size(), Strm.metadata.tracks[audioTrackID].init));
                ToPack.append(Strm.lastData());
                if (AppleCompat){
                  ToPack.prepend(TS::Packet::getPESAudioLeadIn(ToPack.bytes(1073741824ul), lastVid));
                }else{
                  ToPack.prepend(TS::Packet::getPESAudioLeadIn(ToPack.bytes(1073741824ul), Strm.getPacket()["time"].asInt() * 90));
                }
            	PIDno = 0x100 - 1 + Strm.getPacket()["trackid"].asInt();
                ContCounter = &AudioCounter;
                IsKeyFrame = false;
              }

              //initial packet
              PackData.Clear();
              PackData.PID(PIDno);
              PackData.ContinuityCounter(( *ContCounter)++);
              PackData.UnitStart(1);
              if (IsKeyFrame){
                PackData.RandomAccess(1);
                PackData.PCR(TimeStamp);
              }
              unsigned int toSend = PackData.AddStuffing(ToPack.bytes(184));
              std::string gonnaSend = ToPack.remove(toSend);
              PackData.FillFree(gonnaSend);
              HTTP_S.Chunkify(PackData.ToString(), 188, conn);
              PacketNumber++;

              //rest of packets
              while (ToPack.size()){
                PackData.Clear();
                PackData.PID(PIDno);
                PackData.ContinuityCounter(( *ContCounter)++);
                toSend = PackData.AddStuffing(ToPack.bytes(184));
                gonnaSend = ToPack.remove(toSend);
                PackData.FillFree(gonnaSend);
                HTTP_S.Chunkify(PackData.ToString(), 188, conn);
                PacketNumber++;
              }

            }
          }
        }
        if ( !ss.connected()){
          break;
        }
      }
    }
    conn.close();
    ss.SendNow(conn.getStats("HTTP_Live").c_str());
    ss.close();
#if DEBUG >= 5
    fprintf(stderr, "HLS: User %i disconnected.\n", conn.getSocket());
#endif
    return 0;
  } //HLS_Connector main function

} //Connector_HTTP namespace

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["desc"] = "Enables HTTP protocol Apple-specific streaming (also known as HLS).";
  capa["deps"] = "HTTP";
  capa["url_rel"] = "/hls/$/index.m3u8";
  capa["url_prefix"] = "/hls/$/";
  capa["socket"] = "http_live";
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][1u].append("AAC");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "html5/application/vnd.apple.mpegurl";
  capa["methods"][0u]["priority"] = 9ll;
  conf.addBasicConnectorOptions(capa);
  conf.parseArgs(argc, argv);
  
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }

  return conf.serveForkedSocket(Connector_HTTP::liveConnector);
} //main
