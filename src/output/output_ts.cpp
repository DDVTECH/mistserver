#include "output_ts.h"
#include <mist/http_parser.h>
#include <mist/defines.h>

namespace Mist {
  OutTS::OutTS(Socket::Connection & conn) : Output(conn){
    haveAvcc = false;
    AudioCounter = 0;
    VideoCounter = 0;
    std::string tracks = config->getString("tracks");
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
    capa["codecs"][0u][1u].append("MP3");
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
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "AAC"){
        ToPack.append(TS::Packet::getPESAudioLeadIn(7+dataLen, currentPacket.getTime() * 90));
        ToPack.append(TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init));
      }else{
        ToPack.append(TS::Packet::getPESAudioLeadIn(dataLen, currentPacket.getTime() * 90));
      }
      ToPack.append(dataPointer, dataLen);
      ContCounter = &AudioCounter;
    }
    
    bool first = true;
    //send TS packets
    while (ToPack.size()){
      PackData.Clear();
      PackData.PID(0x100 - 1 + currentPacket.getTrackId());
      PackData.ContinuityCounter((*ContCounter)++);
      if (first){
        PackData.UnitStart(1);
        if (IsKeyFrame || myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
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

  ///this function generates the PMT packet
  std::string OutTS::createPMT(){
    //0x02 = table ID = 2 = PMT
    //0xB017 = section syntax(1) = 1, 0(1)=0, reserved(2) = 3, section_len(12) = 23
    //0x0001 = ProgNo = 1
    //0xC1 = reserved(2) = 3, version(5)=0, curr_next_indi(1) = 1
    //0x00 = section_number = 0
    //0x00 = last_section_no = 0
    //0xE100 = reserved(3) = 7, PCR_PID(13) = 0x100
    //0xF000 = reserved(4) = 15, proginfolen = 0
    //0x1B = streamtype = 27 = H264
    //0xE100 = reserved(3) = 7, elem_ID(13) = 0x100
    //0xF000 = reserved(4) = 15, es_info_len = 0
    //0x0F = streamtype = 15 = audio with ADTS transport syntax
    //0xE101 = reserved(3) = 7, elem_ID(13) = 0x101
    //0xF000 = reserved(4) = 15, es_info_len = 0
    //0x2F44B99B = CRC32
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
    INFO_MSG("stringsize: %d %s", PMT.getStrBuf().size(), PMT.toPrettyString(0).c_str());
    return PMT.getStrBuf();
  }

  void OutTS::sendHeader(){
    /// \todo if --tracks is set, clear selected tracks and fill with --tracks tracks.
    myConn.SendNow(TS::PAT, 188);
    /// \todo Find a nice way to not use strings in this case
    std::string pmt = createPMT();
    myConn.SendNow(pmt.c_str(), 188);
    //myConn.SendNow(TS::PMT, 188);
    sentHeader = true;
  }

}
