/// \file conn_http_dynamic.cpp
/// Contains the main code for the HTTP Dynamic Connector

#include <iostream>
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

/// Holds everything unique to HTTP Dynamic Connector.
namespace Connector_HTTP {

  std::string GenerateBootstrap(std::string & MovieId, JSON::Value & metadata, int fragnum, int starttime, int endtime){
    std::string empty;

    MP4::ASRT asrt;
    if (starttime == 0 && metadata.isMember("vod")){
      asrt.setUpdate(false);
    }else{
      asrt.setUpdate(true);
    }
    asrt.setVersion(1);
    //asrt.setQualityEntry(empty, 0);
    if (metadata.isMember("live")){
      asrt.setSegmentRun(1, 4294967295, 0);
    }else{
      asrt.setSegmentRun(1, metadata["keytime"].size(), 0);
    }

    MP4::AFRT afrt;
    if (starttime == 0 && metadata.isMember("vod")){
      afrt.setUpdate(false);
    }else{
      afrt.setUpdate(true);
    }
    afrt.setVersion(1);
    afrt.setTimeScale(1000);
    //afrt.setQualityEntry(empty, 0);
    MP4::afrt_runtable afrtrun;
    if (metadata.isMember("live")){
      for (int i = 0; i < metadata["keynum"].size(); i++){
        afrtrun.firstFragment = metadata["keynum"][i].asInt();
        afrtrun.firstTimestamp = metadata["keytime"][i].asInt();
        afrtrun.duration = metadata["keylen"][i].asInt();
        afrt.setFragmentRun(afrtrun, i);
      }
    }else{
      for (int i = 0; i < metadata["keytime"].size(); i++){
        afrtrun.firstFragment = i + 1;
        afrtrun.firstTimestamp = metadata["keytime"][i].asInt();
        if (i + 1 < metadata["keytime"].size()){
          afrtrun.duration = metadata["keytime"][i + 1].asInt() - metadata["keytime"][i].asInt();
        }else{
          if (metadata["lastms"].asInt()){
            afrtrun.duration = metadata["lastms"].asInt() - metadata["keytime"][i].asInt();
          }else{
            afrtrun.duration = 3000; //guess 3 seconds if unknown
          }
        }
        afrt.setFragmentRun(afrtrun, i);
      }
    }

    MP4::ABST abst;
    abst.setVersion(1);
    if (metadata.isMember("live")){
      abst.setBootstrapinfoVersion(metadata["keynum"][metadata["keynum"].size() - 2].asInt());
    }else{
      abst.setBootstrapinfoVersion(1);
    }
    abst.setProfile(0);
    if (starttime == 0){
      abst.setUpdate(false);
    }else{
      abst.setUpdate(true);
    }
    abst.setTimeScale(1000);
    if (metadata.isMember("vod")){
      abst.setLive(false);
      if (metadata["lastms"].asInt()){
        abst.setCurrentMediaTime(metadata["lastms"].asInt());
      }else{
        abst.setCurrentMediaTime(1000 * metadata["length"].asInt());
      }
    }else{
      abst.setLive(false);
      abst.setCurrentMediaTime(metadata["lastms"].asInt());
    }
    abst.setSmpteTimeCodeOffset(0);
    abst.setMovieIdentifier(MovieId);
    //abst.setServerEntry(empty, 0);
    //abst.setQualityEntry(empty, 0);
    //abst.setDrmData(empty);
    //abst.setMetaData(empty);
    abst.setSegmentRunTable(asrt, 0);
    abst.setFragmentRunTable(afrt, 0);

#if DEBUG >= 8
    std::cout << "Sending bootstrap:" << std::endl << abst.toPrettyString(0) << std::endl;
#endif
    return std::string((char*)abst.asBox(), (int)abst.boxedSize());
  }

  /// Returns a F4M-format manifest file
  std::string BuildManifest(std::string & MovieId, JSON::Value & metadata){
    std::string Result;
    if (metadata.isMember("vod")){
      Result =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
              "<id>" + MovieId + "</id>\n"
              "<width>" + metadata["video"]["width"].asString() + "</width>\n"
              "<height>" + metadata["video"]["height"].asString() + "</height>\n"
              "<duration>" + metadata["length"].asString() + ".000</duration>\n"
              "<mimeType>video/mp4</mimeType>\n"
              "<streamType>recorded</streamType>\n"
              "<deliveryType>streaming</deliveryType>\n"
              "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">" + Base64::encode(GenerateBootstrap(MovieId, metadata, 1, 0, 0)) + "</bootstrapInfo>\n"
              "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"" + MovieId + "/\">\n"
              "<metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>\n"
              "</media>\n"
              "</manifest>\n";
    }else{
      Result =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
              "<id>" + MovieId + "</id>\n"
              "<dvrInfo windowDuration=\"" + metadata["buffer_window"].asString().substr(0, metadata["buffer_window"].asString().size() - 3) + "\"></dvrInfo>"
              "<mimeType>video/mp4</mimeType>\n"
              "<streamType>live</streamType>\n"
              "<deliveryType>streaming</deliveryType>\n"
              "<media url=\"" + MovieId + "/\">\n"
              "<metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>\n"
              "</media>\n"
              "<bootstrapInfo profile=\"named\" url=\"" + MovieId + ".abst\" />\n"
              "</manifest>\n";
    }
#if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result << std::endl;
#endif
    return Result;
  } //BuildManifest

