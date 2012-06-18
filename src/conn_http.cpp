/// \file conn_http.cpp
/// Contains the main code for the HTTP Connector

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
#include "../lib/socket.h"
#include "../lib/http_parser.h"
#include "../lib/json.h"
#include "../lib/dtsc.h"
#include "../lib/flv_tag.h"
#include "../lib/base64.h"
#include "../lib/amf.h"
#include "../lib/mp4.h"

/// Holds everything unique to HTTP Connector.
namespace Connector_HTTP{

  /// Defines the type of handler used to process this request.
  enum {HANDLER_NONE, HANDLER_PROGRESSIVE, HANDLER_FLASH, HANDLER_APPLE, HANDLER_MICRO, HANDLER_JSCRIPT};

  std::queue<std::string> Flash_FragBuffer;///<Fragment buffer for F4V
  DTSC::Stream Strm;///< Incoming stream buffer.
  HTTP::Parser HTTP_R, HTTP_S;///<HTTP Receiver en HTTP Sender.

  /// Returns AMF-format metadata for Adobe HTTP Dynamic Streaming.
  std::string GetMetaData( ) {
    /// \todo Make this actually do what it should - even though it seems to be ignored completely by all media players.
    AMF::Object amfreply("container", AMF::AMF0_DDV_CONTAINER);
    amfreply.addContent(AMF::Object("onMetaData",AMF::AMF0_STRING));
    amfreply.addContent(AMF::Object("",AMF::AMF0_ECMA_ARRAY));
    amfreply.getContentP(1)->addContent(AMF::Object("trackinfo", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->addContent(AMF::Object("arrVal"));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("timescale",(double)1000));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("length",(double)59641700));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("language","eng"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->addContent(AMF::Object("arrVal"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(0)->getContentP(1)->getContentP(0)->addContent(AMF::Object("sampletype","avc1"));
    amfreply.getContentP(1)->getContentP(0)->addContent(AMF::Object("arrVal"));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("timescale",(double)44100));
    //amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("length",(double)28630000));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("language","eng"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->addContent(AMF::Object("sampledescription", AMF::AMF0_STRICT_ARRAY));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->addContent(AMF::Object("arrVal"));
    amfreply.getContentP(1)->getContentP(0)->getContentP(1)->getContentP(1)->getContentP(0)->addContent(AMF::Object("sampletype","mp4a"));
    amfreply.getContentP(1)->addContent(AMF::Object("audiochannels",(double)2));
    amfreply.getContentP(1)->addContent(AMF::Object("audiosamplerate",(double)44100));
    amfreply.getContentP(1)->addContent(AMF::Object("videoframerate",(double)25));
    amfreply.getContentP(1)->addContent(AMF::Object("aacaot",(double)2));
    amfreply.getContentP(1)->addContent(AMF::Object("avclevel",(double)12));
    amfreply.getContentP(1)->addContent(AMF::Object("avcprofile",(double)77));
    amfreply.getContentP(1)->addContent(AMF::Object("audiocodecid","mp4a"));
    amfreply.getContentP(1)->addContent(AMF::Object("videocodecid","avc1"));
    amfreply.getContentP(1)->addContent(AMF::Object("width",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("height",(double)720));
    amfreply.getContentP(1)->addContent(AMF::Object("frameWidth",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("frameHeight",(double)720));
    amfreply.getContentP(1)->addContent(AMF::Object("displayWidth",(double)1280));
    amfreply.getContentP(1)->addContent(AMF::Object("displayHeight",(double)720));
    //amfreply.getContentP(1)->addContent(AMF::Object("moovposition",(double)35506700));
    //amfreply.getContentP(1)->addContent(AMF::Object("duration",(double)596.458));
    return amfreply.Pack( );
  }//getMetaData

  /// Returns a F4M-format manifest file for Adobe HTTP Dynamic Streaming.
  std::string BuildManifest(std::string MovieId) {
    std::string Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n";
    Result += "<id>";
    Result += MovieId;
    Result += "</id>\n<mimeType>video/mp4</mimeType>\n";
    Result += "<streamType>live</streamType>\n";
    Result += "<deliveryType>streaming</deliveryType>\n";
    Result += "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">";
    Result += Base64::encode(MP4::GenerateLiveBootstrap(1));
    Result += "</bootstrapInfo>\n";
    Result += "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"";
    Result += MovieId;
    Result += "/\">\n";
    Result += "<metadata>";
    Result += Base64::encode(GetMetaData());
    Result += "</metadata>\n";
    Result += "</media>\n";
    Result += "</manifest>\n";
    return Result;
  }//BuildManifest

  /// Handles Progressive download streaming requests
  void Progressive(FLV::Tag & tag, HTTP::Parser HTTP_S, Socket::Connection & conn, DTSC::Stream & Strm){
    static bool progressive_has_sent_header = false;
    if (!progressive_has_sent_header){
      HTTP_S.Clean();//make sure no parts of old requests are left in any buffers
      HTTP_S.SetHeader("Content-Type", "video/x-flv");//Send the correct content-type for FLV files
      //HTTP_S.SetHeader("Transfer-Encoding", "chunked");
      HTTP_S.protocol = "HTTP/1.0";
      conn.Send(HTTP_S.BuildResponse("200", "OK"));//no SetBody = unknown length - this is intentional, we will stream the entire file
      conn.Send(std::string(FLV::Header, 13));//write FLV header
      static FLV::Tag tmp;
      //write metadata
      tmp.DTSCMetaInit(Strm);
      conn.Send(std::string(tmp.data, tmp.len));
      //write video init data, if needed
      if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
        tmp.DTSCVideoInit(Strm);
        conn.Send(std::string(tmp.data, tmp.len));
      }
      //write audio init data, if needed
      if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
        tmp.DTSCAudioInit(Strm);
        conn.Send(std::string(tmp.data, tmp.len));
      }
      progressive_has_sent_header = true;
      #if DEBUG >= 1
      fprintf(stderr, "Sent progressive FLV header\n");
      #endif
    }
    conn.Send(std::string(tag.data, tag.len));//write the tag contents
  }

  /// Handles Flash Dynamic HTTP streaming requests
  void FlashDynamic(FLV::Tag & tag, DTSC::Stream & Strm){
    static std::string FlashBuf;
    static FLV::Tag tmp;
    if (Strm.getPacket(0).getContentP("keyframe")){
      if (FlashBuf != ""){
        Flash_FragBuffer.push(FlashBuf);
        while (Flash_FragBuffer.size() > 2){
          Flash_FragBuffer.pop();
        }
        #if DEBUG >= 4
        fprintf(stderr, "Received a fragment. Now %i in buffer.\n", (int)Flash_FragBuffer.size());
        #endif
      }
      FlashBuf.clear();
      //fill buffer with init data, if needed.
      if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
        tmp.DTSCAudioInit(Strm);
        FlashBuf.append(tmp.data, tmp.len);
      }
      if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
        tmp.DTSCVideoInit(Strm);
        FlashBuf.append(tmp.data, tmp.len);
      }
    }
    FlashBuf.append(tag.data, tag.len);
  }


