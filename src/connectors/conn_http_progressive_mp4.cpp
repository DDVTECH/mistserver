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
#include <mist/mp4.h>
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

    //MP4 specific variables
    MP4::DTSC2MP4Converter Conv;
    std::vector<MP4::keyPart>::iterator keyPartIt;
    
    unsigned int lastStats = 0;//Indicates the last time that we have sent stats to the server socket.
    unsigned int seek_sec = 0;//Seek position in ms
    unsigned int seek_byte = 0;//Seek position in bytes
    
    int videoID = -1;
    int audioID = -1;

    while (conn.connected()){
      //Only attempt to parse input when not yet init'ed.
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
#if DEBUG >= 5
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
          Strm.waitForMeta(ss);
          //build header here and set iterator
          HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
          HTTP_S.SetHeader("Content-Type", "video/MP4"); //Send the correct content-type for FLV files
          HTTP_S.protocol = "HTTP/1.0";
          conn.SendNow(HTTP_S.BuildResponse("200", "OK")); //no SetBody = unknown length - this is intentional, we will stream the entire file
          conn.SendNow(Conv.DTSCMeta2MP4Header(Strm.metadata));//SENDING MP4HEADER
          keyPartIt = Conv.keyParts.begin();
          {//using scope to have cmd not declared after action
            std::stringstream cmd;
            cmd << "t "<< (*keyPartIt).trackID;
            cmd << "\ns " << (*keyPartIt).time;
            cmd << "\no\n";
            ss.SendNow(cmd.str());
          }
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
          //wait until we have a header
          while ( !Strm.metadata && ss.connected()){
            if (ss.spool()){
              Strm.parsePacket(ss.Received()); //read the metadata
            }else{
              Util::sleep(5);
            }
          }
          int byterate = 0;
          for (JSON::ObjIter objIt = Strm.metadata["tracks"].ObjBegin(); objIt != Strm.metadata["tracks"].ObjEnd(); objIt++){
            if (videoID == -1 && objIt->second["type"].asString() == "video"){
              videoID = objIt->second["trackid"].asInt();
            }
            if (audioID == -1 && objIt->second["type"].asString() == "audio"){
              audioID = objIt->second["trackid"].asInt();
            }
          }
          if (videoID != -1){
            byterate += Strm.getTrackById(videoID)["bps"].asInt();
          }
          if (audioID != -1){
            byterate += Strm.getTrackById(audioID)["bps"].asInt();
          }
          if ( !byterate){byterate = 1;}
          seek_sec = (seek_byte / byterate) * 1000;
          
          inited = true;
        }
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Progressive_MP4").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              keyPartIt++;
              if (keyPartIt != Conv.keyParts.end()){
                //Schop player
                //t trackID
                //s (*keyPartIt).time
                //p time+len
                std::stringstream cmd;
                cmd << "t "<< (*keyPartIt).trackID;
                cmd << "\ns " << (*keyPartIt).time;
                cmd << "\no\n";
                //std::cerr << cmd.str() << std::endl;
                ss.SendNow(cmd.str());
              }
            }else if(Strm.lastType() == DTSC::AUDIO || Strm.lastType() == DTSC::VIDEO){
              //std::cerr << "send data" << std::endl;
              //parse DTSC to MP4 here
              conn.SendNow(Strm.lastData());//send out and clear Convverter buffer
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
    ss.SendNow(conn.getStats("HTTP_Dynamic").c_str());
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
  capa["url_rel"] = "/$.mp4";
  capa["url_match"] = "/$.mp4";
  capa["codecs"][0u][0u].append("H264");
  capa["codecs"][0u][1u].append("AAC");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "html5/video/mp4";
  capa["methods"][0u]["priority"] = 8ll;
  capa["socket"] = "http_progressive_mp4";
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
