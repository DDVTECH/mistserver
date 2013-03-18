/// \file conn_http_dynamic.cpp
/// Contains the main code for the HTTP Dynamic Connector

#include <iostream>
#include <iomanip>
#include <queue>
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
#include <sstream>
#include <mist/stream.h>
#include <mist/timing.h>

/// Holds everything unique to HTTP Dynamic Connector.
namespace Connector_HTTP {
  /// Returns a Smooth-format manifest file
  std::string BuildManifest(std::string & MovieId, JSON::Value & metadata){
    std::stringstream Result;
    Result << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
    Result << "<SmoothStreamingMedia MajorVersion=\"2\" MinorVersion=\"0\" TimeScale=\"10000000\" ";
    if (metadata.isMember("vod")){
      Result << "Duration=\"" << metadata["lastms"].asInt() << "\"";
    }else{
      Result << "Duration=\"0\" IsLive=\"TRUE\" LookAheadFragmentCount=\"2\" DVRWindowLength=\"" + metadata["buffer_window"].asString() + "0000\" CanSeek=\"TRUE\" CanPause=\"TRUE\" ";
    }
    Result << ">\n";
    if (metadata.isMember("audio")){
      Result << "  <StreamIndex Type=\"audio\" QualityLevels=\"1\" Name=\"audio\" Chunks=\"" << metadata["keytime"].size()
          << "\" Url=\"Q({bitrate})/A({start time})\">\n";
      Result << "    <QualityLevel Index=\"0\" Bitrate=\"" << metadata["audio"]["bps"].asInt() * 8 << "\" CodecPrivateData=\"";
      Result << std::hex;
      for (int i = 0; i < metadata["audio"]["init"].asString().size(); i++){
        Result << std::setfill('0') << std::setw(2) << std::right << (int)metadata["audio"]["init"].asString()[i];
      }
      Result << std::dec;
      Result << "\" SamplingRate=\"" << metadata["audio"]["rate"].asInt()
          << "\" Channels=\"2\" BitsPerSample=\"16\" PacketSize=\"4\" AudioTag=\"255\" FourCC=\"AACL\"  />\n";
      for (int i = 0; i < metadata["keytime"].size() - 1; i++){
        Result << "    <c ";
        if (i == 0){
          Result << "t=\"" << metadata["keytime"][0u].asInt() * 10000 << "\" ";
        }
        Result << "d=\"" << 10000 * (metadata["keytime"][i + 1].asInt() - metadata["keytime"][i].asInt()) << "\" />\n";
      }
      Result << "    <c d=\"" << 10000 * (metadata["lastms"].asInt() - metadata["keytime"][metadata["keytime"].size() - 1].asInt()) << "\" />\n";
      Result << "   </StreamIndex>\n";
    }
    if (metadata.isMember("video")){
      Result << "  <StreamIndex Type=\"video\" QualityLevels=\"1\" Name=\"video\" Chunks=\"" << metadata["keytime"].size()
          << "\" Url=\"Q({bitrate})/V({start time})\" MaxWidth=\"" << metadata["video"]["width"].asInt() << "\" MaxHeight=\""
          << metadata["video"]["height"].asInt() << "\" DisplayWidth=\"" << metadata["video"]["width"].asInt() << "\" DisplayHeight=\""
          << metadata["video"]["height"].asInt() << "\">\n";
      Result << "    <QualityLevel Index=\"0\" Bitrate=\"" << metadata["video"]["bps"].asInt() * 8 << "\" CodecPrivateData=\"";
      MP4::AVCC avccbox;
      avccbox.setPayload(metadata["video"]["init"].asString());
      std::string tmpString = avccbox.asAnnexB();
      Result << std::hex;
      for (int i = 0; i < tmpString.size(); i++){
        Result << std::setfill('0') << std::setw(2) << std::right << (int)tmpString[i];
      }
      Result << std::dec;
      Result << "\" MaxWidth=\"" << metadata["video"]["width"].asInt() << "\" MaxHeight=\"" << metadata["video"]["height"].asInt()
          << "\" FourCC=\"AVC1\" />\n";
      for (int i = 0; i < metadata["keytime"].size() - 1; i++){
        Result << "    <c ";
        if (i == 0){
          Result << "t=\"" << metadata["keytime"][0u].asInt() * 10000 << "\" ";
        }
        Result << "d=\"" << 10000 * (metadata["keytime"][i + 1].asInt() - metadata["keytime"][i].asInt()) << "\" />\n";
      }
      Result << "    <c d=\"" << 10000 * (metadata["lastms"].asInt() - metadata["keytime"][metadata["keytime"].size() - 1].asInt()) << "\" />\n";
      Result << "   </StreamIndex>\n";
    }
    Result << "</SmoothStreamingMedia>\n";

#if DEBUG >= 8
    std::cerr << "Sending this manifest:" << std::endl << Result << std::endl;
#endif
    return Result.str();
  } //BuildManifest

