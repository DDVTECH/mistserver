#include "output_ts_base.h"
#include <mist/bitfields.h>

namespace Mist{
  template<class T>
  TSOutputTmpl<T>::TSOutputTmpl(Socket::Connection &conn) : T(conn){
    packCounter = 0;
    ts_from = 0;
    this->setBlocking(true);
    sendRepeatingHeaders = 0;
    lastHeaderTime = 0;
  }

  template<class T>
  void TSOutputTmpl<T>::fillPacket(char const *data, size_t dataLen, bool &firstPack, bool video,
                            bool keyframe, size_t pkgPid, uint16_t &contPkg){
    do{
      if (!packData.getBytesFree()){
        if ((sendRepeatingHeaders && this->thisPacket.getTime() - lastHeaderTime > sendRepeatingHeaders) || !packCounter){

          std::set<size_t> selectedTracks;
          for (std::map<size_t, Comms::Users>::iterator it = this->userSelect.begin(); it != this->userSelect.end(); it++){
            selectedTracks.insert(it->first);
          }

          lastHeaderTime = this->thisPacket.getTime();
          TS::Packet tmpPack;
          tmpPack.FromPointer(TS::PAT);
          tmpPack.setContinuityCounter(++contPAT);
          sendTS(tmpPack.checkAndGetBuffer());
          sendTS(TS::createPMT(selectedTracks, this->M, ++contPMT));
          sendTS(TS::createSDT(this->streamName, ++contSDT));
          packCounter += 3;
        }
        sendTS(packData.checkAndGetBuffer());
        packCounter++;
        packData.clear();
      }

      if (!dataLen){return;}

      if (packData.getBytesFree() == 184){
        packData.clear();
        packData.setPID(pkgPid);
        packData.setContinuityCounter(++contPkg);
        if (firstPack){
          packData.setUnitStart(1);
          if (video){
            if (keyframe){
              packData.setRandomAccess(true);
              packData.setESPriority(true);
            }
            packData.setPCR(this->thisPacket.getTime() * 27000);
          }
          firstPack = false;
        }
      }

      size_t tmp = packData.fillFree(data, dataLen);
      data += tmp;
      dataLen -= tmp;
    }while (dataLen);
  }

