#include "output_ts.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTS::OutTS(Socket::Connection & conn) : Output(conn){
    haveAvcc = false;
    AudioCounter = 0;
    VideoCounter = 0;
    std::string tracks = config->getString("tracks");
    unsigned int currTrack = 0;
    //loop over tracks, add any found track IDs to selectedTracks
    if (tracks != ""){
      for (unsigned int i = 0; i < tracks.size(); ++i){
        if (tracks[i] >= '0' && tracks[i] <= '9'){
          currTrack = currTrack*10 + (tracks[i] - '0');
        }else{
          if (currTrack > 0){
            selectedTracks.insert(currTrack);
          }
          currTrack = 0;
        }
      }
      if (currTrack > 0){
        selectedTracks.insert(currTrack);
      }
    }
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
    initialize();
  }
  
  OutTS::~OutTS() {}
  
  void OutTS::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "TS";
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
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    cfg->addOption("streamname",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("tracks",
                   JSON::fromString("{\"arg\":\"string\",\"value\":[\"\"],\"short\": \"t\",\"long\":\"tracks\",\"help\":\"The track IDs of the stream that this connector will transmit separated by spaces.\"}"));
    cfg->addConnectorOptions(8888, capa);
    config = cfg;
  }
  
  void OutTS::sendNext(){
    Socket::Buffer ToPack;
    char * ContCounter = 0;
    bool IsKeyFrame = false;
    
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen);
    
    //detect packet type, and put converted data into ToPack.
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
      ToPack.append(TS::Packet::getPESVideoLeadIn(0ul, currentPacket.getTime() * 90));
      
      IsKeyFrame = currentPacket.getInt("keyframe");
      if (IsKeyFrame){
        if (!haveAvcc){
          avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
          haveAvcc = true;
        }
        ToPack.append(avccbox.asAnnexB());
      }
      unsigned int i = 0;
      while (i + 4 < (unsigned int)dataLen){
        unsigned int ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
        if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
          DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
          break;
        }
        ToPack.append("\000\000\000\001", 4);
        i += 4;
        ToPack.append(dataPointer + i, ThisNaluSize);
        i += ThisNaluSize;
      }
      ContCounter = &VideoCounter;
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
      ToPack.append(TS::Packet::getPESAudioLeadIn(7+dataLen, currentPacket.getTime() * 90));
      ToPack.append(TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init));
      ToPack.append(dataPointer, dataLen);
      ContCounter = &AudioCounter;
    }
    
    bool first = true;
    //send TS packets
    while (ToPack.size()){
      PackData.Clear();
      /// \todo Update according to sendHeader()'s generated data.
      //0x100 - 1 + currentPacket.getTrackId()
      if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
        PackData.PID(0x100);
      }else{
        PackData.PID(0x101);
      }
      PackData.ContinuityCounter((*ContCounter)++);
      if (first){
        PackData.UnitStart(1);
        if (IsKeyFrame){
          PackData.RandomAccess(1);
          PackData.PCR(currentPacket.getTime() * 27000);
        }
        first = false;
      }
      unsigned int toSend = PackData.AddStuffing(ToPack.bytes(184));
      std::string gonnaSend = ToPack.remove(toSend);
      PackData.FillFree(gonnaSend);
      myConn.SendNow(PackData.ToString(), 188);
    }
  }

  void OutTS::sendHeader(){
    /// \todo Update this to actually generate these from the selected tracks.
    /// \todo ts_packet.h contains all neccesary info for this
    myConn.SendNow(TS::PAT, 188);
    myConn.SendNow(TS::PMT, 188);
    sentHeader = true;
  }

}
