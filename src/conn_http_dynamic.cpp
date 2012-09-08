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
#include <ctime>
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

/// Holds everything unique to HTTP Dynamic Connector.
namespace Connector_HTTP{

  std::string GenerateBootstrap(std::string & MovieId, JSON::Value & metadata){
    MP4::AFRT afrt;
    afrt.SetUpdate(false);
    afrt.SetTimeScale(1000);
    afrt.AddQualityEntry("");
    if (!metadata.isMember("video") || !metadata["video"].isMember("keyms") || metadata["video"]["keyms"].asInt() == 0){
      //metadata["lasttime"].asInt()?
      afrt.AddFragmentRunEntry(1, 0, 2000); //FirstFragment, FirstFragmentTimestamp,Fragment Duration in milliseconds
    }else{
      afrt.AddFragmentRunEntry(1, 0, metadata["video"]["keyms"].asInt()); //FirstFragment, FirstFragmentTimestamp,Fragment Duration in milliseconds
    }
    afrt.WriteContent();
    
    MP4::ASRT asrt;
    asrt.SetUpdate(false);
    asrt.AddQualityEntry("");
    /// \todo Actually use correct number of fragments.
    asrt.AddSegmentRunEntry(1, 20000);//1 Segment, 20000 Fragments
    asrt.WriteContent();
    
    MP4::ABST abst;
    abst.AddFragmentRunTable(&afrt);
    abst.AddSegmentRunTable(&asrt);
    abst.SetBootstrapVersion(1);
    abst.SetProfile(0);
    if (metadata.isMember("length") && metadata["length"].asInt() > 0){
      abst.SetLive(false);
      abst.SetMediaTime(1000*metadata["length"].asInt());
    }else{
      abst.SetLive(true);
      abst.SetMediaTime(0xFFFFFFFF);//metadata["lasttime"].asInt()?
    }
    abst.SetUpdate(false);
    abst.SetTimeScale(1000);
    abst.SetSMPTE(0);
    abst.SetMovieIdentifier(MovieId);
    abst.SetDRM("");
    abst.SetMetaData("");
    abst.AddServerEntry("");
    abst.AddQualityEntry("");
    abst.WriteContent();
    
    std::string Result;
    Result.append((char*)abst.GetBoxedData(), (int)abst.GetBoxedDataSize());
    #if DEBUG >= 8
    std::cout << "Sending bootstrap:" << std::endl << abst.toPrettyString(0) << std::endl;
    #endif
    return Base64::encode(Result);
  }
  

  /// Returns a F4M-format manifest file
  std::string BuildManifest(std::string & MovieId, JSON::Value & metadata){
    std::string Result;
    if (metadata.isMember("length") && metadata["length"].asInt() > 0){
      std::stringstream st;
      st << ((double)metadata["video"]["keyms"].asInt() / 1000);
      Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
      "<id>" + MovieId + "</id>\n"
      "<duration>" + metadata["length"].asString() + "</duration>\n"
      "<mimeType>video/mp4</mimeType>\n"
      "<streamType>recorded</streamType>\n"
      "<deliveryType>streaming</deliveryType>\n"
      "<bestEffortFetchInfo segmentDuration=\""+metadata["length"].asString()+".000\" fragmentDuration=\""+st.str()+"\" />\n"
      "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">" + GenerateBootstrap(MovieId, metadata) + "</bootstrapInfo>\n"
      "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"" + MovieId + "/\"></media>\n"
      "</manifest>\n";
    }else{
      Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
      "<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n"
      "<id>" + MovieId + "</id>\n"
      "<mimeType>video/mp4</mimeType>\n"
      "<streamType>live</streamType>\n"
      "<deliveryType>streaming</deliveryType>\n"
      "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">" + GenerateBootstrap(MovieId, metadata) + "</bootstrapInfo>\n"
      "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"" + MovieId + "/\"></media>\n"
      "</manifest>\n";
    }
    #if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result << std::endl;
    #endif
    return Result;
  }//BuildManifest

