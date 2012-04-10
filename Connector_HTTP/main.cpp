/// \file Connector_HTTP/main.cpp
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
#include "../util/socket.h"
#include "../util/http_parser.h"
#include "../util/dtsc.h"
#include "../util/flv_tag.h"
#include "../util/MP4/interface.cpp"
#include "../util/base64.h"
#include "../util/amf.h"

/// Holds everything unique to HTTP Connector.
namespace Connector_HTTP{

  /// Defines the type of handler used to process this request.
  enum {HANDLER_NONE, HANDLER_PROGRESSIVE, HANDLER_FLASH, HANDLER_APPLE, HANDLER_MICRO, HANDLER_JSCRIPT};

  std::queue<std::string> Flash_FragBuffer;///<Fragment buffer for F4V
  DTSC::Stream Strm;///< Incoming stream buffer.
  HTTP::Parser HTTP_R, HTTP_S;///<HTTP Receiver en HTTP Sender.
  

  /// Returns AMF-format metadata for Adobe HTTP Dynamic Streaming.
  std::string GetMetaData( ) {
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
    Interface * temp = new Interface;
    std::string Result="<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<manifest xmlns=\"http://ns.adobe.com/f4m/1.0\">\n";
    Result += "<id>";
    Result += MovieId;
    Result += "</id>\n<mimeType>video/mp4</mimeType>\n";
    Result += "<streamType>live</streamType>\n";
    Result += "<deliveryType>streaming</deliveryType>\n";
    Result += "<bootstrapInfo profile=\"named\" id=\"bootstrap1\">";
    Result += Base64::encode(temp->GenerateLiveBootstrap(1));
    Result += "</bootstrapInfo>\n";
    Result += "<media streamId=\"1\" bootstrapInfoId=\"bootstrap1\" url=\"";
    Result += MovieId;
    Result += "/\">\n";
    Result += "<metadata>";
    Result += Base64::encode(GetMetaData());
    Result += "</metadata>\n";
    Result += "</media>\n";
    Result += "</manifest>\n";
    delete temp;
    return Result;
  }//BuildManifest

  /// Handles Progressive download streaming requests
  void Progressive(FLV::Tag & tag, HTTP::Parser HTTP_S, Socket::Connection & conn, DTSC::Stream & Strm){
    static bool progressive_has_sent_header = false;
    if (!progressive_has_sent_header){
      HTTP_S.Clean();//troep opruimen die misschien aanwezig is...
      HTTP_S.SetHeader("Content-Type", "video/x-flv");//FLV files hebben altijd dit content-type.
      //HTTP_S.SetHeader("Transfer-Encoding", "chunked");
      HTTP_S.protocol = "HTTP/1.0";
      HTTP_S.SendResponse(conn, "200", "OK");//geen SetBody = unknown length! Dat willen we hier.
      //HTTP_S.SendBodyPart(CONN_fd, FLVHeader, 13);//schrijf de FLV header
      conn.write(FLV::Header, 13);
      static FLV::Tag tmp;
      tmp.DTSCMetaInit(Strm);
      conn.write(tmp.data, tmp.len);
      if (Strm.metadata.getContentP("audio") && Strm.metadata.getContentP("audio")->getContentP("init")){
        tmp.DTSCAudioInit(Strm);
        conn.write(tmp.data, tmp.len);
      }
      if (Strm.metadata.getContentP("video") && Strm.metadata.getContentP("video")->getContentP("init")){
        tmp.DTSCVideoInit(Strm);
        conn.write(tmp.data, tmp.len);
      }
      progressive_has_sent_header = true;
      #if DEBUG >= 1
      fprintf(stderr, "Sent progressive FLV header\n");
      #endif
    }
    //HTTP_S.SendBodyPart(CONN_fd, tag->data, tag->len);//schrijf deze FLV tag onbewerkt weg
    conn.write(tag.data, tag.len);
  }

