///\file conn_http_progressive_ogg.cpp
///\brief Contains the main code for the HTTP Progressive OGG Connector

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
#include <mist/ogg.h>
#include <mist/amf.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>
#include "../converters/oggconv.h"

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

    //OGG specific variables
    //OGG::headerPages oggMeta;
    //OGG::Page curOggPage;
    OGG::converter oggConv;
    std::map <long long unsigned int, std::vector<JSON::Value> > DTSCBuffer;
    //std::map <long long unsigned int, long long unsigned int> prevGran;
    std::vector<unsigned int> curSegTable;
    long long int currID = 0;
    std::string sendBuffer;
    
    unsigned int lastStats = 0;//Indicates the last time that we have sent stats to the server socket.
    
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
          for (std::map<int,DTSC::Track>::iterator it = Strm.metadata.tracks.begin(); it != Strm.metadata.tracks.end(); it++){
            if (videoID == -1 && it->second.codec == "theora"){
              videoID = it->second.trackID;
            }
            if (audioID == -1 && it->second.codec == "vorbis"){
              audioID = it->second.trackID;
            }
          }
          if (videoID == -1 && audioID == -1){
            HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
            HTTP_S.SetBody("This stream contains no OGG compatible codecs");
            HTTP_S.SendResponse("406", "Not acceptable",conn); 
            HTTP_R.Clean();
            continue;
          }
          std::stringstream cmd;
          cmd << "t";
          if (videoID != -1){
            cmd << " " << videoID;
          }
          if (audioID != -1){
            cmd << " " << audioID;
          }
          cmd << "\np\n";
          ss.SendNow(cmd.str().c_str(), cmd.str().size());
          inited = true;
        }
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Progressive_Ogg").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            
            if ( !progressive_has_sent_header){
              HTTP_S.Clean(); //make sure no parts of old requests are left in any buffers
              HTTP_S.SetHeader("Content-Type", "video/ogg"); //Send the correct content-type for FLV files
              HTTP_S.protocol = "HTTP/1.0";
              conn.SendNow(HTTP_S.BuildResponse("200", "OK")); //no SetBody = unknown length - this is intentional, we will stream the entire file
              //Fill in ogg header here
              oggConv.readDTSCHeader(Strm.metadata);
              conn.SendNow((char*)oggConv.parsedPages.c_str(), oggConv.parsedPages.size());
              progressive_has_sent_header = true;
            }
            //parse DTSC to Ogg here
            if (Strm.lastType() == DTSC::AUDIO || Strm.lastType() == DTSC::VIDEO){
              std::string tmpString;
              oggConv.readDTSCVector(Strm.getPacket(), tmpString);
              conn.SendNow(tmpString);
              
            }
            if (Strm.lastType() == DTSC::PAUSEMARK){
              conn.close();
              ss.close();
              //last page output
            }
            if (Strm.lastType() == DTSC::INVALID){
              #if DEBUG >= 3
              fprintf(stderr, "Invalid packet received - closing connection.\n");
              #endif
              conn.close();
            }
          }
        }else{
          Util::sleep(100);
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
  capa["url_rel"] = "/$.ogg";
  capa["url_match"] = "/$.ogg";
  capa["socket"] = "http_progressive_ogg";
  capa["codecs"][0u][0u].append("theora");
  capa["codecs"][0u][1u].append("vorbis");
  capa["methods"][0u]["handler"] = "http";
  capa["methods"][0u]["type"] = "html5/video/ogg";
  capa["methods"][0u]["priority"] = 8ll;
  capa["methods"][0u]["nolive"] = 1;
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