  /// Main function for Connector_HTTP_Dynamic
  int Connector_HTTP_Dynamic(Socket::Connection conn){
    std::deque<std::string> FlashBuf;
    int FlashBufSize = 0;
    long long int FlashBufTime = 0;
    FLV::Tag tmp; //temporary tag

    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; //HTTP Receiver en HTTP Sender.

    Socket::Connection ss( -1);
    std::string streamname;
    std::string recBuffer = "";

    std::string Quality;
    int Segment = -1;
    int ReqFragment = -1;
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
          streamname = HTTP_R.GetHeader("X-Stream");
          if ( !ss){
            ss = Util::Stream::getStream(streamname);
            if ( !ss.connected()){
              HTTP_S.Clean();
              HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
              conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
              continue;
            }
            ss.setBlocking(false);
            //make sure metadata is received
            while ( !Strm.metadata){
              if (ss.spool()){
                while (Strm.parsePacket(ss.Received())){
                  //do nothing
                }
              }
            }
          }
          if (HTTP_R.url.find(".abst") != std::string::npos){
            HTTP_S.Clean();
            HTTP_S.SetBody(GenerateBootstrap(streamname, Strm.metadata, 1, 0, 0));
            HTTP_S.SetHeader("Content-Type", "binary/octet");
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
            HTTP_R.Clean(); //clean for any possible next requests
            continue;
          }
          if (HTTP_R.url.find("f4m") == std::string::npos){
            Quality = HTTP_R.url.substr(HTTP_R.url.find("/", 10) + 1);
            Quality = Quality.substr(0, Quality.find("Seg"));
            int temp;
            temp = HTTP_R.url.find("Seg") + 3;
            Segment = atoi(HTTP_R.url.substr(temp, HTTP_R.url.find("-", temp) - temp).c_str());
            temp = HTTP_R.url.find("Frag") + 4;
            ReqFragment = atoi(HTTP_R.url.substr(temp).c_str());
#if DEBUG >= 5
            printf("Quality: %s, Seg %d Frag %d\n", Quality.c_str(), Segment, ReqFragment);
#endif
            std::stringstream sstream;
            sstream << "f " << ReqFragment << "\no \n";
            ss.SendNow(sstream.str().c_str());
          }else{
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "text/xml");
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            std::string manifest = BuildManifest(streamname, Strm.metadata);
            HTTP_S.SetBody(manifest);
            conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
          }
          HTTP_R.Clean(); //clean for any possible next requests
        }
      }else{
        Util::sleep(1);
      }
      if (ss.connected()){
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Dynamic").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              if (FlashBufSize){
                HTTP_S.Clean();
                HTTP_S.SetHeader("Content-Type", "video/mp4");
                HTTP_S.SetBody("");
                std::string new_strap = GenerateBootstrap(streamname, Strm.metadata, 1, 0, 0);
                HTTP_S.SetHeader("Content-Length", FlashBufSize + 8 + new_strap.size()); //32+33+btstrp.size());
                conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
                conn.SendNow(new_strap);
                unsigned long size = htonl(FlashBufSize+8);
                conn.SendNow((char*) &size, 4);
                conn.SendNow("mdat", 4);
                while (FlashBuf.size() > 0){
                  conn.SendNow(FlashBuf.front());
                  FlashBuf.pop_front();
                }
              }
              FlashBuf.clear();
              FlashBufSize = 0;
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              if (FlashBufSize == 0){
                //fill buffer with init data, if needed.
                if (Strm.metadata.isMember("audio") && Strm.metadata["audio"].isMember("init")){
                  tmp.DTSCAudioInit(Strm);
                  tmp.tagTime(Strm.getPacket(0)["time"].asInt());
                  FlashBuf.push_back(std::string(tmp.data, tmp.len));
                  FlashBufSize += tmp.len;
                }
                if (Strm.metadata.isMember("video") && Strm.metadata["video"].isMember("init")){
                  tmp.DTSCVideoInit(Strm);
                  tmp.tagTime(Strm.getPacket(0)["time"].asInt());
                  FlashBuf.push_back(std::string(tmp.data, tmp.len));
                  FlashBufSize += tmp.len;
                }
                FlashBufTime = Strm.getPacket(0)["time"].asInt();
              }
              tmp.DTSCLoader(Strm);
              FlashBuf.push_back(std::string(tmp.data, tmp.len));
              FlashBufSize += tmp.len;
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

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server("/tmp/mist/http_dynamic");
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::Connector_HTTP_Dynamic(S);
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
