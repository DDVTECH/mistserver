#include "output_ts_base.h"

namespace Mist {
  TSOutput::TSOutput(Socket::Connection & conn) : TS_BASECLASS(conn){
    packCounter=0;
    haveAvcc = false;
    haveHvcc = false;
    until=0xFFFFFFFFFFFFFFFFull;
    setBlocking(true);
    sendRepeatingHeaders = false;
    appleCompat=false;
  }

  void TSOutput::fillPacket(const char * data, const size_t dataLen){
    
    if (!packData.getBytesFree()){
      ///\todo only resend the PAT/PMT for HLS
      if ( (sendRepeatingHeaders && packCounter % 42 == 0) || !packCounter){
        TS::Packet tmpPack;
        tmpPack.FromPointer(TS::PAT);
        tmpPack.setContinuityCounter(++contCounters[0]);
        sendTS(tmpPack.checkAndGetBuffer());
        sendTS(TS::createPMT(selectedTracks, myMeta, ++contCounters[4096]));
        packCounter += 2;
      }
      sendTS(packData.checkAndGetBuffer());
      packCounter ++;
      packData.clear();
    }
    
    if (!dataLen){return;}
    
    if (packData.getBytesFree() == 184){
      packData.clear();      
      packData.setPID(0x100 - 1 + thisPacket.getTrackId());      
      packData.setContinuityCounter(++contCounters[packData.getPID()]);
      if (first[thisPacket.getTrackId()]){
        packData.setUnitStart(1);
        packData.setDiscontinuity(true);
        if (myMeta.tracks[thisPacket.getTrackId()].type == "video"){
          if (thisPacket.getInt("keyframe")){
            packData.setRandomAccess(1);
          }      
          packData.setPCR(thisPacket.getTime() * 27000);      
        }
        first[thisPacket.getTrackId()] = false;
      }
    }
    
    int tmp = packData.fillFree(data, dataLen);     
    if (tmp != dataLen){
      return fillPacket(data+tmp, dataLen-tmp);
    }
  }

