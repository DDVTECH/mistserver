///\file conn_http_smooth.cpp
///\brief Contains the main code for the HTTP Smooth Connector

#include <iostream>
#include <iomanip>
#include <queue>
#include <sstream>

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
#include <mist/base64.h>
#include <mist/amf.h>
#include <mist/mp4.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/timing.h>

///\brief Holds everything unique to HTTP Connectors.
namespace Connector_HTTP {
  ///\brief Builds an index file for HTTP Smooth streaming.
  ///\param metadata The current metadata, used to generate the index.
  ///\return The index file for HTTP Smooth Streaming.
  std::string smoothIndex(JSON::Value & metadata){
    std::stringstream Result;
    Result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    Result << "<SmoothStreamingMedia "
              "MajorVersion=\"2\" "
              "MinorVersion=\"0\" "
              "TimeScale=\"10000000\" ";
    if (metadata.isMember("vod")){
      Result << "Duration=\"" << metadata["lastms"].asInt() << "0000\"";
    }else{
      Result << "Duration=\"0\" "
                "IsLive=\"TRUE\" "
                "LookAheadFragmentCount=\"2\" "
                "DVRWindowLength=\"" + metadata["buffer_window"].asString() + "0000\" "
                "CanSeek=\"TRUE\" "
                "CanPause=\"TRUE\" ";
    }
    Result << ">\n";
    //Add audio entries
    if (metadata.isMember("audio") && metadata["audio"]["codec"].asString() == "AAC"){
      Result << "<StreamIndex "
                "Type=\"audio\" "
                "QualityLevels=\"1\" "
                "Name=\"audio\" "
                "Chunks=\"" << metadata["keytime"].size() << "\" "
                "Url=\"Q({bitrate})/A({start time})\">\n";
      //Add audio qualities
      Result << "<QualityLevel "
                "Index=\"0\" "
                "Bitrate=\"" << metadata["audio"]["bps"].asInt() * 8 << "\" "
                "CodecPrivateData=\"" << std::hex;
      for (int i = 0; i < metadata["audio"]["init"].asString().size(); i++){
        Result << std::setfill('0') << std::setw(2) << std::right << (int)metadata["audio"]["init"].asString()[i];
      }
      Result << std::dec << "\" "
                "SamplingRate=\"" << metadata["audio"]["rate"].asInt() << "\" "
                "Channels=\"2\" "
                "BitsPerSample=\"16\" "
                "PacketSize=\"4\" "
                "AudioTag=\"255\" "
                "FourCC=\"AACL\" />\n";
      for (unsigned int i = 0; i < metadata["keylen"].size(); i++){
        Result << "<c ";
        if (i == 0){
          Result << "t=\"" << metadata["keytime"][i].asInt() * 10000 << "\" ";
        }
        Result << "d=\"" << metadata["keylen"][i].asInt() * 10000 << "\" />\n";
      }
      Result << "   </StreamIndex>\n";
    }
    //Add video entries
    if (metadata.isMember("video") && metadata["video"]["codec"].asString() == "H264"){
      Result << "<StreamIndex "
                "Type=\"video\" "
                "QualityLevels=\"1\" "
                "Name=\"video\" "
                "Chunks=\"" << metadata["keytime"].size() << "\" "
                "Url=\"Q({bitrate})/V({start time})\" "
                "MaxWidth=\"" << metadata["video"]["width"].asInt() << "\" "
                "MaxHeight=\"" << metadata["video"]["height"].asInt() << "\" "
                "DisplayWidth=\"" << metadata["video"]["width"].asInt() << "\" "
                "DisplayHeight=\"" << metadata["video"]["height"].asInt() << "\">\n";
      //Add video qualities
      Result << "<QualityLevel "
                "Index=\"0\" "
                "Bitrate=\"" << metadata["video"]["bps"].asInt() * 8 << "\" "
                "CodecPrivateData=\"" << std::hex;
      MP4::AVCC avccbox;
      avccbox.setPayload(metadata["video"]["init"].asString());
      std::string tmpString = avccbox.asAnnexB();
      for (int i = 0; i < tmpString.size(); i++){
        Result << std::setfill('0') << std::setw(2) << std::right << (int)tmpString[i];
      }
      Result << std::dec << "\" "
                "MaxWidth=\"" << metadata["video"]["width"].asInt() << "\" "
                "MaxHeight=\"" << metadata["video"]["height"].asInt() << "\" "
                "FourCC=\"AVC1\" />\n";
      for (unsigned int i = 0; i < metadata["keylen"].size(); i++){
        Result << "<c ";
        if (i == 0){
          Result << "t=\"" << metadata["keytime"][i].asInt() * 10000 << "\" ";
        }
        Result << "d=\"" << metadata["keylen"][i].asInt() * 10000 << "\" />\n";
      }
      Result << "</StreamIndex>\n";
    }
    Result << "</SmoothStreamingMedia>\n";

#if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result << std::endl;
#endif
    return Result.str();
  } //smoothIndex

