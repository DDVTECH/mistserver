#include "output_ts_base.h"

namespace Mist {
  TSOutput::TSOutput(Socket::Connection & conn) : TS_BASECLASS(conn){
    packCounter=0;
    haveAvcc = false;
    ts_from = 0;
    until=0xFFFFFFFFFFFFFFFFull;
    setBlocking(true);
    sendRepeatingHeaders = false;
    appleCompat=false;
  }

  void TSOutput::fillPacket(char const * data, size_t dataLen, bool & firstPack, bool video, bool keyframe, uint32_t pkgPid, int & contPkg){
    do {
      if (!packData.getBytesFree()){
        if ( (sendRepeatingHeaders && packCounter % 42 == 0) || !packCounter){
          TS::Packet tmpPack;
          tmpPack.FromPointer(TS::PAT);
          tmpPack.setContinuityCounter(++contPAT);
          sendTS(tmpPack.checkAndGetBuffer());
          sendTS(TS::createPMT(selectedTracks, myMeta, ++contPMT));
          packCounter += 2;
        }
        sendTS(packData.checkAndGetBuffer());
        packCounter ++;
        packData.clear();
      }
      
      if (!dataLen){return;}
      
      if (packData.getBytesFree() == 184){
        packData.clear();      
        packData.setPID(pkgPid);
        packData.setContinuityCounter(++contPkg);
        if (firstPack){
          packData.setUnitStart(1);
          packData.setDiscontinuity(true);
          if (video){
            if (keyframe){
              packData.setRandomAccess(true);
              packData.setESPriority(true);
            }      
            packData.setPCR(thisPacket.getTime() * 27000);      
          }
          firstPack = false;
        }
      }
      
      int tmp = packData.fillFree(data, dataLen);
      data += tmp;
      dataLen -= tmp;
    } while(dataLen);
  }

  void TSOutput::sendNext(){
    //Get ready some data to speed up accesses
    uint32_t trackId = thisPacket.getTrackId();
    DTSC::Track & Trk = myMeta.tracks[trackId];
    bool & firstPack = first[trackId];
    uint32_t pkgPid = 255 + trackId;
    int & contPkg = contCounters[pkgPid];
    uint64_t packTime = thisPacket.getTime();
    bool video = (Trk.type == "video");
    bool keyframe = thisPacket.getInt("keyframe");
    firstPack = true;

    char * dataPointer = 0;
    unsigned int dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen); //data
    if (packTime >= until){ //this if should only trigger for HLS       
      stop();
      wantRequest = true;
      parseData = false;
      sendTS("",0);      
      return;
    }
    //apple compatibility timestamp correction
    if (appleCompat){
      packTime -= ts_from;
      if (Trk.type == "audio"){
        packTime = 0;
      }
    }
    packTime *= 90;
    std::string bs;
    //prepare bufferstring    
    if (video){
      unsigned int extraSize = 0;      
      //dataPointer[4] & 0x1f is used to check if this should be done later: fillPacket("\000\000\000\001\011\360", 6);
      if (Trk.codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
        extraSize += 6;
      }
      if (keyframe){
        if (Trk.codec == "H264"){
          if (!haveAvcc){
            avccbox.setPayload(Trk.init);
            haveAvcc = true;
          }
          bs = avccbox.asAnnexB();
          extraSize += bs.size();
        }
      }
      
      unsigned int watKunnenWeIn1Ding = 65490-13;
      unsigned int splitCount = (dataLen+extraSize) / watKunnenWeIn1Ding;
      unsigned int currPack = 0;
      unsigned int ThisNaluSize = 0;
      unsigned int i = 0;
      unsigned int nalLead = 0;
      uint64_t offset = thisPacket.getInt("offset") * 90;

      while (currPack <= splitCount){
        unsigned int alreadySent = 0;
        bs = TS::Packet::getPESVideoLeadIn((currPack != splitCount ? watKunnenWeIn1Ding : dataLen+extraSize - currPack*watKunnenWeIn1Ding), packTime, offset, !currPack);
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
        if (!currPack){
          if (Trk.codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
            //End of previous nal unit, if not already present
            fillPacket("\000\000\000\001\011\360", 6, firstPack, video, keyframe, pkgPid, contPkg);
            alreadySent += 6;
          }
          if (keyframe){
            if (Trk.codec == "H264"){
              bs = avccbox.asAnnexB();
              fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
              alreadySent += bs.size();
            }
          }
        }
        while (i + 4 < (unsigned int)dataLen){
          if (nalLead){
            fillPacket("\000\000\000\001"+4-nalLead,nalLead, firstPack, video, keyframe, pkgPid, contPkg);
            i += nalLead;
            alreadySent += nalLead;
            nalLead = 0;
          }
          if (!ThisNaluSize){
            ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
            if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
              DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
              break;
            }
            if (alreadySent + 4 > watKunnenWeIn1Ding){
              nalLead = 4 - (watKunnenWeIn1Ding-alreadySent);
              fillPacket("\000\000\000\001",watKunnenWeIn1Ding-alreadySent, firstPack, video, keyframe, pkgPid, contPkg);
              i += watKunnenWeIn1Ding-alreadySent;
              alreadySent += watKunnenWeIn1Ding-alreadySent;
            }else{
              fillPacket("\000\000\000\001",4, firstPack, video, keyframe, pkgPid, contPkg);
              alreadySent += 4;
              i += 4;
            }
          }
          if (alreadySent + ThisNaluSize > watKunnenWeIn1Ding){
            fillPacket(dataPointer+i,watKunnenWeIn1Ding-alreadySent, firstPack, video, keyframe, pkgPid, contPkg);
            i += watKunnenWeIn1Ding-alreadySent;
            ThisNaluSize -= watKunnenWeIn1Ding-alreadySent;
            alreadySent += watKunnenWeIn1Ding-alreadySent;
          }else{
            fillPacket(dataPointer+i,ThisNaluSize, firstPack, video, keyframe, pkgPid, contPkg);
            alreadySent += ThisNaluSize;
            i += ThisNaluSize;
            ThisNaluSize = 0;
          }          
          if (alreadySent == watKunnenWeIn1Ding){
            packData.addStuffing();
            fillPacket(0, 0, firstPack, video, keyframe, pkgPid, contPkg);
            firstPack = true;
            break;
          }
        }
        currPack++;
      }
    }else if (Trk.type == "audio"){
      long unsigned int tempLen = dataLen;
      if (Trk.codec == "AAC"){
        tempLen += 7;
      }
      bs = TS::Packet::getPESAudioLeadIn(tempLen, packTime);// myMeta.tracks[thisPacket.getTrackId()].rate / 1000 );
      fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      if (Trk.codec == "AAC"){        
        bs = TS::getAudioHeader(dataLen, Trk.init);      
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      }
      fillPacket(dataPointer,dataLen, firstPack, video, keyframe, pkgPid, contPkg);
    }
    if (packData.getBytesFree() < 184){
      packData.addStuffing();
      fillPacket(0, 0, firstPack, video, keyframe, pkgPid, contPkg);
    }
  }
}