  void TSOutput::sendNext(){
    first[thisPacket.getTrackId()] = true;
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen); //data
    if (thisPacket.getTime() >= until){ //this if should only trigger for HLS       
      stop();
      wantRequest = true;
      parseData = false;
      sendTS("",0);      
      return;
    }
    std::string bs;
    //prepare bufferstring    
    if (myMeta.tracks[thisPacket.getTrackId()].type == "video"){      
      unsigned int extraSize = 0;      
      //dataPointer[4] & 0x1f is used to check if this should be done later: fillPacket("\000\000\000\001\011\360", 6);
      if (myMeta.tracks[thisPacket.getTrackId()].codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
        extraSize += 6;
      }
      if (thisPacket.getInt("keyframe")){
        if (myMeta.tracks[thisPacket.getTrackId()].codec == "H264"){
          if (!haveAvcc){
            avccbox.setPayload(myMeta.tracks[thisPacket.getTrackId()].init);
            haveAvcc = true;
          }
          bs = avccbox.asAnnexB();
          extraSize += bs.size();
        }
        /*LTS-START*/
        if (myMeta.tracks[thisPacket.getTrackId()].codec == "HEVC"){
          if (!haveHvcc){
            hvccbox.setPayload(myMeta.tracks[thisPacket.getTrackId()].init);
            haveHvcc = true;
          }
          bs = hvccbox.asAnnexB();
          extraSize += bs.size();
        }
        /*LTS-END*/
      }
      
      unsigned int watKunnenWeIn1Ding = 65490-13;
      unsigned int splitCount = (dataLen+extraSize) / watKunnenWeIn1Ding;
      unsigned int currPack = 0;
      unsigned int ThisNaluSize = 0;
      unsigned int i = 0;
      unsigned int nalLead = 0;
      
      while (currPack <= splitCount){
        unsigned int alreadySent = 0;
        bs = TS::Packet::getPESVideoLeadIn((currPack != splitCount ? watKunnenWeIn1Ding : dataLen+extraSize - currPack*watKunnenWeIn1Ding), thisPacket.getTime() * 90, thisPacket.getInt("offset") * 90, !currPack);
        fillPacket(bs.data(), bs.size());
        if (!currPack){
          if (myMeta.tracks[thisPacket.getTrackId()].codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
            //End of previous nal unit, if not already present
            fillPacket("\000\000\000\001\011\360", 6);
            alreadySent += 6;
          }
          if (thisPacket.getInt("keyframe")){
            if (myMeta.tracks[thisPacket.getTrackId()].codec == "H264"){
              bs = avccbox.asAnnexB();
              fillPacket(bs.data(), bs.size());
              alreadySent += bs.size();
            }
            /*LTS-START*/
            if (myMeta.tracks[thisPacket.getTrackId()].codec == "HEVC"){
              bs = hvccbox.asAnnexB();
              fillPacket(bs.data(), bs.size());
              alreadySent += bs.size();
            }
            /*LTS-END*/
          }
        }
        while (i + 4 < (unsigned int)dataLen){
          if (nalLead){
            fillPacket("\000\000\000\001"+4-nalLead,nalLead);
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
              nalLead = 4 - watKunnenWeIn1Ding-alreadySent;
              fillPacket("\000\000\000\001",watKunnenWeIn1Ding-alreadySent);
              i += watKunnenWeIn1Ding-alreadySent;
              alreadySent += watKunnenWeIn1Ding-alreadySent;
            }else{
              fillPacket("\000\000\000\001",4);
              alreadySent += 4;
              i += 4;
            }
          }
          if (alreadySent + ThisNaluSize > watKunnenWeIn1Ding){
            fillPacket(dataPointer+i,watKunnenWeIn1Ding-alreadySent);
            i += watKunnenWeIn1Ding-alreadySent;
            ThisNaluSize -= watKunnenWeIn1Ding-alreadySent;
            alreadySent += watKunnenWeIn1Ding-alreadySent;
          }else{
            fillPacket(dataPointer+i,ThisNaluSize);
            alreadySent += ThisNaluSize;
            i += ThisNaluSize;
            ThisNaluSize = 0;
          }          
          if (alreadySent == watKunnenWeIn1Ding){
            packData.addStuffing();
            fillPacket(0, 0);
            first[thisPacket.getTrackId()] = true;
            break;
          }
        }
        currPack++;
      }
    }else if (myMeta.tracks[thisPacket.getTrackId()].type == "audio"){
      long unsigned int tempLen = dataLen;
      if ( myMeta.tracks[thisPacket.getTrackId()].codec == "AAC"){
        tempLen += 7;
      }
      long long unsigned int tempTime;
      if (appleCompat){
        tempTime = 0;// myMeta.tracks[thisPacket.getTrackId()].rate / 1000;
      }else{
        tempTime = thisPacket.getTime() * 90;
      }
      ///\todo stuur 70ms aan audio per PES pakket om applecompat overbodig te maken.
      //static unsigned long long lastSent=thisPacket.getTime() * 90;
      //if( (thisPacket.getTime() * 90)-lastSent >= 70*90 ){
      //   lastSent=(thisPacket.getTime() * 90);
      //}      
      bs = TS::Packet::getPESAudioLeadIn(tempLen, tempTime);// myMeta.tracks[thisPacket.getTrackId()].rate / 1000 );
      fillPacket(bs.data(), bs.size());
      if (myMeta.tracks[thisPacket.getTrackId()].codec == "AAC"){        
        bs = TS::getAudioHeader(dataLen, myMeta.tracks[thisPacket.getTrackId()].init);      
        fillPacket(bs.data(), bs.size());
      }
      fillPacket(dataPointer,dataLen);
    }
    if (packData.getBytesFree() < 184){
      packData.addStuffing();
      fillPacket(0, 0);
    }
  }
}
