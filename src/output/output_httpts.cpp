#include "output_httpts.h"
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <unistd.h>

namespace Mist {
  OutHTTPTS::OutHTTPTS(Socket::Connection & conn) : HTTPOutput(conn) {
    haveAvcc = false;
    myConn.setBlocking(true);
  }
  
  OutHTTPTS::~OutHTTPTS() {}

  void OutHTTPTS::init(Util::Config * cfg){
    HTTPOutput::init(cfg);
    capa["name"] = "HTTPTS";
    capa["desc"] = "Enables HTTP protocol MPEG2/TS pseudostreaming.";
    capa["url_rel"] = "/$.ts";
    capa["url_match"] = "/$.ts";
    capa["socket"] = "http_ts";
    capa["codecs"][0u][0u].append("H264");
    capa["codecs"][0u][1u].append("AAC");
    capa["codecs"][0u][1u].append("MP3");
    capa["methods"][0u]["handler"] = "http";
    capa["methods"][0u]["type"] = "html5/video/mp2t";
    capa["methods"][0u]["priority"] = 1ll;
  }
  
  ///this function generates the PMT packet
  std::string OutHTTPTS::createPMT(){
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
    return PMT.getStrBuf();
  }
  
  void OutHTTPTS::fillPacket(bool & first, const char * data, size_t dataLen, char & ContCounter){
    if (!PackData.BytesFree()){
      if (PacketNumber % 42 == 0){
        H.Chunkify(TS::PAT, 188, myConn);
        std::string PMT = createPMT();
        H.Chunkify(PMT, myConn);
        PacketNumber += 2;
      }
      H.Chunkify(PackData.ToString(), 188, myConn);
      PacketNumber ++;
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
    int tmp = PackData.FillFree(data, dataLen);
    if (tmp != dataLen){
      fillPacket(first, data+tmp, dataLen-tmp, ContCounter);
    }
  }
  
  void OutHTTPTS::sendNext(){
    bool first = true;
    char * ContCounter = 0;
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen); //data
    
    std::string bs;
    //prepare bufferstring
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
      bs = TS::Packet::getPESVideoLeadIn(0ul, currentPacket.getTime() * 90);
      fillPacket(first, bs.data(), bs.size(), VideoCounter);
      
      if (currentPacket.getInt("keyframe")){
        if (!haveAvcc){
          avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
          haveAvcc = true;
        }
        bs = avccbox.asAnnexB();
        fillPacket(first, bs.data(), bs.size(), VideoCounter);
      }
      
      unsigned int i = 0;
      while (i + 4 < (unsigned int)dataLen){
        unsigned int ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
        if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
          DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
          break;
        }
        fillPacket(first, "\000\000\000\001",4, VideoCounter);
        fillPacket(first, dataPointer+i+4,ThisNaluSize, VideoCounter);
        i += ThisNaluSize+4;
      }
      if (PackData.BytesFree() < 184){
        PackData.AddStuffing();
        fillPacket(first, 0, 0, VideoCounter);
      }
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
      bs = TS::Packet::getPESAudioLeadIn(7+dataLen, currentPacket.getTime() * 90);
      fillPacket(first, bs.data(), bs.size(), AudioCounter);
      bs = TS::GetAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init);
      fillPacket(first, bs.data(), bs.size(), AudioCounter);
      ContCounter = &AudioCounter;
      fillPacket(first, dataPointer,dataLen, AudioCounter);
      if (PackData.BytesFree() < 184){
        PackData.AddStuffing();
        fillPacket(first, 0, 0, AudioCounter);
      }
    }
  }

  void OutHTTPTS::onHTTP(){

    initialize();
    H.Clean();
    H.SetHeader("Content-Type", "video/mp2t");
    H.StartResponse(H, myConn);
    PacketNumber = 0;
    parseData = true;
    wantRequest = false;
    H.Clean(); //clean for any possible next requests
  }
}
