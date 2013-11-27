///\file conn_http_progressive.cpp
///\brief Contains the main code for the HTTP Progressive Connector

#include <iostream>
#include <queue>
#include <sstream>

#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <mist/socket.h>
#include <mist/http_parser.h>
#include <mist/dtsc.h>
#include <mist/flv_tag.h>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>

///\brief Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  ///\brief Main function for the HTTP Progressive Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int progressiveConnector(Socket::Connection conn){
    bool progressive_has_sent_header = false;//Indicates whether we have sent a header.
    bool ready4data = false; //Set to true when streaming is to begin.
    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S;//HTTP Receiver en HTTP Sender.
    bool inited = false;//Whether the stream is initialized
    Socket::Connection ss( -1);//The Stream Socket, used to connect to the desired stream.
    std::string streamname;//Will contain the name of the stream.
    FLV::Tag tag;//Temporary tag buffer.

    unsigned int lastStats = 0;//Indicates the last time that we have sent stats to the server socket.
    unsigned int seek_sec = 0;//Seek position in ms
    unsigned int seek_byte = 0;//Seek position in bytes
    
    bool isMP3 = false;//Indicates whether the request is audio-only mp3.
    
    int videoID = -1;
    int audioID = -1;

    while (conn.connected()){
      //Only attempt to parse input when not yet init'ed.
      if ( !inited){
        if (conn.Received().size() || conn.spool()){
          if (HTTP_R.Read(conn)){
#if DEBUG >= 5
            std::cout << "Received request: " << HTTP_R.getUrl() << std::endl;
#endif
            conn.setHost(HTTP_R.GetHeader("X-Origin"));
            streamname = HTTP_R.GetHeader("X-Stream");
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
              seek_byte = 0;
            }else{
              seek_byte = start; //divide by 1mbit, then *1000 for ms.
              seek_sec = 0;
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
          Strm.waitForMeta(ss);
          int byterate = 0;
          for (std::map<int,DTSC::Track>::iterator it = Strm.metadata.tracks.begin(); it != Strm.metadata.tracks.end(); it++){
            if (videoID == -1 && it->second.type == "video"){
              videoID = it->second.trackID;
            }
            if (audioID == -1 && it->second.type == "audio"){
              audioID = it->second.trackID;
            }
          }
          if (videoID != -1){
            byterate += Strm.metadata.tracks[videoID].bps;
          }
          if (audioID != -1){
            byterate += Strm.metadata.tracks[audioID].bps;
          }
          if ( !byterate){byterate = 1;}
          if (seek_byte){
            seek_sec = (seek_byte / byterate) * 1000;
          }
          std::stringstream cmd;
          cmd << "t";
          if (videoID != -1){
            cmd << " " << videoID;
          }
          if (audioID != -1){
            cmd << " " << audioID;
          }
          cmd << "\ns " << seek_sec << "\np\n";
          ss.SendNow(cmd.str().c_str(), cmd.str().size());
          inited = true;
        }
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Progressive_FLV").c_str());
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
              tag.DTSCMetaInit(Strm, Strm.metadata.tracks[videoID], Strm.metadata.tracks[audioID]);
              conn.SendNow(tag.data, tag.len);
              //write video init data, if needed
              if (videoID != -1){
                tag.DTSCVideoInit(Strm.metadata.tracks[videoID]);
                conn.SendNow(tag.data, tag.len);
              }
              //write audio init data, if needed
              if (audioID != -1){
                tag.DTSCAudioInit(Strm.metadata.tracks[audioID]);
                conn.SendNow(tag.data, tag.len);
              }
              progressive_has_sent_header = true;
            }
            if (Strm.lastType() == DTSC::PAUSEMARK){
              conn.close();
            }
            if (Strm.lastType() == DTSC::INVALID){
              #if DEBUG >= 3
              fprintf(stderr, "Invalid packet received - closing connection.\n");
              #endif
              conn.close();
            }
            if (Strm.lastType() == DTSC::AUDIO || Strm.lastType() == DTSC::VIDEO){
              std::string codec = Strm.metadata.tracks[Strm.getPacket()["trackid"].asInt()].codec;
              if (codec == "AAC" || codec == "MP3" || codec == "H264" || codec == "H263" || codec == "VP6"){
                tag.DTSCLoader(Strm);
                conn.SendNow(tag.data, tag.len); //write the tag contents
              }
            }
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
    ss.SendNow(conn.getStats("HTTP_Progressive_FLV").c_str());
    ss.close();
    return 0;
  } //Progressive_Connector main function

} //Connector_HTTP namespace

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["desc"] = "Enables HTTP protocol progressive streaming.";
  capa["deps"] = "HTTP";
  capa["url_rel"] = "/$.flv";
  capa["url_match"] = "/$.flv";
  capa["socket"] = "http_progressive_flv";
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][0u].append("H263");
  capa["codecs"][0u][0u].append("VP6");
  capa["codecs"][0u][1u].append("AAC");
  capa["codecs"][0u][1u].append("MP3");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "flash/7";
  capa["methods"][0u]["priority"] = 5ll;
  conf.addBasicConnectorOptions(capa);
  conf.parseArgs(argc, argv);
  
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  
  Socket::Server server_socket = Socket::Server(Util::getTmpFolder() + capa["socket"].asStringRef());
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::progressiveConnector(S);
      }else{ //otherwise, do nothing or output debugging text
#if DEBUG >= 5
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, S.getSocket());
#endif
      }
    }
  } //while connected
  server_socket.close();
  return 0;
} //main
