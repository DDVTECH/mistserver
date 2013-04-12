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

/// Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  ///\brief Builds a bootstrap for use in HTTP Dynamic streaming.
  ///\param streamName The name of the stream.
  ///\param metadata The current metadata, used to generate the index.
  ///\param fragnum The index of the current fragment
  ///\return The generated bootstrap.
  std::string dynamicBootstrap(std::string & streamName, JSON::Value & metadata, int fragnum = 0){
    std::string empty;

    MP4::ASRT asrt;
    asrt.setUpdate(false);
    asrt.setVersion(1);
    //asrt.setQualityEntry(empty, 0);
    if (metadata.isMember("live")){
      asrt.setSegmentRun(1, 4294967295ul, 0);
    }else{
      asrt.setSegmentRun(1, metadata["keytime"].size(), 0);
    }

    MP4::AFRT afrt;
    afrt.setUpdate(false);
    afrt.setVersion(1);
    afrt.setTimeScale(1000);
    //afrt.setQualityEntry(empty, 0);
    MP4::afrt_runtable afrtrun;
    if (metadata.isMember("live")){
      // restrict data to last 2 fragments, unless an earlier fragment was expressly requested.
      int count = 0;
      unsigned int begin = std::max(0u, metadata["keynum"].size() - 3);
      while (begin > 0 && fragnum && metadata["keynum"][begin].asInt() > fragnum){
        begin--;
      }
      for (int i = begin; i < metadata["keynum"].size(); i++){
        afrtrun.firstFragment = metadata["keynum"][i].asInt();
        afrtrun.firstTimestamp = metadata["keytime"][i].asInt();
        afrtrun.duration = metadata["keylen"][i].asInt();
        afrt.setFragmentRun(afrtrun, count++);
      }
    }else{
      for (int i = 0; i < metadata["keynum"].size(); i++){
        afrtrun.firstFragment = metadata["keynum"][i].asInt();
        afrtrun.firstTimestamp = metadata["keytime"][i].asInt();
        afrtrun.duration = metadata["keylen"][i].asInt();
        afrt.setFragmentRun(afrtrun, i);
      }
    }

    MP4::ABST abst;
    abst.setVersion(1);
    abst.setBootstrapinfoVersion(1);
    abst.setProfile(0);
    abst.setUpdate(false);
    abst.setTimeScale(1000);
    abst.setLive(false);
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
    std::string Result;
    if (metadata.isMember("vod")){
      Result =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
              "<id>" + streamName + "</id>\n"
              "<width>" + metadata["video"]["width"].asString() + "</width>\n"
              "<height>" + metadata["video"]["height"].asString() + "</height>\n"
              "<duration>" + metadata["length"].asString() + ".000</duration>\n"
              "<mimeType>video/mp4</mimeType>\n"
              "<streamType>recorded</streamType>\n"
              "<deliveryType>streaming</deliveryType>\n"
              "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">" + Base64::encode(dynamicBootstrap(streamName, metadata)) + "</bootstrapInfo>\n"
              "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"" + streamName + "/\">\n"
              "<metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>\n"
              "</media>\n"
              "</manifest>\n";
    }else{
      Result =
          "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
              "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
              "<id>" + streamName + "</id>\n"
              "<dvrInfo windowDuration=\"" + metadata["buffer_window"].asString().substr(0, metadata["buffer_window"].asString().size() - 3) + "\"></dvrInfo>"
              "<mimeType>video/mp4</mimeType>\n"
              "<streamType>live</streamType>\n"
              "<deliveryType>streaming</deliveryType>\n"
              "<media url=\"" + streamName + "/\">\n"
              "<metadata>AgAKb25NZXRhRGF0YQMAAAk=</metadata>\n"
              "</media>\n"
              "<bootstrapInfo profile=\"named\" url=\"" + streamName + ".abst\" />\n"
              "</manifest>\n";
    }
#if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result << std::endl;
#endif
    return Result;
  } //BuildManifest

  ///\brief Main function for the HTTP Dynamic Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int dynamicConnector(Socket::Connection conn){
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
              conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
              continue;
            }
            ss.setBlocking(false);
            //make sure metadata is received
            while ( !Strm.metadata && ss.connected()){
              if (ss.spool()){
                while (Strm.parsePacket(ss.Received())){
                  //do nothing
                }
              }
            }
          }
          if (HTTP_R.url.find(".abst") != std::string::npos){
            HTTP_S.Clean();
            HTTP_S.SetBody(dynamicBootstrap(streamname, Strm.metadata));
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
            if (Strm.metadata.isMember("live")){
              int seekable = Strm.canSeekFrame(ReqFragment);
              if (seekable == 0){
                // iff the fragment in question is available, check if the next is available too
                seekable = Strm.canSeekFrame(ReqFragment + 1);
              }
              if (seekable < 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
                conn.SendNow(HTTP_S.BuildResponse("412", "Fragment out of range"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ F" << ReqFragment << " too old (F" << Strm.metadata["keynum"][0u].asInt() << " - " << Strm.metadata["keynum"][Strm.metadata["keynum"].size() - 1].asInt() << ")" << std::endl;
                continue;
              }
              if (seekable > 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
                conn.SendNow(HTTP_S.BuildResponse("208", "Ask again later"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ F" << ReqFragment << " not available yet (F" << Strm.metadata["keynum"][0u].asInt() << " - " << Strm.metadata["keynum"][Strm.metadata["keynum"].size() - 1].asInt() << ")" << std::endl;
                continue;
              }
            }
            std::stringstream sstream;
            sstream << "f " << ReqFragment << "\no \n";
            ss.SendNow(sstream.str().c_str());
          }else{
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "text/xml");
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            std::string manifest = dynamicIndex(streamname, Strm.metadata);
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
                std::string new_strap = dynamicBootstrap(streamname, Strm.metadata, ReqFragment);
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

///\brief The standard process-spawning main function.
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
