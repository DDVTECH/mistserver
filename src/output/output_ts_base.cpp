#include "output_ts_base.h"

namespace Mist {
  TSOutput::TSOutput(Socket::Connection & conn) : TS_BASECLASS(conn){
    packCounter=0;
    haveAvcc = false;
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
        tmpPack.setContinuityCounter(++contCounters[tmpPack.getPID()]);
        sendTS(tmpPack.checkAndGetBuffer());
        sendTS(TS::createPMT(selectedTracks, myMeta, ++contCounters[tmpPack.getPID()]));
        packCounter += 2;
      }
      sendTS(packData.checkAndGetBuffer());
      packCounter ++;
      packData.clear();
    }
    
    if (!dataLen){return;}
    
    if (packData.getBytesFree() == 184){
      packData.clear();      
      packData.setPID(0x100 - 1 + currentPacket.getTrackId());      
      packData.setContinuityCounter(++contCounters[packData.getPID()]);
      if (first[currentPacket.getTrackId()]){
        packData.setUnitStart(1);
        if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){
          if (currentPacket.getInt("keyframe")){
            packData.setRandomAccess(1);
          }      
          packData.setPCR(currentPacket.getTime() * 27000);      
        }
        first[currentPacket.getTrackId()] = false;
      }
    }
    
    int tmp = packData.fillFree(data, dataLen);     
    if (tmp != dataLen){
      return fillPacket(data+tmp, dataLen-tmp);
    }
  }

  void TSOutput::sendNext(){
    first[currentPacket.getTrackId()] = true;
    char * dataPointer = 0;
    unsigned int dataLen = 0;
    currentPacket.getString("data", dataPointer, dataLen); //data
    if (currentPacket.getTime() >= until){ //this if should only trigger for HLS       
      stop();
      wantRequest = true;
      parseData = false;
      sendTS("",0);      
      return;
    }
    std::string bs;
    //prepare bufferstring    
    if (myMeta.tracks[currentPacket.getTrackId()].type == "video"){      
      unsigned int extraSize = 0;      
      //dataPointer[4] & 0x1f is used to check if this should be done later: fillPacket("\000\000\000\001\011\360", 6);
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
        extraSize += 6;
      }
      if (currentPacket.getInt("keyframe")){
        if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264"){
          if (!haveAvcc){
            avccbox.setPayload(myMeta.tracks[currentPacket.getTrackId()].init);
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
      
      while (currPack <= splitCount){
        unsigned int alreadySent = 0;
        bs = TS::Packet::getPESVideoLeadIn((currPack != splitCount ? watKunnenWeIn1Ding : dataLen+extraSize - currPack*watKunnenWeIn1Ding), currentPacket.getTime() * 90, currentPacket.getInt("offset") * 90, !currPack);
        fillPacket(bs.data(), bs.size());
        if (!currPack){
          if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
            //End of previous nal unit, if not already present
            fillPacket("\000\000\000\001\011\360", 6);
            alreadySent += 6;
          }
          if (currentPacket.getInt("keyframe")){
            if (myMeta.tracks[currentPacket.getTrackId()].codec == "H264"){
              bs = avccbox.asAnnexB();
              fillPacket(bs.data(), bs.size());
              alreadySent += bs.size();
            }
          }
        }
        while (i + 4 < (unsigned int)dataLen){
          if (!ThisNaluSize){
            ThisNaluSize = (dataPointer[i] << 24) + (dataPointer[i+1] << 16) + (dataPointer[i+2] << 8) + dataPointer[i+3];
            if (ThisNaluSize + i + 4 > (unsigned int)dataLen){
              DEBUG_MSG(DLVL_WARN, "Too big NALU detected (%u > %d) - skipping!", ThisNaluSize + i + 4, dataLen);
              break;
            }
            if (alreadySent + 4 > watKunnenWeIn1Ding){
              /// \todo Houd rekening met deze relatief zelfdzame sub-optimale situatie
              //Kom op, wat is de kans nou? ~_~
              FAIL_MSG("Encountered lazy coders. Maybe someone should fix this.");
            }
            fillPacket("\000\000\000\001",4);
            alreadySent += 4;
            i += 4;
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
            first[currentPacket.getTrackId()] = true;
            break;
          }
        }
        currPack++;
      }
    }else if (myMeta.tracks[currentPacket.getTrackId()].type == "audio"){
      long unsigned int tempLen = dataLen;
      if ( myMeta.tracks[currentPacket.getTrackId()].codec == "AAC"){
        tempLen += 7;
      }
      long long unsigned int tempTime;
      if (appleCompat){
        tempTime = 0;// myMeta.tracks[currentPacket.getTrackId()].rate / 1000;
      }else{
        tempTime = currentPacket.getTime() * 90;
      }
      ///\todo stuur 70ms aan audio per PES pakket om applecompat overbodig te maken.
      //static unsigned long long lastSent=currentPacket.getTime() * 90;
      //if( (currentPacket.getTime() * 90)-lastSent >= 70*90 ){
      //   lastSent=(currentPacket.getTime() * 90);
      //}      
      bs = TS::Packet::getPESAudioLeadIn(tempLen, tempTime);// myMeta.tracks[currentPacket.getTrackId()].rate / 1000 );
      fillPacket(bs.data(), bs.size());
      if (myMeta.tracks[currentPacket.getTrackId()].codec == "AAC"){        
        bs = TS::getAudioHeader(dataLen, myMeta.tracks[currentPacket.getTrackId()].init);      
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
