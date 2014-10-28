#include "output_ts_push.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTSPush::OutTSPush(Socket::Connection & conn) : Output(conn){
    haveAvcc = false;
    haveHvcc = false;
    AudioCounter = 0;
    VideoCounter = 0;
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
    initialize();
    std::string tracks = config->getString("tracks");
    unsigned int currTrack = 0;
    //loop over tracks, add any found track IDs to selectedTracks
    if (tracks != ""){
      selectedTracks.clear();
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

    //For udp pushing, 7 ts packets a time
    packetBuffer.reserve(config->getInteger("udpsize") * 188);
    std::string host = config->getString("destination");
    if (host.substr(0, 6) == "udp://"){
      host = host.substr(6);
    }
    int port = atoi(host.substr(host.find(":") + 1).c_str());
    host = host.substr(0, host.find(":"));
    pushSock.SetDestination(host, port);
  }
  
  OutTSPush::~OutTSPush() {}
  
  void OutTSPush::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "TSPush";
    capa["desc"] = "Push raw MPEG/TS over a TCP or UDP socket.";
    capa["deps"] = "";
    capa["required"]["streamname"]["name"] = "Stream";
    capa["required"]["streamname"]["help"] = "What streamname to serve. For multiple streams, add this protocol multiple times using different ports.";
    capa["required"]["streamname"]["type"] = "str";
    capa["required"]["streamname"]["option"] = "--stream";
    capa["required"]["destination"]["name"] = "Destination";
    capa["required"]["destination"]["help"] = "Where to push to, in the format protocol://hostname:port. Ie: udp://127.0.0.1:9876";
    capa["required"]["destination"]["type"] = "str";
    capa["required"]["destination"]["option"] = "--destination";
    capa["required"]["udpsize"]["name"] = "UDP Size";
    capa["required"]["udpsize"]["help"] = "The number of TS packets to push in a single UDP datagram";
    capa["required"]["udpsize"]["type"] = "uint";
    capa["required"]["udpsize"]["default"] = 5;
    capa["required"]["udpsize"]["option"] = "--udpsize";
    capa["optional"]["tracks"]["name"] = "Tracks";
    capa["optional"]["tracks"]["help"] = "The track IDs of the stream that this connector will transmit separated by spaces";
    capa["optional"]["tracks"]["type"] = "str";
    capa["optional"]["tracks"]["option"] = "--tracks";
    capa["codecs"][0u][0u].append("HEVC");
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    cfg->addBasicConnectorOptions(capa);
    cfg->addOption("streamname",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("destination",
                   JSON::fromString("{\"arg\":\"string\",\"short\":\"D\",\"long\":\"destination\",\"help\":\"Where to push to, in the format protocol://hostname:port. Ie: udp://127.0.0.1:9876\"}"));
    cfg->addOption("tracks",
                   JSON::fromString("{\"arg\":\"string\",\"value\":[\"\"],\"short\": \"t\",\"long\":\"tracks\",\"help\":\"The track IDs of the stream that this connector will transmit separated by spaces.\"}"));
    cfg->addOption("udpsize",
                   JSON::fromString("{\"arg\":\"integer\",\"value\":5,\"short\": \"u\",\"long\":\"udpsize\",\"help\":\"The number of TS packets to push in a single UDP datagram.\"}"));
    config = cfg;
  }
  
  void OutTSPush::fillBuffer(const char * data, size_t dataLen){
    static int curFilled = 0;
    if (curFilled == config->getInteger("udpsize")){
      pushSock.SendNow(packetBuffer);
      packetBuffer.clear();
      packetBuffer.reserve(config->getInteger("udpsize") * 188);
      curFilled = 0;
    }
    packetBuffer += std::string(data, 188);
    curFilled ++;
  }

  void OutTSPush::fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter){
    if (!PackData.BytesFree()){
      fillBuffer(PackData.getBuffer(), 188);
      PackData.Clear();
    }
    
    if (!dataLen){return;}
    
    if (PackData.BytesFree() == 184){
      PackData.PID(0x100 - 1 + currentPacket.getTrackId());
      PackData.ContinuityCounter(ContCounter++);
      if (first){
        PackData.UnitStart(1);
        if (currentPacket.getInt("keyframe")){
          PackData.RandomAccess(1);
          PackData.PCR(currentPacket.getTime() * 27000);
        }
        first = false;
      }
    }
    
    unsigned int tmp = PackData.FillFree(data, dataLen);
    if (tmp != dataLen){
      fillPacket(first, data+tmp, dataLen-tmp, ContCounter);
    }
 
  }
  
  
  void OutTSPush::sendNext(){
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen); //data
    
    bool first = true;    
    std::string bs;    
    //prepare bufferstring
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
      bs = TS::Packet::getPESVideoLeadIn(0ul, currentPacket.getTime() * 90);
      fillPacket(first, bs.data(), bs.size(),VideoCounter);
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264"){
        //End of previous nal unit, somehow needed for h264
        bs = "\000\000\000\001\011\360";
        fillPacket(first, bs.data(), bs.size(),VideoCounter);
      }
      
      if (currentPacket.getInt("keyframe")){
        if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264"){
          if (!haveAvcc){
            avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
            haveAvcc = true;
          }
          bs = avccbox.asAnnexB();
          fillPacket(first, bs.data(), bs.size(),VideoCounter);
        }
        if (myMeta.tracks[currentPacket.getTrackId()].codec == "HEVC"){
          if (!haveHvcc){
            hvccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
            haveHvcc = true;
          }
          bs = hvccbox.asAnnexB();
          fillPacket(first, bs.data(), bs.size(),VideoCounter);
        }
      }
      unsigned int i = 0;
      while (i + 4 < (unsigned int)dataLen){
        unsigned int ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
        if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
          DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
          break;
        }
        fillPacket(first, "\000\000\000\001",4,VideoCounter);
        fillPacket(first, dataPointer+i+4,ThisNaluSize,VideoCounter);      
        i += ThisNaluSize+4;
      }
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){      
      bs = TS::Packet::getPESAudioLeadIn(7+dataLen, currentPacket.getTime() * 90);
      fillPacket(first, bs.data(), bs.size(),AudioCounter);
      bs = TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init);      
      fillPacket(first, bs.data(), bs.size(),AudioCounter);
      fillPacket(first, dataPointer,dataLen,AudioCounter);
    }
    
    if (PackData.BytesFree() < 184){
      PackData.AddStuffing();
      fillPacket(first, 0,0,VideoCounter);
    }
  }

  ///this function generates the PMT packet
  std::string OutTSPush::createPMT(){
    TS::ProgramMappingTable PMT;
    PMT.PID(4096);
    PMT.setTableId(2);
    PMT.setSectionLength(0xB017);
    PMT.setProgramNumber(1);
    PMT.setVersionNumber(0);
    PMT.setCurrentNextIndicator(0);
    PMT.setSectionNumber(0);
    PMT.setLastSectionNumber(0);
    PMT.setPCRPID(0x100 + (*(selectedTracks.begin())) - 1);
    PMT.setProgramInfoLength(0);
    short id = 0;
    //for all selected tracks
    for (std::set<long unsigned int>::iterator it = selectedTracks.begin(); it != selectedTracks.end(); it++){
      if (myMeta.tracks[*it].codec == "H264"){
        PMT.setStreamType(0x1B,id);
      }else if (myMeta.tracks[*it].codec == "HEVC"){
        PMT.setStreamType(0x06,id);
      }else if (myMeta.tracks[*it].codec == "AAC"){
        PMT.setStreamType(0x0F,id);
      }else if (myMeta.tracks[*it].codec == "MP3"){
        PMT.setStreamType(0x03,id);
      }
      PMT.setElementaryPID(0x100 + (*it) - 1, id);
      PMT.setESInfoLength(0,id);
      id++;
    }
    PMT.calcCRC();
    return PMT.getStrBuf();
  }

  void OutTSPush::sendHeader(){
    fillBuffer(TS::PAT, 188);
    std::string pmt = createPMT();
    fillBuffer(pmt.data(), 188);
    sentHeader = true;
  }

}