  template<class T>
  void TSOutputTmpl<T>::sendNext(){
    static uint64_t lastMeta = 0;
    if (Util::epoch() > lastMeta + 5){
      lastMeta = Util::epoch();
      if (this->selectDefaultTracks()){
        INFO_MSG("Track selection changed - resending headers and continuing");
        packCounter = 0;
        return;
      }
    }
    // Get ready some data to speed up accesses
    std::string type = this->M.getType(this->thisIdx);
    std::string codec = this->M.getCodec(this->thisIdx);
    bool video = (type == "video");
    size_t pkgPid = TS::getUniqTrackID(this->M, this->thisIdx);
    bool &firstPack = first[this->thisIdx];
    uint16_t &contPkg = contCounters[pkgPid];
    uint64_t packTime = this->thisPacket.getTime();
    bool keyframe = this->thisPacket.getInt("keyframe");
    firstPack = true;
    char *dataPointer = 0;
    size_t dataLen = 0;
    this->thisPacket.getString("data", dataPointer, dataLen); // data

    if (codec == "rawts"){
      for (size_t i = 0; i+188 <= dataLen; i+=188){sendTS(dataPointer+i, 188);}
      return;
    }

    packTime *= 90;
    std::string bs;
    // prepare bufferstring
    if (video){
      bool addInit = keyframe;
      bool addEndNal = true;
      if (codec == "H264" || codec == "HEVC"){
        uint32_t extraSize = 0;
        //Check if we need to skip sending some things
        if (codec == "H264"){
          size_t ctr = 0;
          char * ptr = dataPointer;
          while (ptr+4 < dataPointer+dataLen && ++ctr <= 5){
            switch (ptr[4] & 0x1f){
            case 0x07://init
            case 0x08://init
              addInit = false;
              break;
            case 0x09://new nal
              addEndNal = false;
              break;
            default: break;
            }
            ptr += Bit::btohl(ptr);
          }
        }

        if (addEndNal && codec == "H264"){extraSize += 6;}
        if (addInit){
          if (codec == "H264"){
            MP4::AVCC avccbox;
            avccbox.setPayload(this->M.getInit(this->thisIdx));
            bs = avccbox.asAnnexB();
            extraSize += bs.size();
          }
          if (codec == "HEVC"){
            MP4::HVCC hvccbox;
            hvccbox.setPayload(this->M.getInit(this->thisIdx));
            bs = hvccbox.asAnnexB();
            extraSize += bs.size();
          }
        }

        const uint32_t MAX_PES_SIZE = 65490 - 13;
        uint32_t ThisNaluSize = 0;
        uint32_t i = 0;
        uint64_t offset = this->thisPacket.getInt("offset") * 90;

        bs.clear();
        TS::Packet::getPESVideoLeadIn(bs,
            (((dataLen + extraSize) > MAX_PES_SIZE) ? 0 : dataLen + extraSize),
            packTime, offset, true, this->M.getBps(this->thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);

        // End of previous nal unit, if not already present
        if (addEndNal && codec == "H264"){
          fillPacket("\000\000\000\001\011\360", 6, firstPack, video, keyframe, pkgPid, contPkg);
        }
        // Init data, if keyframe and not already present
        if (addInit){
          if (codec == "H264"){
            MP4::AVCC avccbox;
            avccbox.setPayload(this->M.getInit(this->thisIdx));
            bs = avccbox.asAnnexB();
            fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
          }
          /*LTS-START*/
          if (codec == "HEVC"){
            MP4::HVCC hvccbox;
            hvccbox.setPayload(this->M.getInit(this->thisIdx));
            bs = hvccbox.asAnnexB();
            fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
          }
          /*LTS-END*/
        }
        size_t lenSize = 4;
        if (codec == "H264"){lenSize = (this->M.getInit(this->thisIdx)[4] & 3) + 1;}
        while (i + lenSize < (unsigned int)dataLen){
          if (lenSize == 4){
            ThisNaluSize = Bit::btohl(dataPointer + i);
          }else if (lenSize == 2){
            ThisNaluSize = Bit::btohs(dataPointer + i);
          }else{
            ThisNaluSize = dataPointer[i];
          }
          if (ThisNaluSize + i + lenSize > dataLen){
            WARN_MSG("Too big NALU detected (%" PRIu32 " > %zu) - skipping!",
                     ThisNaluSize + i + 4, dataLen);
            break;
          }
          fillPacket("\000\000\000\001", 4, firstPack, video, keyframe, pkgPid, contPkg);
          fillPacket(dataPointer + i + lenSize, ThisNaluSize, firstPack, video, keyframe, pkgPid, contPkg);
          i += ThisNaluSize + lenSize;
        }
      }else{
        uint64_t offset = this->thisPacket.getInt("offset") * 90;
        bs.clear();
        TS::Packet::getPESVideoLeadIn(bs, 0, packTime, offset, true, this->M.getBps(this->thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);

        fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
      }
    }else if (type == "audio"){
      size_t tempLen = dataLen;
      if (codec == "AAC"){
        tempLen += 7;
        // Make sure TS timestamp is sample-aligned, if possible
        uint32_t freq = this->M.getRate(this->thisIdx);
        if (freq){
          uint64_t aacSamples = packTime * freq / 90000;
          //round to nearest packet, assuming all 1024 samples (probably wrong, but meh)
          aacSamples += 256;//Add a quarter frame of offset to encourage correct rounding
          aacSamples &= ~0x3FF;
          //Get closest 90kHz clock time to perfect sample alignment
          packTime = aacSamples * 90000 / freq;
        }
      }
      if (codec == "opus"){
        tempLen += 3 + (dataLen/255);
        bs = TS::Packet::getPESPS1LeadIn(tempLen, packTime, this->M.getBps(this->thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
        bs = "\177\340";
        bs.append(dataLen/255, (char)255);
        bs.append(1, (char)(dataLen-255*(dataLen/255)));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      }else{
        bs.clear();
        TS::Packet::getPESAudioLeadIn(bs, tempLen, packTime, this->M.getBps(this->thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
        if (codec == "AAC"){
          bs = TS::getAudioHeader(dataLen, this->M.getInit(this->thisIdx));
          fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
        }
      }
      fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
    }else if (type == "meta"){
      long unsigned int tempLen = dataLen;
      if (codec == "JSON"){tempLen += 2;}
      bs = TS::Packet::getPESMetaLeadIn(tempLen, packTime, this->M.getBps(this->thisIdx));
      fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      if (codec == "JSON"){
        char dLen[2];
        Bit::htobs(dLen, dataLen);
        fillPacket(dLen, 2, firstPack, video, keyframe, pkgPid, contPkg);
      }
      fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
    }
    if (packData.getBytesFree() < 184){
      packData.addStuffing();
      fillPacket(0, 0, firstPack, video, keyframe, pkgPid, contPkg);
    }
  }

  TSOutput::TSOutput(Socket::Connection &conn) : TSOutputTmpl<Output>(conn){}
  TSOutputHTTP::TSOutputHTTP(Socket::Connection &conn) : TSOutputTmpl<HTTPOutput>(conn){}
}// namespace Mist
