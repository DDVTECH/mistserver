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
#include <sstream>
#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>

/// Holds everything unique to HTTP Progressive Connector.
namespace Connector_HTTP {

  /// Main function for Connector_HTTP_Progressive
  int Connector_HTTP_Progressive(Socket::Connection conn){
    bool progressive_has_sent_header = false;
    bool ready4data = false; ///< Set to true when streaming is to begin.
    DTSC::Stream Strm; ///< Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; ///<HTTP Receiver en HTTP Sender.
    bool inited = false;
    Socket::Connection ss( -1);
    std::string streamname;
    FLV::Tag tag; ///< Temporary tag buffer.

    unsigned int lastStats = 0;
    unsigned int seek_sec = 0; //seek position in ms
    unsigned int seek_byte = 0; //seek position in bytes

    while (conn.connected()){
      //only parse input if available or not yet init'ed
      if ( !inited){
        if (conn.Received().size() || conn.spool()){
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
            //we assume the URL is the stream name with a 3 letter extension
            streamname = HTTP_R.getUrl().substr(1);
            size_t extDot = streamname.rfind('.');
            if (extDot != std::string::npos){
              streamname.resize(extDot);
            }; //strip the extension
            int start = 0;
            if ( !HTTP_R.GetVar("start").empty()){
              start = atoi(HTTP_R.GetVar("start").c_str());
            }
            if ( !HTTP_R.GetVar("starttime").empty()){
              start = atoi(HTTP_R.GetVar("starttime").c_str());
            }
            if ( !HTTP_R.GetVar("apstart").empty()){
              start = atoi(HTTP_R.GetVar("apstart").c_str());
            }
            if ( !HTTP_R.GetVar("ec_seek").empty()){
              start = atoi(HTTP_R.GetVar("ec_seek").c_str());
            }
            if ( !HTTP_R.GetVar("fs").empty()){
              start = atoi(HTTP_R.GetVar("fs").c_str());
            }
            //under 3 hours we assume seconds, otherwise byte position
            if (start < 10800){
              seek_sec = start * 1000; //ms, not s
            }else{
              seek_byte = start; //divide by 1mbit, then *1000 for ms.
            }
            ready4data = true;
            HTTP_R.Clean(); //clean for any possible next requests
          }
        }
      }
      if (ready4data){
        if ( !inited){
          //we are ready, connect the socket!
          ss = Util::Stream::getStream(streamname);
          if ( !ss.connected()){
#if DEBUG >= 1
            fprintf(stderr, "Could not connect to server for %s!\n", streamname.c_str());
#endif
            ss.close();
            HTTP_S.Clean();
            HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
            conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
            ready4data = false;
            continue;
          }
          if (seek_byte){
            //wait until we have a header
            while ( !Strm.metadata){
              if (ss.spool()){
                Strm.parsePacket(ss.Received()); //read the metadata
              }else{
                Util::sleep(5);
              }
            }
            int byterate = 0;
            if (Strm.metadata.isMember("video")){
              byterate += Strm.metadata["video"]["bps"].asInt();
            }
            if (Strm.metadata.isMember("audio")){
              byterate += Strm.metadata["audio"]["bps"].asInt();
            }
            seek_sec = (seek_byte / byterate) * 1000;
          }
          if (seek_sec){
            std::stringstream cmd;
            cmd << "s " << seek_sec << "\n";
            ss.SendNow(cmd.str().c_str());
          }
#if DEBUG >= 3
          fprintf(stderr, "Everything connected, starting to send video data...\n");
#endif
          ss.SendNow("p\n");
          inited = true;
        }
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Progressive").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if ( !progressive_has_sent_header){
              HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
              HTTP_S.SetHeader("Content-Type", "video/x-flv"); //Send the correct content-type for FLV files
              //HTTP_S.SetHeader("Transfer-Encoding", "chunked");
              HTTP_S.protocol = "HTTP/1.0";
              conn.SendNow(HTTP_S.BuildResponse("200", "OK")); //no SetBody = unknown length - this is intentional, we will stream the entire file
              conn.SendNow(FLV::Header, 13); //write FLV header
              //write metadata
              tag.DTSCMetaInit(Strm);
              conn.SendNow(tag.data, tag.len);
              //write video init data, if needed
              if (Strm.metadata.isMember("video") && Strm.metadata["video"].isMember("init")){
                tag.DTSCVideoInit(Strm);
                conn.SendNow(tag.data, tag.len);
              }
              //write audio init data, if needed
              if (Strm.metadata.isMember("audio") && Strm.metadata["audio"].isMember("init")){
                tag.DTSCAudioInit(Strm);
                conn.SendNow(tag.data, tag.len);
              }
              progressive_has_sent_header = true;
#if DEBUG >= 1
              fprintf(stderr, "Sent progressive FLV header\n");
#endif
            }
            tag.DTSCLoader(Strm);
            conn.SendNow(tag.data, tag.len); //write the tag contents
          }
        }else{
          Util::sleep(1);
        }
        if ( !ss.connected()){
          break;
        }
      }
    }
    conn.close();
    ss.SendNow(conn.getStats("HTTP_Dynamic").c_str());
    ss.close();
#if DEBUG >= 1
    if (FLV::Parse_Error){
      fprintf(stderr, "FLV Parser Error: %s\n", FLV::Error_Str.c_str());
    }
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
  } //Connector_HTTP main function

} //Connector_HTTP namespace

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server("/tmp/mist/http_progressive");
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::Connector_HTTP_Progressive(S);
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