  /// Main function for Connector_HTTP
  int Connector_HTTP(Socket::Connection conn){
    int handler = HANDLER_PROGRESSIVE;///< The handler used for processing this request.
    bool ready4data = false;///< Set to true when streaming is to begin.
    bool inited = false;
    Socket::Connection ss(-1);
    std::string streamname;
    FLV::Tag tag;///< Temporary tag buffer.
    std::string recBuffer = "";

    std::string Movie;
    std::string Quality;
    int Segment = -1;
    int ReqFragment = -1;
    int temp;
    int Flash_RequestPending = 0;
    unsigned int lastStats = 0;
    conn.setBlocking(false);//do not block on conn.spool() when no data is available

    while (conn.connected()){
      //only parse input if available or not yet init'ed
      if (conn.spool()){
        if (HTTP_R.Read(conn.Received())){
          handler = HANDLER_PROGRESSIVE;
          #if DEBUG >= 4
          std::cout << "Received request: " << HTTP_R.url << std::endl;
          #endif
          if ((HTTP_R.url.find("Seg") != std::string::npos) && (HTTP_R.url.find("Frag") != std::string::npos)){handler = HANDLER_FLASH;}
          if (HTTP_R.url.find("f4m") != std::string::npos){handler = HANDLER_FLASH;}
          if (HTTP_R.url == "/crossdomain.xml"){
            handler = HANDLER_NONE;
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "text/xml");
            HTTP_S.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
            conn.Send(HTTP_S.BuildResponse("200", "OK"));
            #if DEBUG >= 3
            printf("Sending crossdomain.xml file\n");
            #endif
          }
          if (HTTP_R.url.length() > 10 && HTTP_R.url.substr(0, 7) == "/embed_" && HTTP_R.url.substr(HTTP_R.url.length() - 3, 3) == ".js"){
            streamname = HTTP_R.url.substr(7, HTTP_R.url.length() - 10);
            JSON::Value ServConf = JSON::fromFile("/tmp/mist/streamlist");
            std::string response;
            handler = HANDLER_NONE;
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "application/javascript");
            response = "// Generating embed code for stream " + streamname + "\n\n";
            if (ServConf["streams"].isMember(streamname)){
              std::string streamurl = "http://" + HTTP_S.GetHeader("Host") + "/" + streamname + ".flv";
              response += "// Stream URL: " + streamurl + "\n\n";
              response += "document.write('<object width=\"600\" height=\"409\"><param name=\"movie\" value=\"http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf\"></param><param name=\"flashvars\" value=\"src="+HTTP::Parser::urlencode(streamurl)+"&controlBarMode=floating\"></param><param name=\"allowFullScreen\" value=\"true\"></param><param name=\"allowscriptaccess\" value=\"always\"></param><embed src=\"http://fpdownload.adobe.com/strobe/FlashMediaPlayback.swf\" type=\"application/x-shockwave-flash\" allowscriptaccess=\"always\" allowfullscreen=\"true\" width=\"600\" height=\"409\" flashvars=\"src="+HTTP::Parser::urlencode(streamurl)+"&controlBarMode=floating\"></embed></object>');\n";
            }else{
              response += "// Stream not available at this server.\nalert(\"This stream is currently not available at this server.\\\\nPlease try again later!\");";
            }
            response += "";
            HTTP_S.SetBody(response);
            conn.Send(HTTP_S.BuildResponse("200", "OK"));
            #if DEBUG >= 3
            printf("Sending embed code for %s\n", streamname.c_str());
            #endif
          }
          if (handler == HANDLER_FLASH){
            if (HTTP_R.url.find("f4m") == std::string::npos){
              Movie = HTTP_R.url.substr(1);
              Movie = Movie.substr(0,Movie.find("/"));
              Quality = HTTP_R.url.substr( HTTP_R.url.find("/",1)+1 );
              Quality = Quality.substr(0, Quality.find("Seg"));
              temp = HTTP_R.url.find("Seg") + 3;
              Segment = atoi( HTTP_R.url.substr(temp,HTTP_R.url.find("-",temp)-temp).c_str());
              temp = HTTP_R.url.find("Frag") + 4;
              ReqFragment = atoi( HTTP_R.url.substr(temp).c_str() );
              #if DEBUG >= 4
              printf( "Quality: %s, Seg %d Frag %d\n", Quality.c_str(), Segment, ReqFragment);
              #endif
              Flash_RequestPending++;
            }else{
              Movie = HTTP_R.url.substr(1);
              Movie = Movie.substr(0,Movie.find("/"));
              HTTP_S.Clean();
              HTTP_S.SetHeader("Content-Type","text/xml");
              HTTP_S.SetHeader("Cache-Control","no-cache");
              std::string manifest = BuildManifest(Movie);
              HTTP_S.SetBody(manifest);
              conn.Send(HTTP_S.BuildResponse("200", "OK"));
              #if DEBUG >= 3
              printf("Sent manifest\n");
              #endif
            }
            ready4data = true;
          }//FLASH handler
          if (handler == HANDLER_PROGRESSIVE){
            //we assume the URL is the stream name with a 3 letter extension
            std::string extension = HTTP_R.url.substr(HTTP_R.url.size()-4);
            Movie = HTTP_R.url.substr(0, HTTP_R.url.size()-4);//strip the extension
            /// \todo VoD streams will need support for position reading from the URL parameters
            ready4data = true;
          }//PROGRESSIVE handler
          if (Movie != "" && Movie != streamname){
            #if DEBUG >= 4
            printf("Buffer switch detected (%s -> %s)! (Re)connecting buffer...\n", streamname.c_str(), Movie.c_str());
            #endif
            streamname = Movie;
            inited = false;
            ss.close();
            if (inited && handler == HANDLER_PROGRESSIVE){
              #if DEBUG >= 4
              printf("Progressive-mode reconnect impossible - disconnecting.\n");
              #endif
              conn.close();
              ready4data = false;
            }
          }
          HTTP_R.Clean(); //clean for any possible next requests
        }else{
          #if DEBUG >= 3
          fprintf(stderr, "Could not parse the following:\n%s\n", conn.Received().c_str());
          #endif
        }
      }
      if (ready4data){
        if (!inited){
          //we are ready, connect the socket!
          ss = Socket::getStream(streamname);
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
          #if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
          #endif
          inited = true;
        }
        if ((Flash_RequestPending > 0) && !Flash_FragBuffer.empty()){
          HTTP_S.Clean();
          HTTP_S.SetHeader("Content-Type","video/mp4");
          HTTP_S.SetBody(MP4::mdatFold(Flash_FragBuffer.front()));
          Flash_FragBuffer.pop();
          conn.Send(HTTP_S.BuildResponse("200", "OK"));
          Flash_RequestPending--;
          #if DEBUG >= 3
          fprintf(stderr, "Sending a video fragment. %i left in buffer, %i requested\n", (int)Flash_FragBuffer.size(), Flash_RequestPending);
          #endif
        }
        unsigned int now = time(0);
        if (now != lastStats){
          lastStats = now;
          ss.Send("S "+conn.getStats("HTTP"));
        }
        if (ss.spool() || ss.Received() != ""){
          if (Strm.parsePacket(ss.Received())){
            tag.DTSCLoader(Strm);
            if (handler == HANDLER_FLASH){
              FlashDynamic(tag, Strm);
            }
            if (handler == HANDLER_PROGRESSIVE){
              Progressive(tag, HTTP_S, conn, Strm);
            }
          }
        }
      }
    }
    conn.close();
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
  }//Connector_HTTP main function

};//Connector_HTTP namespace

// Load main server setup file, default port 8080, handler is Connector_HTTP::Connector_HTTP
#define DEFAULT_PORT 8080
#define MAINHANDLER Connector_HTTP::Connector_HTTP
#define CONFIGSECT HTTP
#include "server_setup.h"
