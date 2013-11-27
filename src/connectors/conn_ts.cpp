/// \file conn_ts.cpp
/// Contains the main code for the TS Connector

#include <queue>
#include <string>
#include <iostream>

#include <cmath>
#include <ctime>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/types.h>

#include <mist/socket.h>
#include <mist/config.h>
#include <mist/stream.h>
#include <mist/ts_packet.h> //TS support
#include <mist/dtsc.h> //DTSC support
#include <mist/mp4.h> //For initdata conversion

///\brief Holds everything unique to the TS Connector
namespace Connector_TS {
  ///\brief Main function for the TS Connector
  ///\param conn A socket describing the connection the client.
  ///\param streamName The stream to connect to.
  ///\return The exit code of the connector.
  int tsConnector(Socket::Connection conn, std::string streamName, std::string trackIDs){
    std::string ToPack;
    TS::Packet PackData;
    std::string DTMIData;
    int PacketNumber = 0;
    long long unsigned int TimeStamp = 0;
    int ThisNaluSize;
    char VideoCounter = 0;
    char AudioCounter = 0;
    bool WritePesHeader;
    bool IsKeyFrame;
    bool FirstKeyFrame = true;
    bool FirstIDRInKeyFrame;
    MP4::AVCC avccbox;
    bool haveAvcc = false;

    DTSC::Stream Strm;
    bool inited = false;
    Socket::Connection ss;

    while (conn.connected()){
      if ( !inited){
        ss = Util::Stream::getStream(streamName);
        if ( !ss.connected()){
  #if DEBUG >= 1
          fprintf(stderr, "Could not connect to server!\n");
  #endif
          conn.close();
          break;
        }

        if(trackIDs == ""){
          std::stringstream tmpTracks;
          // no track ids given? Find the first video and first audio track (if available) and use those!
          int videoID = -1;
          int audioID = -1;

          Strm.waitForMeta(ss);
          for (std::map<int,DTSC::Track>::iterator it = Strm.metadata.tracks.begin(); it != Strm.metadata.tracks.end(); it){
            if (audioID == -1 && it->second.type == "audio"){
              audioID = it->first;
              tmpTracks << " " << it->first;
            }

            if (videoID == -1 && it->second.type == "video"){
              videoID = it->first;
              tmpTracks << " " << it->first;
            }

          }	// for iterator
          trackIDs += tmpTracks.str();
        } // if trackIDs == ""

        std::string cmd = "t " + trackIDs + "\ns 0\np\n";
        ss.SendNow( cmd );
        inited = true;
      }
      if (ss.spool()){
        while (Strm.parsePacket(ss.Received())){

          std::stringstream TSBuf;
          Socket::Buffer ToPack;
          //write PAT and PMT TS packets
          if (PacketNumber == 0){
            PackData.DefaultPAT();
            TSBuf.write(PackData.ToString(), 188);
            PackData.DefaultPMT();
            TSBuf.write(PackData.ToString(), 188);
            PacketNumber += 2;
          }

          int PIDno = 0;
          char * ContCounter = 0;
          if (Strm.lastType() == DTSC::VIDEO){
		      if ( !haveAvcc){
		          avccbox.setPayload(Strm.metadata.tracks[Strm.getPacket()["trackid"].asInt()].init);
		        haveAvcc = true;
		      }

            IsKeyFrame = Strm.getPacket().isMember("keyframe");
            if (IsKeyFrame){
              TimeStamp = (Strm.getPacket()["time"].asInt() * 27000);
            }
            ToPack.append(avccbox.asAnnexB());
                while (Strm.lastData().size() > 4){
              ThisNaluSize = (Strm.lastData()[0] << 24) + (Strm.lastData()[1] << 16) + (Strm.lastData()[2] << 8) + Strm.lastData()[3];
              Strm.lastData().replace(0, 4, TS::NalHeader, 4);
              if (ThisNaluSize + 4 == Strm.lastData().size()){
                ToPack.append(Strm.lastData());
                break;
              }else{
                ToPack.append(Strm.lastData().c_str(), ThisNaluSize + 4);
                Strm.lastData().erase(0, ThisNaluSize + 4);
              }
            }
            ToPack.prepend(TS::Packet::getPESVideoLeadIn(0ul, Strm.getPacket()["time"].asInt() * 90));
            PIDno = 0x100 - 1 + Strm.getPacket()["trackid"].asInt();
            ContCounter = &VideoCounter;
          }else if (Strm.lastType() == DTSC::AUDIO){
            ToPack.append(TS::GetAudioHeader(Strm.lastData().size(), Strm.metadata.tracks[Strm.getPacket()["trackid"].asInt()].init));
            ToPack.append(Strm.lastData());
            ToPack.prepend(TS::Packet::getPESAudioLeadIn(ToPack.bytes(1073741824ul), Strm.getPacket()["time"].asInt() * 90));
            PIDno = 0x100 - 1 + Strm.getPacket()["trackid"].asInt();
            ContCounter = &AudioCounter;
                IsKeyFrame = false;
          }

          //initial packet
          PackData.Clear();
          PackData.PID(PIDno);
          PackData.ContinuityCounter(( *ContCounter)++);
          PackData.UnitStart(1);
          if (IsKeyFrame){
            PackData.RandomAccess(1);
            PackData.PCR(TimeStamp);
          }
          unsigned int toSend = PackData.AddStuffing(ToPack.bytes(184));
          std::string gonnaSend = ToPack.remove(toSend);
          PackData.FillFree(gonnaSend);
          TSBuf.write(PackData.ToString(), 188);
          PacketNumber++;

          //rest of packets
          while (ToPack.size()){
            PackData.Clear();
            PackData.PID(PIDno);
            PackData.ContinuityCounter(( *ContCounter)++);
            toSend = PackData.AddStuffing(ToPack.bytes(184));
            gonnaSend = ToPack.remove(toSend);
            PackData.FillFree(gonnaSend);
            TSBuf.write(PackData.ToString(), 188);
            PacketNumber++;
          }

          TSBuf.flush();
          if (TSBuf.str().size()){
            conn.SendNow(TSBuf.str().c_str(), TSBuf.str().size());
            TSBuf.str("");
          }
          TSBuf.str("");
          PacketNumber = 0;
        }
      }else{
        Util::sleep(1000);
        conn.spool();
      }
    }
    return 0;
  }
}

