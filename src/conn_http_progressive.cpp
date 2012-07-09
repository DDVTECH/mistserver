/// \file conn_http_progressive.cpp
/// Contains the main code for the HTTP Progressive Connector

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
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/amf.h>

/// Holds everything unique to HTTP Progressive Connector.
namespace Connector_HTTP_Progressive{

  /// Main function for Connector_HTTP_Progressive
  int Connector_HTTP_Progressive(Socket::Connection conn){
    bool progressive_has_sent_header = false;
    bool ready4data = false;///< Set to true when streaming is to begin.
    DTSC::Stream Strm;///< Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S;///<HTTP Receiver en HTTP Sender.
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
          #if DEBUG >= 4
          std::cout << "Received request: " << HTTP_R.url << std::endl;
          #endif
          //we assume the URL is the stream name with a 3 letter extension
          std::string extension = HTTP_R.url.substr(HTTP_R.url.size()-4);
          Movie = HTTP_R.url.substr(0, HTTP_R.url.size()-4);//strip the extension
          /// \todo VoD streams will need support for position reading from the URL parameters
          ready4data = true;
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
        unsigned int now = time(0);
        if (now != lastStats){
          lastStats = now;
          ss.Send("S "+conn.getStats("HTTP_Progressive"));
        }
        if (ss.spool() || ss.Received() != ""){
          if (Strm.parsePacket(ss.Received())){
            tag.DTSCLoader(Strm);
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

// Load http setup file with the correct settings for this HTTP connector
#define MAINHANDLER Connector_HTTP_Progressive::Connector_HTTP_Progressive
#define CONNECTOR progressive
#include "server_setup_http.h"