  ///\brief Main function for the HTTP Smooth Connector
  ///\param conn A socket describing the connection the client.
  ///\return The exit code of the connector.
  int smoothConnector(Socket::Connection conn){
    std::deque<std::string> dataBuffer;//A buffer for the data that needs to be sent to the client.
    int dataSize = 0;//The amount of bytes in the dataBuffer

    DTSC::Stream Strm;//Incoming stream buffer.
    HTTP::Parser HTTP_R;//HTTP Receiver
    HTTP::Parser HTTP_S;//HTTP Sender.

    bool ready4data = false;//Set to true when streaming is to begin.
    Socket::Connection ss( -1);//The Stream Socket, used to connect to the desired stream.
    std::string streamname;//Will contain the name of the stream.

    bool wantsVideo = false;//Indicates whether this request is a video request.
    bool wantsAudio = false;//Indicates whether this request is an audio request.

    std::string Quality;//Indicates the request quality of the movie.
    long long int requestedTime = -1;//Indicates the fragment requested.
    std::string parseString;//A string used for parsing different aspects of the request.
    unsigned int lastStats = 0;//Indicates the last time that we have sent stats to the server socket.
    conn.setBlocking(false);//Set the client socket to non-blocking

    while (conn.connected()){
      if (conn.spool() || conn.Received().size()){
        //Make sure the received data ends in a newline (\n).
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
          //Get data set by the proxy.
          conn.setHost(HTTP_R.GetHeader("X-Origin"));
          streamname = HTTP_R.GetHeader("X-Stream");
          if ( !ss){
            //initiate Stream Socket
            ss = Util::Stream::getStream(streamname);
            if ( !ss.connected()){
              #if DEBUG >= 1
              fprintf(stderr, "Could not connect to server!\n");
              #endif
              HTTP_S.Clean();
              HTTP_S.SetBody("No such stream is available on the system. Please try again.\n");
              conn.SendNow(HTTP_S.BuildResponse("404", "Not found"));
              ready4data = false;
              continue;
            }
            ss.setBlocking(false);
            //Do nothing until metadata has been received.
            while ( !Strm.metadata && ss.connected()){
              if (ss.spool()){
                while (Strm.parsePacket(ss.Received())){
                  //do nothing
                }
              }
            }
          }
          if (HTTP_R.url.find("Manifest") == std::string::npos){
            //We have a non-manifest request, parse it.
            Quality = HTTP_R.url.substr(HTTP_R.url.find("/Q(", 8) + 3);
            Quality = Quality.substr(0, Quality.find(")"));
            parseString = HTTP_R.url.substr(HTTP_R.url.find(")/") + 2);
            wantsAudio = false;
            wantsVideo = false;
            if (parseString[0] == 'A'){
              wantsAudio = true;
            }
            if (parseString[0] == 'V'){
              wantsVideo = true;
            }
            parseString = parseString.substr(parseString.find("(") + 1);
            requestedTime = atoll(parseString.substr(0, parseString.find(")")).c_str());
            if (Strm.metadata.isMember("live")){
              int seekable = Strm.canSeekms(requestedTime / 10000);
              if (seekable == 0){
                // iff the fragment in question is available, check if the next is available too
                for (int i = 0; i < Strm.metadata["keytime"].size(); i++){
                  if (Strm.metadata["keytime"][i].asInt() >= (requestedTime / 10000)){
                    if (i + 1 == Strm.metadata["keytime"].size()){
                      seekable = 1;
                    }
                    break;
                  }
                }
              }
              if (seekable < 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
                conn.SendNow(HTTP_S.BuildResponse("412", "Fragment out of range"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ " << requestedTime / 10000 << "ms too old (" << Strm.metadata["keytime"][0u].asInt() << " - " << Strm.metadata["keytime"][Strm.metadata["keytime"].size() - 1].asInt() << " ms)" << std::endl;
                continue;
              }
              if (seekable > 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
                conn.SendNow(HTTP_S.BuildResponse("208", "Ask again later"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ " << requestedTime / 10000 << "ms not available yet (" << Strm.metadata["keytime"][0u].asInt() << " - " << Strm.metadata["keytime"][Strm.metadata["keytime"].size() - 1].asInt() << " ms)" << std::endl;
                continue;
              }
            }
            //Seek to the right place and send a play-once for a single fragment.
            std::stringstream sstream;
            sstream << "s " << (requestedTime / 10000) << "\no \n";
            ss.SendNow(sstream.str().c_str());
          }else{
            //We have a request for a Manifest, generate and send it.
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "text/xml");
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            std::string manifest = smoothIndex(Strm.metadata);
            HTTP_S.SetBody(manifest);
            conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
          }
          ready4data = true;
          //Clean for any possible next requests
          HTTP_R.Clean();
        }
      }else{
        //Wait 1 second before checking for new data.
        Util::sleep(1);
      }
      if (ready4data){
        unsigned int now = Util::epoch();
        if (now != lastStats){
          //Send new stats.
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Smooth").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              //Send the current buffer
              if (dataSize){
                HTTP_S.Clean();
                HTTP_S.SetHeader("Content-Type", "video/mp4");
                HTTP_S.SetBody("");

                unsigned int myDuration;
                
                //Wrap everything in mp4 boxes
                MP4::MFHD mfhd_box;
                for (int i = 0; i < Strm.metadata["keytime"].size(); i++){
                  if (Strm.metadata["keytime"][i].asInt() >= (requestedTime / 10000)){
                    mfhd_box.setSequenceNumber(Strm.metadata["keynum"][i].asInt());
                    myDuration = Strm.metadata["keylen"][i].asInt() * 10000;
                    break;
                  }
                }
                
                MP4::TFHD tfhd_box;
                tfhd_box.setFlags(MP4::tfhdSampleFlag);
                tfhd_box.setTrackID(1);
                tfhd_box.setDefaultSampleFlags(0x000000C0 | MP4::noIPicture | MP4::noDisposable | MP4::noKeySample);
                
                MP4::TRUN trun_box;
                trun_box.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleDuration | MP4::trunsampleSize);
                trun_box.setDataOffset(42);
                trun_box.setFirstSampleFlags(0x00000040 | MP4::isIPicture | MP4::noDisposable | MP4::isKeySample);
                for (int i = 0; i < dataBuffer.size(); i++){
                  MP4::trunSampleInformation trunSample;
                  trunSample.sampleSize = dataBuffer[i].size();
                  trunSample.sampleDuration = (((double)myDuration / dataBuffer.size()) * i) - (((double)myDuration / dataBuffer.size()) * (i - 1));
                  trun_box.setSampleInformation(trunSample, i);
                }
                
                MP4::SDTP sdtp_box;
                sdtp_box.setVersion(0);
                sdtp_box.setValue(0x24, 4);
                for (int i = 1; i < dataBuffer.size(); i++){
                  sdtp_box.setValue(0x14, 4 + i);
                }
                
                MP4::TRAF traf_box;
                traf_box.setContent(tfhd_box, 0);
                traf_box.setContent(trun_box, 1);
                traf_box.setContent(sdtp_box, 2);
                
                //If the stream is live, we want to have a fragref box if possible
                if (Strm.metadata.isMember("live")){
                  MP4::UUID_TrackFragmentReference fragref_box;
                  fragref_box.setVersion(1);
                  fragref_box.setFragmentCount(0);
                  int fragCount = 0;
                  for (int i = 0; i < Strm.metadata["keytime"].size(); i++){
                    if (Strm.metadata["keytime"][i].asInt() > (requestedTime / 10000)){
                      fragref_box.setTime(fragCount, Strm.metadata["keytime"][i].asInt() * 10000);
                      fragref_box.setDuration(fragCount, Strm.metadata["keylen"][i].asInt() * 10000);
                      fragref_box.setFragmentCount(++fragCount);
                    }
                  }
                  traf_box.setContent(fragref_box, 3);
                }

                MP4::MOOF moof_box;
                moof_box.setContent(mfhd_box, 0);
                moof_box.setContent(traf_box, 1);

                //Setting the correct offsets.
                trun_box.setDataOffset(moof_box.boxedSize() + 8);
                traf_box.setContent(trun_box, 1);
                moof_box.setContent(traf_box, 1);

                //Send the complete message
                HTTP_S.SetHeader("Content-Length", dataSize + 8 + moof_box.boxedSize());
                conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
                conn.SendNow(moof_box.asBox(), moof_box.boxedSize());

                unsigned long size = htonl(dataSize + 8);
                conn.SendNow((char*) &size, 4);
                conn.SendNow("mdat", 4);
                while (dataBuffer.size() > 0){
                  conn.SendNow(dataBuffer.front());
                  dataBuffer.pop_front();
                }
              }
              dataBuffer.clear();
              dataSize = 0;
            }
            if ((wantsAudio && Strm.lastType() == DTSC::AUDIO) || (wantsVideo && Strm.lastType() == DTSC::VIDEO)){
              //Select only the data that the client has requested.
              dataBuffer.push_back(Strm.lastData());
              dataSize += Strm.lastData().size();
            }
          }
        }
        if ( !ss.connected()){
          break;
        }
      }
    }
    conn.close();
    ss.SendNow(conn.getStats("HTTP_Smooth").c_str());
    ss.close();
    return 0;
  }//Smooth_Connector main function

}//Connector_HTTP namespace

///\brief The standard process-spawning main function.
int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  conf.addConnectorOptions(1935);
  conf.parseArgs(argc, argv);
  Socket::Server server_socket = Socket::Server("/tmp/mist/http_smooth");
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_HTTP::smoothConnector(S);
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