int main(int argc, char ** argv){
  Util::Config conf(argv[0], PACKAGE_VERSION);
  JSON::Value capa;
  capa["desc"] = "Enables the raw MPEG Transport Stream protocol over TCP.";
  capa["deps"] = "";
  capa["required"]["streamname"]["name"] = "Stream";
  capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add this protocol multiple times using different ports.";
  capa["required"]["streamname"]["type"] = "str";
  capa["required"]["streamname"]["option"] = "--stream";
  capa["optional"]["tracks"]["name"] = "Tracks";
  capa["optional"]["tracks"]["help"] = "The track IDs of the stream that this connector will transmit separated by spaces";
  capa["optional"]["tracks"]["type"] = "str";
  capa["optional"]["tracks"]["option"] = "--tracks";
  conf.addOption("streamname",
      JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
  conf.addOption("tracks",
      JSON::fromString("{\"arg\":\"string\",\"value\":[\"\"],\"short\": \"t\",\"long\":\"tracks\",\"help\":\"The track IDs of the stream that this connector will transmit separated by spaces.\"}"));
  conf.addConnectorOptions(8888, capa);
  bool ret = conf.parseArgs(argc, argv);
  if (conf.getBool("json")){
    std::cout << capa.toString() << std::endl;
    return -1;
  }
  if (!ret){
    std::cerr << "Usage error: missing argument(s)." << std::endl;
    conf.printHelp(std::cout);
    return 1;
  }
  
  Socket::Server server_socket = Socket::Server(conf.getInteger("listen_port"), conf.getString("listen_interface"));
  if ( !server_socket.connected()){
    return 1;
  }
  conf.activate();

  while (server_socket.connected() && conf.is_active){
    Socket::Connection S = server_socket.accept();
    if (S.connected()){ //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0){ //if new child, start MAINHANDLER
        return Connector_TS::tsConnector(S, conf.getString("streamname"), conf.getString("tracks"));
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