  /// Main function for Connector_HTTP_Dynamic
  int Connector_HTTP_Dynamic(Socket::Connection conn){
    std::stringstream FlashBuf;
    int flashbuf_nonempty = 0;
    FLV::Tag tmp;//temporary tag, for init data

    DTSC::Stream Strm;//Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.

    bool ready4data = false;//Set to true when streaming is to begin.
    bool pending_manifest = false;
    bool receive_marks = false;//when set to true, this stream will ignore keyframes and instead use pause marks
    bool inited = false;
    Socket::Connection ss(-1);
    std::string streamname;
    FLV::Tag tag;///< Temporary tag buffer.
    std::string recBuffer = "";

    std::string Quality;
    int Segment = -1;
    int ReqFragment = -1;
    int temp;
    int Flash_RequestPending = 0;
    unsigned int lastStats = 0;
    conn.setBlocking(false);//do not block on conn.spool() when no data is available

    while (conn.connected()){
      if (conn.spool()){
        if (HTTP_R.Read(conn.Received())){
          #if DEBUG >= 4
          std::cout << "Received request: " << HTTP_R.getUrl() << std::endl;
          #endif
          conn.setHost(HTTP_R.GetHeader("X-Origin"));
          if (HTTP_R.url.find("f4m") == std::string::npos){
            streamname = HTTP_R.url.substr(1,HTTP_R.url.find("/",1)-1);
            if (!ss){
              ss = Util::Stream::getStream(streamname);
              if (!ss.connected()){
                #if DEBUG >= 1
                fprintf(stderr, "Could not connect to server!\n");
                #endif
                ss.close();
                HTTP_S.Clean();
                HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
                conn.Send(HTTP_S.BuildResponse("404", "Not found"));
                ready4data = false;
                continue;
              }
              ss.setBlocking(false);
              inited = true;
            }
            Quality = HTTP_R.url.substr( HTTP_R.url.find("/",1)+1 );
            Quality = Quality.substr(0, Quality.find("Seg"));
            temp = HTTP_R.url.find("Seg") + 3;
            Segment = atoi( HTTP_R.url.substr(temp,HTTP_R.url.find("-",temp)-temp).c_str());
            temp = HTTP_R.url.find("Frag") + 4;
            ReqFragment = atoi( HTTP_R.url.substr(temp).c_str() );
            #if DEBUG >= 4
            printf( "Quality: %s, Seg %d Frag %d\n", Quality.c_str(), Segment, ReqFragment);
            #endif
            std::stringstream sstream;
            sstream << "f " << ReqFragment << "\no \n";
            ss.Send(sstream.str().c_str());
            ss.flush();
            Flash_RequestPending++;
          }else{
            streamname = HTTP_R.url.substr(1,HTTP_R.url.find("/",1)-1);
            if (!Strm.metadata.isNull()){
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type","text/xml");
              HTTP_S.SetHeader("Cache-Control","no-cache");
              if (Strm.metadata.isMember("length")){receive_marks = true;}
              std::string manifest = BuildManifest(streamname, Strm.metadata);
              HTTP_S.SetBody(manifest);
              conn.Send(HTTP_S.BuildResponse("200", "OK"));
              #if DEBUG >= 3
              printf("Sent manifest\n");
              #endif
              pending_manifest = false;
            }else{
              pending_manifest = true;
            }
          }
          ready4data = true;
          HTTP_R.Clean(); //clean for any possible next requests
        }else{
          #if DEBUG >= 3
          fprintf(stderr, "Could not parse the following:\n%s\n", conn.Received().c_str());
          #endif
        }
      }else{
        usleep(10000);//sleep 10ms
      }
      if (ready4data){
        if (!inited){
          //we are ready, connect the socket!
          ss = Util::Stream::getStream(streamname);
          if (!ss.connected()){
            #if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
            #endif
            ss.close();
            HTTP_S.Clean();
            HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
            conn.Send(HTTP_S.BuildResponse("404", "Not found"));
            ready4data = false;
            continue;
          }
          ss.setBlocking(false);
          #if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
          #endif
          inited = true;
        }
        unsigned int now = time(0);
        if (now != lastStats){
          lastStats = now;
          ss.Send("S ");
          ss.Send(conn.getStats("HTTP_Dynamic").c_str());
        }
        if (ss.spool() || ss.Received() != ""){
          if (Strm.parsePacket(ss.Received())){
            if (Strm.getPacket(0).isMember("time")){
              if (!Strm.metadata.isMember("firsttime")){
                Strm.metadata["firsttime"] = Strm.getPacket(0)["time"];
              }else{
                if (!Strm.metadata.isMember("length") || Strm.metadata["length"].asInt() == 0){
                  Strm.getPacket(0)["time"] = Strm.getPacket(0)["time"].asInt() - Strm.metadata["firsttime"].asInt();
                }
              }
              Strm.metadata["lasttime"] = Strm.getPacket(0)["time"];
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              tag.DTSCLoader(Strm);
            }
            if (pending_manifest){
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type","text/xml");
              HTTP_S.SetHeader("Cache-Control","no-cache");
              if (Strm.metadata.isMember("length")){receive_marks = true;}
              std::string manifest = BuildManifest(streamname, Strm.metadata);
              HTTP_S.SetBody(manifest);
              conn.Send(HTTP_S.BuildResponse("200", "OK"));
              #if DEBUG >= 3
              printf("Sent manifest\n");
              #endif
              pending_manifest = false;
            }
            if (!receive_marks && Strm.metadata.isMember("length")){receive_marks = true;}
            if ((Strm.getPacket(0).isMember("keyframe") && !receive_marks) || Strm.lastType() == DTSC::PAUSEMARK){
              if (flashbuf_nonempty || Strm.lastType() == DTSC::PAUSEMARK){
                #if DEBUG >= 4
                fprintf(stderr, "Received a %s fragment of %i packets.\n", Strm.getPacket(0)["datatype"].asString().c_str(), flashbuf_nonempty);
                #endif
                if (Flash_RequestPending > 0){
                  HTTP_S.Clean();
                  HTTP_S.SetHeader("Content-Type","video/mp4");
                  HTTP_S.SetBody(MP4::mdatFold(FlashBuf.str()));
                  conn.Send(HTTP_S.BuildResponse("200", "OK"));
                  Flash_RequestPending--;
                  #if DEBUG >= 3
                  fprintf(stderr, "Sending a fragment\n");
                  #endif
                }
              }
              FlashBuf.str("");
              flashbuf_nonempty = 0;
              //fill buffer with init data, if needed.
              if (Strm.metadata.isMember("audio") && Strm.metadata["audio"].isMember("init")){
                tmp.DTSCAudioInit(Strm);
                FlashBuf.write(tmp.data, tmp.len);
              }
              if (Strm.metadata.isMember("video") && Strm.metadata["video"].isMember("init")){
                tmp.DTSCVideoInit(Strm);
                FlashBuf.write(tmp.data, tmp.len);
              }
            }
            if (Strm.lastType() == DTSC::VIDEO || Strm.lastType() == DTSC::AUDIO){
              ++flashbuf_nonempty;
              FlashBuf.write(tag.data, tag.len);
            }
          }else{
            if (pending_manifest && !Strm.metadata.isNull()){
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type","text/xml");
              HTTP_S.SetHeader("Cache-Control","no-cache");
              if (Strm.metadata.isMember("length")){receive_marks = true;}
              std::string manifest = BuildManifest(streamname, Strm.metadata);
              HTTP_S.SetBody(manifest);
              conn.Send(HTTP_S.BuildResponse("200", "OK"));
              #if DEBUG >= 3
              printf("Sent manifest\n");
              #endif
              pending_manifest = false;
            }
          }
        }
        if (!ss.connected()){break;}
      }
    }
    conn.close();
    ss.Send("S ");
    ss.Send(conn.getStats("HTTP_Dynamic").c_str());
    ss.flush();
    ss.close();
    #if DEBUG >= 1
    if (FLV::Parse_Error){fprintf(stderr, "FLV Parser Error: %s\n", FLV::Error_Str.c_str());}
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
  }//Connector_HTTP_Dynamic main function

};//Connector_HTTP_Dynamic namespace

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server("/tmp/mist/http_dynamic");
  if (!server_socket.connected()){return 1;}
  conf.activate();
  
  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){//check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){//if new child, start MAINHANDLER
        return Connector_HTTP::Connector_HTTP_Dynamic(S);
      }else{//otherwise, do nothing or output debugging text
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
        #endif
      }
    }
  }//while connected
  server_socket.close();
  return 0;
}//main