  /// Handles Flash Dynamic HTTP streaming requests
  void FlashDynamic(FLV::Tag & tag, DTSC::Stream & Strm){
    static std::string FlashBuf;
    static FLV::Tag tmp;
    if (Strm.getPacket(0).getContentP("keyframe")){
      if (FlashBuf != ""){
        Flash_FragBuffer.push(FlashBuf);
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

    std::string Movie = "";
    std::string Quality = "";
    int Segment = -1;
    int ReqFragment = -1;
    int temp;
    int Flash_RequestPending = 0;
    bool Flash_ManifestSent = false;
    unsigned int lastStats = 0;
    //int CurrentFragment = -1; later herbruiken?

    while (conn.connected()){
      //only parse input if available or not yet init'ed
      if (HTTP_R.Read(conn, ready4data)){
        handler = HANDLER_PROGRESSIVE;
        std::cout << "Received request: " << HTTP_R.url << std::endl;
        if ((HTTP_R.url.find("Seg") != std::string::npos) && (HTTP_R.url.find("Frag") != std::string::npos)){handler = HANDLER_FLASH;}
        if (HTTP_R.url.find("f4m") != std::string::npos){handler = HANDLER_FLASH;}
        if (HTTP_R.url == "/crossdomain.xml"){
          handler = HANDLER_NONE;
          HTTP_S.Clean();
          HTTP_S.SetHeader("Content-Type", "text/xml");
          HTTP_S.SetBody("<?xml version=\"1.0\"?><!DOCTYPE cross-domain-policy SYSTEM \"http://www.adobe.com/xml/dtds/cross-domain-policy.dtd\"><cross-domain-policy><allow-access-from domain=\"*\" /><site-control permitted-cross-domain-policies=\"all\"/></cross-domain-policy>");
          HTTP_S.SendResponse(conn, "200", "OK");//geen SetBody = unknown length! Dat willen we hier.
          #if DEBUG >= 3
          printf("Sending crossdomain.xml file\n");
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
            printf( "URL: %s\n", HTTP_R.url.c_str());
            printf( "Movie: %s, Quality: %s, Seg %d Frag %d\n", Movie.c_str(), Quality.c_str(), Segment, ReqFragment);
            #endif
            Flash_RequestPending++;
          }else{
            Movie = HTTP_R.url.substr(1);
            Movie = Movie.substr(0,Movie.find("/"));
          }
          streamname = "/tmp/shared_socket_";
          for (std::string::iterator i=Movie.end()-1; i>=Movie.begin(); --i){
            if (!isalpha(*i) && !isdigit(*i) && *i != '_'){
              Movie.erase(i);
            }else{
              *i=tolower(*i);
            }//strip nonalphanumeric
          }
          streamname += Movie;
          if( !Flash_ManifestSent ) {
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type","text/xml");
            HTTP_S.SetHeader("Cache-Control","no-cache");
            HTTP_S.SetBody(BuildManifest(Movie));
            HTTP_S.SendResponse(conn, "200", "OK");
            Flash_ManifestSent = true;//stop manifest from being sent multiple times
            std::cout << "Sent manifest" << std::endl;
          }
          ready4data = true;
        }//FLASH handler
        if (handler == HANDLER_PROGRESSIVE){
          //in het geval progressive nemen we aan dat de URL de streamname is, met .flv erachter
          std::string extension = HTTP_R.url.substr(HTTP_R.url.size()-4);
          streamname = HTTP_R.url.substr(0, HTTP_R.url.size()-4);//strip de .flv
          for (std::string::iterator i=streamname.end()-1; i>=streamname.begin(); --i){
            if (!isalpha(*i) && !isdigit(*i) && *i != '_'){streamname.erase(i);}else{*i=tolower(*i);}//strip nonalphanumeric
          }
          streamname = "/tmp/shared_socket_" + streamname;//dit is dan onze shared_socket
          //normaal zouden we ook een position uitlezen uit de URL, maar bij LIVE streams is dat zinloos
          ready4data = true;
        }//PROGRESSIVE handler
        HTTP_R.CleanForNext(); //maak schoon na verwerken voor eventuele volgende requests...
      }
      if (ready4data){
        if (!inited){
          //we are ready, connect the socket!
          ss = Socket::Connection(streamname);
          if (!ss.connected()){
            #if DEBUG >= 1
            fprintf(stderr, "Could not connect to server!\n");
            #endif
            conn.close();
            break;
          }
          #if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
          #endif
          inited = true;
        }
        if ((Flash_RequestPending > 0) && !Flash_FragBuffer.empty()){
          HTTP_S.Clean();
          HTTP_S.SetHeader("Content-Type","video/mp4");
          HTTP_S.SetBody(Interface::mdatFold(Flash_FragBuffer.front()));
          Flash_FragBuffer.pop();
          HTTP_S.SendResponse(conn, "200", "OK");//schrijf de HTTP response header
          Flash_RequestPending--;
          #if DEBUG >= 3
          fprintf(stderr, "Sending a video fragment. %i left in buffer, %i requested\n", (int)Flash_FragBuffer.size(), Flash_RequestPending);
          #endif
        }
        if (inited){
          unsigned int now = time(0);
          if (now != lastStats){
            lastStats = now;
            std::string stat = "S "+conn.getStats("HTTP");
            ss.write(stat);
          }
        }
        if (ss.canRead()){
          ss.spool();
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
    if (inited) ss.close();
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
#include "../util/server_setup.cpp"
