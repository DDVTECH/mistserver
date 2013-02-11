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
#include <mist/config.h>
#include <sstream>
#include <mist/stream.h>
#include <mist/timing.h>
#include <mist/ts_packet.h>

/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  /// Parses the list of keyframes into 10 second fragments
  std::vector<int> keyframesToFragments(JSON::Value & metadata){
    std::vector<int> result;
    if (metadata.isNull()){
      return result;
    }
    result.push_back(0);
    int currentBase = metadata["keytime"][0u].asInt();
    for (int i = 0; i < metadata["keytime"].size(); i++){
      if ((metadata["keytime"][i].asInt() - currentBase) > 10000){
        currentBase = metadata["keytime"][i].asInt();
        result.push_back(i);
      }
    }
    return result;
  }

  /// Returns a m3u or m3u8 index file
  std::string BuildIndex(std::string & MovieId, JSON::Value & metadata){
    std::stringstream Result;
    std::vector<int> fragIndices = keyframesToFragments(metadata);
    int longestFragment = 0;
    for (int i = 1; i < fragIndices.size(); i++){
      int fragDuration = metadata["keytime"][fragIndices[i]].asInt() - metadata["keytime"][fragIndices[i - 1]].asInt();
      if (fragDuration > longestFragment){
        longestFragment = fragDuration;
      }
    }
    if (metadata.isMember("length") && metadata["length"].asInt() > 0){
      Result << "#EXTM3U\r\n"
      //"#EXT-X-VERSION:1\r\n"
      //"#EXT-X-ALLOW-CACHE:YES\r\n"
              "#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n"
          "#EXT-X-MEDIA-SEQUENCE:0\r\n";
      //"#EXT-X-PLAYLIST-TYPE:VOD\r\n";
      int lastDuration = 0;
      bool writeOffset = true;
      for (int i = 0; i < fragIndices.size() - 1; i++){
        Result << "#EXTINF:" << (metadata["keytime"][fragIndices[i + 1]].asInt() - lastDuration) / 1000 << ", no desc\r\n" << fragIndices[i] + 1
            << "_" << fragIndices[i + 1] - fragIndices[i] << ".ts\r\n";
        lastDuration = metadata["keytime"][fragIndices[i + 1]].asInt();
      }
      Result << "#EXT-X-ENDLIST";
    }else{
      Result << "#EXTM3U\r\n"
          "#EXT-X-MEDIA-SEQUENCE:0\r\n"
          "#EXT-X-TARGETDURATION:" << (longestFragment / 1000) + 1 << "\r\n";
    }
#if DEBUG >= 8
    std::cerr << "Sending this index:" << std::endl << Result.str() << std::endl;
#endif
    return Result.str();
  } //BuildIndex

  /// Main function for Connector_HTTP_Live
  int Connector_HTTP_Live(Socket::Connection conn){
    std::stringstream TSBuf;
    long long int TSBufTime = 0;

    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; //HTTP Receiver en HTTP Sender.

    bool ready4data = false; //Set to true when streaming is to begin.
    bool pending_manifest = false;
    bool receive_marks = false; //when set to true, this stream will ignore keyframes and instead use pause marks
    bool inited = false;
    Socket::Connection ss( -1);
    std::string streamname;
    std::string recBuffer = "";

    std::string ToPack;
    TS::Packet PackData;
    std::string DTMIData;
    int PacketNumber = 0;
    long long unsigned int TimeStamp = 0;
    int ThisNaluSize;
    char VideoCounter = 0;
    char AudioCounter = 0;
    bool WritePesHeader;
    bool IsKeyFrame;
    MP4::AVCC avccbox;
    bool haveAvcc = false;

    std::vector<int> fragIndices;

    std::string manifestType;

    int Segment = -1;
    int temp;
    int Flash_RequestPending = 0;
    unsigned int lastStats = 0;
    conn.setBlocking(false); //do not block on conn.spool() when no data is available

    while (conn.connected()){
      if (conn.spool() || conn.Received().size()){
        //make sure it ends in a \n
        if ( *(conn.Received().get().rbegin()) != '\n'){
          std::string tmp = conn.Received().get();
          conn.Received().get().clear();
          if (conn.Received().size()){
            conn.Received().get().insert(0, tmp);
          }else{
            conn.Received().append(tmp);
          }
        }
        if (HTTP_R.Read(conn.Received().get())){
#if DEBUG >= 4
          std::cout << "Received request: " << HTTP_R.getUrl() << std::endl;
#endif
          conn.setHost(HTTP_R.GetHeader("X-Origin"));
          if (HTTP_R.url.find(".m3u") == std::string::npos){
            streamname = HTTP_R.url.substr(5, HTTP_R.url.find("/", 5) - 5);
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
              inited = true;
            }
            temp = HTTP_R.url.find("/", 5) + 1;
            Segment = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find("_", temp) - temp).c_str());
            temp = HTTP_R.url.find("_", temp) + 1;
            int frameCount = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find(".ts", temp) - temp).c_str());

            std::stringstream sstream;
            sstream << "f " << Segment + 1 << "\n";
            for (int i = 0; i < frameCount; i++){
              sstream << "o \n";
            }
            ss.SendNow(sstream.str().c_str());
            Flash_RequestPending++;
          }else{
            streamname = HTTP_R.url.substr(5, HTTP_R.url.find("/", 5) - 5);
            if (HTTP_R.url.find(".m3u8") != std::string::npos){
              manifestType = "audio/x-mpegurl";
            }else{
              manifestType = "audio/mpegurl";
            }
            if ( !Strm.metadata.isNull()){
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type", manifestType);
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              if (Strm.metadata.isMember("length")){
                receive_marks = true;
              }
              std::string manifest = BuildIndex(streamname, Strm.metadata);
              HTTP_S.SetBody(manifest);
              conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
#if DEBUG >= 3
              printf("Sent index\n");
#endif
              pending_manifest = false;
            }else{
              pending_manifest = true;
            }
          }
          ready4data = true;
          HTTP_R.Clean(); //clean for any possible next requests
        }
      }else{
        if (Flash_RequestPending){
          usleep(1000); //sleep 1ms
        }else{
          usleep(10000); //sleep 10ms
        }
      }
      if (ready4data){
        if ( !inited){
          //we are ready, connect the socket!
          ss = Util::Stream::getStream(streamname);
          if ( !ss.connected()){
#if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
#endif
            ss.close();
            HTTP_S.Clean();
            HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
            conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
            ready4data = false;
            continue;
          }
          ss.setBlocking(false);
#if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
#endif
          inited = true;
        }
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Live").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.getPacket(0).isMember("time")){
              if ( !Strm.metadata.isMember("firsttime")){
                Strm.metadata["firsttime"] = Strm.getPacket(0)["time"];
              }else{
                if ( !Strm.metadata.isMember("length") || Strm.metadata["length"].asInt() == 0){
                  Strm.getPacket(0)["time"] = Strm.getPacket(0)["time"].asInt() - Strm.metadata["firsttime"].asInt();
                }
              }
              Strm.metadata["lasttime"] = Strm.getPacket(0)["time"];
            }
            if (pending_manifest){
              HTTP_S.Clean();
              HTTP_S.protocol = "HTTP/1.1";
              HTTP_S.SetHeader("Cache-Control", "no-cache");
              if (Strm.metadata.isMember("length")){
                receive_marks = true;
              }
              std::string manifest = BuildIndex(streamname, Strm.metadata);
              HTTP_S.SetHeader("Content-Type", manifestType);
              HTTP_S.SetHeader("Connection", "keep-alive");
              HTTP_S.SetBody(manifest);
              conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
#if DEBUG >= 3
              printf("Sent manifest\n");
#endif
              pending_manifest = false;
            }
            if ( !receive_marks && Strm.metadata.isMember("length")){
              receive_marks = true;
            }
            if ((Strm.getPacket(0).isMember("keyframe") && !receive_marks) || Strm.lastType() == DTSC::PAUSEMARK){
              TSBuf.flush();
#if DEBUG >= 4
              fprintf(stderr, "Received a %s fragment of %i bytes.\n", Strm.getPacket(0)["datatype"].asString().c_str(), TSBuf.str().size());
#endif
              if (Flash_RequestPending > 0 && TSBuf.str().size()){
#if DEBUG >= 3
                fprintf(stderr, "Sending a fragment...");
#endif
                HTTP_S.Clean();
                HTTP_S.protocol = "HTTP/1.1";
                HTTP_S.SetHeader("Content-Type", "video/mp2t");
                HTTP_S.SetHeader("Connection", "keep-alive");
                HTTP_S.SetBody("");
                HTTP_S.SetHeader("Content-Length", TSBuf.str().size());
                conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
                conn.SendNow(TSBuf.str().c_str(), TSBuf.str().size());
                TSBuf.str("");
                Flash_RequestPending--;
                PacketNumber = 0;
#if DEBUG >= 3
                fprintf(stderr, "Done\n");
#endif
              }
              TSBuf.str("");
            }
            if ( !haveAvcc){
              avccbox.setPayload(Strm.metadata["video"]["init"].asString());
              haveAvcc = true;
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              if (PacketNumber == 0){
                PackData.DefaultPAT();
                TSBuf.write(PackData.ToString(), 188);
                PackData.DefaultPMT();
                TSBuf.write(PackData.ToString(), 188);
                PacketNumber += 2;
              }
              if (Strm.lastType() == DTSC::VIDEO){
                DTMIData = Strm.lastData();
                IsKeyFrame = Strm.getPacket(0).isMember("keyframe");
                std::cout << Strm.getPacket(0)["time"].asInt() << std::endl;
                if (IsKeyFrame){
                  TimeStamp = (Strm.getPacket(0)["time"].asInt() * 27000);
                }
                ToPack += avccbox.asAnnexB();
                while (DTMIData.size()){
                  ThisNaluSize = (DTMIData[0] << 24) + (DTMIData[1] << 16) + (DTMIData[2] << 8) + DTMIData[3];
                  DTMIData.replace(0, 4, TS::NalHeader, 4);
                  if (ThisNaluSize + 4 == DTMIData.size()){
                    ToPack.append(DTMIData);
                    break;
                  }else{
                    ToPack.append(DTMIData, 0, ThisNaluSize + 4);
                    DTMIData.erase(0, ThisNaluSize + 4);
                  }
                }
                WritePesHeader = true;
                while (ToPack.size()){
                  PackData.Clear();
                  PackData.PID(0x100);
                  PackData.ContinuityCounter(VideoCounter);
                  VideoCounter++;
                  if (WritePesHeader){
                    PackData.UnitStart(1);
                    if (IsKeyFrame){
                      PackData.RandomAccess(1);
                      PackData.PCR(TimeStamp);
                    }else{
                      PackData.AdaptationField(1);
                    }
                    PackData.AddStuffing(PackData.BytesFree() - (25 + ToPack.size()));
                    PackData.PESVideoLeadIn(ToPack.size(), Strm.getPacket(0)["time"].asInt() * 90);
                    WritePesHeader = false;
                  }else{
                    PackData.AdaptationField(1);
                    PackData.AddStuffing(PackData.BytesFree() - (ToPack.size()));
                  }
                  PackData.FillFree(ToPack);
                  TSBuf.write(PackData.ToString(), 188);
                  PacketNumber++;
                }
              }else if (Strm.lastType() == DTSC::AUDIO){
                WritePesHeader = true;
                DTMIData = Strm.lastData();
                ToPack = TS::GetAudioHeader(DTMIData.size(), Strm.metadata["audio"]["init"].asString());
                ToPack += DTMIData;
                while (ToPack.size()){
                  PackData.Clear();
                  PackData.PID(0x101);
                  PackData.ContinuityCounter(AudioCounter);
                  AudioCounter++;
                  if (WritePesHeader){
                    PackData.UnitStart(1);
                    PackData.AddStuffing(PackData.BytesFree() - (14 + ToPack.size()));
                    PackData.PESAudioLeadIn(ToPack.size(), Strm.getPacket(0)["time"].asInt() * 90);
                    WritePesHeader = false;
                  }else{
                    PackData.AdaptationField(1);
                    PackData.AddStuffing(PackData.BytesFree() - ToPack.size());
                  }
                  int before = ToPack.size();
                  PackData.FillFree(ToPack);
                  TSBuf.write(PackData.ToString(), 188);
                  PacketNumber++;
                }
              }
            }
          }
          if (pending_manifest && !Strm.metadata.isNull()){
            HTTP_S.Clean();
            HTTP_S.protocol = "HTTP/1.1";
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            if (Strm.metadata.isMember("length")){
              receive_marks = true;
            }
            HTTP_S.SetHeader("Content-Type", manifestType);
            HTTP_S.SetHeader("Connection", "keep-alive");
            std::string manifest = BuildIndex(streamname, Strm.metadata);
            HTTP_S.SetBody(manifest);
            conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
#if DEBUG >= 3
            printf("Sent index\n");
#endif
            pending_manifest = false;
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
#if DEBUG >= 1
    fprintf(stderr, "User %i disconnected.\n", conn.getSocket());
    if (inited){
      fprintf(stderr, "Status was: inited\n");
    }else{
      if (ready4data){
        fprintf(stderr, "Status was: ready4data\n");
      }else{
        fprintf(stderr, "Status was: connected\n");
      }
    }
#endif
    return 0;
  } //Connector_HTTP_Dynamic main function

} //Connector_HTTP_Dynamic namespace

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server("/tmp/mist/http_live");
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::Connector_HTTP_Live(S);
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
