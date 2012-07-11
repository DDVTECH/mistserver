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
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/json.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/base64.h>
#include <mist/amf.h>
#include <mist/mp4.h>

/// Holds everything unique to HTTP Connector.
namespace Connector_HTTP{

  /// Main function for Connector_HTTP
  int Handle_Connection(Socket::Connection conn){
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