  /// Main function for Connector_HTTP_Dynamic
  int Connector_HTTP_Dynamic(Socket::Connection conn){
    std::deque<std::string> FlashBuf;
    int FlashBufSize = 0;

    DTSC::Stream Strm; //Incoming stream buffer.
    HTTP::Parser HTTP_R, HTTP_S; //HTTP Receiver and HTTP Sender.

    bool ready4data = false; //Set to true when streaming is to begin.
    Socket::Connection ss( -1);
    std::string streamname;

    bool wantsVideo = false;
    bool wantsAudio = false;

    std::string Quality;
    long long int ReqFragment = -1;
    std::string tempStr;
    unsigned int lastStats = 0;
    conn.setBlocking(false); //do not block on conn.spool() when no data is available

    while (conn.connected()){
      if (conn.spool() || conn.Received().size()){
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
          streamname = HTTP_R.GetHeader("X-Stream");
          if ( !ss){
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
            //make sure metadata is received
            while ( !Strm.metadata){
              if (ss.spool()){
                while (Strm.parsePacket(ss.Received())){
                  //do nothing
                }
              }
            }
          }
          if (HTTP_R.url.find("Manifest") == std::string::npos){
            Quality = HTTP_R.url.substr(HTTP_R.url.find("/Q(", 8) + 3);
            Quality = Quality.substr(0, Quality.find(")"));
            tempStr = HTTP_R.url.substr(HTTP_R.url.find(")/") + 2);
            wantsAudio = false;
            wantsVideo = false;
            if (tempStr[0] == 'A'){
              wantsAudio = true;
            }
            if (tempStr[0] == 'V'){
              wantsVideo = true;
            }
            tempStr = tempStr.substr(tempStr.find("(") + 1);
            ReqFragment = atoll(tempStr.substr(0, tempStr.find(")")).c_str());
            if (Strm.metadata.isMember("live")){
              int seekable = Strm.canSeekms(ReqFragment / 10000);
              if (seekable < 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("The requested fragment is no longer kept in memory on the server and cannot be served.\n");
                conn.SendNow(HTTP_S.BuildResponse("412", "Fragment out of range"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ " << ReqFragment / 10000 << "ms too old (" << Strm.metadata["keytime"][0u].asInt() << " - " << Strm.metadata["keytime"][Strm.metadata["keytime"].size() - 1].asInt() << " ms)" << std::endl;
                continue;
              }
              if (seekable > 0){
                HTTP_S.Clean();
                HTTP_S.SetBody("Proxy, re-request this in a second or two.\n");
                conn.SendNow(HTTP_S.BuildResponse("208", "Ask again later"));
                HTTP_R.Clean(); //clean for any possible next requests
                std::cout << "Fragment @ " << ReqFragment / 10000 << "ms not available yet (" << Strm.metadata["keytime"][0u].asInt() << " - " << Strm.metadata["keytime"][Strm.metadata["keytime"].size() - 1].asInt() << " ms)" << std::endl;
                continue;
              }
            }
            std::stringstream sstream;
            sstream << "s " << (ReqFragment / 10000) << "\no \n";
            ss.SendNow(sstream.str().c_str());
          }else{
            HTTP_S.Clean();
            HTTP_S.SetHeader("Content-Type", "text/xml");
            HTTP_S.SetHeader("Cache-Control", "no-cache");
            std::string manifest = BuildManifest(streamname, Strm.metadata);
            HTTP_S.SetBody(manifest);
            conn.SendNow(HTTP_S.BuildResponse("200", "OK"));
          }
          ready4data = true;
          HTTP_R.Clean(); //clean for any possible next requests
        }
      }else{
        Util::sleep(1);
      }
      if (ready4data){
        unsigned int now = Util::epoch();
        if (now != lastStats){
          lastStats = now;
          ss.SendNow(conn.getStats("HTTP_Smooth").c_str());
        }
        if (ss.spool()){
          while (Strm.parsePacket(ss.Received())){
            if (Strm.lastType() == DTSC::PAUSEMARK){
              if (FlashBufSize){
                HTTP_S.Clean();
                HTTP_S.SetHeader("Content-Type", "video/mp4");
                HTTP_S.SetBody("");

                unsigned int myDuration;
                

                MP4::MFHD mfhd_box;
                for (int i = 0; i < Strm.metadata["keytime"].size(); i++){
                  if (Strm.metadata["keytime"][i].asInt() >= (ReqFragment / 10000)){
                    if (Strm.metadata.isMember("keynum")){
                      mfhd_box.setSequenceNumber(Strm.metadata["keynum"][i].asInt());
                    }else{
                      mfhd_box.setSequenceNumber(i + 1);
                    }
                    if (Strm.metadata.isMember("keylen")){
                      myDuration = Strm.metadata["keylen"][i].asInt() * 10000;
                    }else{
                      if (i != Strm.metadata["keytime"].size()){
                        myDuration = Strm.metadata["keytime"][i + 1].asInt() - Strm.metadata["keytime"][i].asInt();
                      }else{
                        myDuration = Strm.metadata["lastms"].asInt() - Strm.metadata["keytime"][i].asInt();
                      }
                      myDuration = myDuration * 10000;
                    }
                    break;
                  }
                }

                MP4::TFHD tfhd_box;
                tfhd_box.setFlags(MP4::tfhdSampleFlag);
                tfhd_box.setTrackID(1);
                tfhd_box.setDefaultSampleFlags(0x000000C0 | MP4::noIPicture | MP4::noDisposable | MP4::noKeySample);

                MP4::TRUN trun_box;
                //maybe reinsert dataOffset
                trun_box.setFlags(MP4::trundataOffset | MP4::trunfirstSampleFlags | MP4::trunsampleDuration | MP4::trunsampleSize);
                trun_box.setDataOffset(42);
                trun_box.setFirstSampleFlags(0x00000040 | MP4::isIPicture | MP4::noDisposable | MP4::isKeySample);
                for (int i = 0; i < FlashBuf.size(); i++){
                  MP4::trunSampleInformation trunSample;
                  trunSample.sampleSize = FlashBuf[i].size();
                  trunSample.sampleDuration = (((double)myDuration / FlashBuf.size()) * i) - (((double)myDuration / FlashBuf.size()) * (i - 1));
                  trun_box.setSampleInformation(trunSample, i);
                }
                MP4::SDTP sdtp_box;
                sdtp_box.setVersion(0);
                sdtp_box.setValue(0x24, 4);
                for (int i = 1; i < FlashBuf.size(); i++){
                  sdtp_box.setValue(0x14, 4 + i);
                }
                
                MP4::TRAF traf_box;
                traf_box.setContent(tfhd_box, 0);
                traf_box.setContent(trun_box, 1);
                traf_box.setContent(sdtp_box, 2);
                
                // if live, send fragref box if we can
                if (Strm.metadata.isMember("live")){
                  MP4::UUID_TrackFragmentReference fragref_box;
                  fragref_box.setVersion(1);
                  fragref_box.setFragmentCount(0);
                  int fragCount = 0;
                  for (int i = 0; i < Strm.metadata["keytime"].size(); i++){
                    if (Strm.metadata["keytime"][i].asInt() > (ReqFragment / 10000)){
                      fragref_box.setTime(fragCount, Strm.metadata["keytime"][i].asInt() * 10000);
                      fragref_box.setDuration(fragCount, Strm.metadata["keylen"][i].asInt() * 10000);
                      fragCount++;
                      fragref_box.setFragmentCount(fragCount);
                    }
                  }
                  traf_box.setContent(fragref_box, 3);
                }

                MP4::MOOF moof_box;
                moof_box.setContent(mfhd_box, 0);
                moof_box.setContent(traf_box, 1);

                //setting tha offsets!
                trun_box.setDataOffset(moof_box.boxedSize() + 8);
                traf_box.setContent(trun_box, 1);
                moof_box.setContent(traf_box, 1);

                //std::cerr << "\t[encoded] = " << ((MP4::TRUN&)(((MP4::TRAF&)(moof_box.getContent(1))).getContent(1))).getDataOffset() << std::endl;

                HTTP_S.SetHeader("Content-Length", FlashBufSize + 8 + moof_box.boxedSize()); //32+33+btstrp.size());
                conn.SendNow(HTTP_S.BuildResponse("200", "OK"));

                conn.SendNow(moof_box.asBox(), moof_box.boxedSize());

                unsigned long size = htonl(FlashBufSize+8);
                conn.SendNow((char*) &size, 4);
                conn.SendNow("mdat", 4);
                while (FlashBuf.size() > 0){
                  conn.SendNow(FlashBuf.front());
                  FlashBuf.pop_front();
                }
              }
              FlashBuf.clear();
              FlashBufSize = 0;
            }
            if ((wantsAudio && Strm.lastType() == DTSC::AUDIO) || (wantsVideo && Strm.lastType() == DTSC::VIDEO)){
              FlashBuf.push_back(Strm.lastData());
              FlashBufSize += Strm.lastData().size();
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
  } //Connector_HTTP_Smooth main function

} //Connector_HTTP_Smooth namespace

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
        return Connector_HTTP::Connector_HTTP_Dynamic(S);
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
