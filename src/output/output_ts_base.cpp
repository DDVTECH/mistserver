#include "output_ts_base.h"
#include <mist/bitfields.h>

namespace Mist{
  TSOutput::TSOutput(Socket::Connection &conn) : TS_BASECLASS(conn){
    packCounter = 0;
    ts_from = 0;
    setBlocking(true);
    sendRepeatingHeaders = 0;
    lastHeaderTime = 0;
  }

  void TSOutput::fillPacket(char const *data, size_t dataLen, bool &firstPack, bool video,
                            bool keyframe, size_t pkgPid, uint16_t &contPkg){
    do{
      if (!packData.getBytesFree()){
        if ((sendRepeatingHeaders && thisPacket.getTime() - lastHeaderTime > sendRepeatingHeaders) || !packCounter){

          std::set<size_t> selectedTracks;
          for (std::map<size_t, Comms::Users>::iterator it = userSelect.begin(); it != userSelect.end(); it++){
            selectedTracks.insert(it->first);
          }

          lastHeaderTime = thisPacket.getTime();
          TS::Packet tmpPack;
          tmpPack.FromPointer(TS::PAT);
          tmpPack.setContinuityCounter(++contPAT);
          sendTS(tmpPack.checkAndGetBuffer());
          sendTS(TS::createPMT(selectedTracks, M, ++contPMT));
          sendTS(TS::createSDT(streamName, ++contSDT));
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
            packData.setPCR(thisPacket.getTime() * 27000);
          }
          firstPack = false;
        }
      }

      size_t tmp = packData.fillFree(data, dataLen);
      data += tmp;
      dataLen -= tmp;
    }while (dataLen);
  }

  void TSOutput::sendNext(){
    // Get ready some data to speed up accesses
    std::string type = M.getType(thisIdx);
    std::string codec = M.getCodec(thisIdx);
    bool video = (type == "video");
    size_t pkgPid = TS::getUniqTrackID(M, thisIdx);
    bool &firstPack = first[thisIdx];
    uint16_t &contPkg = contCounters[pkgPid];
    uint64_t packTime = thisPacket.getTime();
    bool keyframe = thisPacket.getInt("keyframe");
    firstPack = true;
    char *dataPointer = 0;
    size_t dataLen = 0;
    thisPacket.getString("data", dataPointer, dataLen); // data

    packTime *= 90;
    std::string bs;
    // prepare bufferstring
    if (video){
      if (codec == "H264" || codec == "HEVC"){
        unsigned int extraSize = 0;
        // dataPointer[4] & 0x1f is used to check if this should be done later:
        // fillPacket("\000\000\000\001\011\360", 6);
        if (codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){extraSize += 6;}
        if (keyframe){
          if (codec == "H264"){
            MP4::AVCC avccbox;
            avccbox.setPayload(M.getInit(thisIdx));
            bs = avccbox.asAnnexB();
            extraSize += bs.size();
          }
          /*LTS-START*/
          if (codec == "HEVC"){
            MP4::HVCC hvccbox;
            hvccbox.setPayload(M.getInit(thisIdx));
            bs = hvccbox.asAnnexB();
            extraSize += bs.size();
          }
          /*LTS-END*/
        }

        unsigned int watKunnenWeIn1Ding = 65490 - 13;
        unsigned int splitCount = (dataLen + extraSize) / watKunnenWeIn1Ding;
        unsigned int currPack = 0;
        uint64_t ThisNaluSize = 0;
        unsigned int i = 0;
        unsigned int nalLead = 0;
        uint64_t offset = thisPacket.getInt("offset") * 90;

        while (currPack <= splitCount){
          unsigned int alreadySent = 0;
          bs = TS::Packet::getPESVideoLeadIn(
              (currPack != splitCount ? watKunnenWeIn1Ding : dataLen + extraSize - currPack * watKunnenWeIn1Ding),
              packTime, offset, !currPack, M.getBps(thisIdx));
          fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
          if (!currPack){
            if (codec == "H264" && (dataPointer[4] & 0x1f) != 0x09){
              // End of previous nal unit, if not already present
              fillPacket("\000\000\000\001\011\360", 6, firstPack, video, keyframe, pkgPid, contPkg);
              alreadySent += 6;
            }
            if (keyframe){
              if (codec == "H264"){
                MP4::AVCC avccbox;
                avccbox.setPayload(M.getInit(thisIdx));
                bs = avccbox.asAnnexB();
                fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
                alreadySent += bs.size();
              }
              /*LTS-START*/
              if (codec == "HEVC"){
                MP4::HVCC hvccbox;
                hvccbox.setPayload(M.getInit(thisIdx));
                bs = hvccbox.asAnnexB();
                fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
                alreadySent += bs.size();
              }
              /*LTS-END*/
            }
          }
          while (i + 4 < (unsigned int)dataLen){
            if (nalLead){
              fillPacket(&"\000\000\000\001"[4 - nalLead], nalLead, firstPack, video, keyframe, pkgPid, contPkg);
              i += nalLead;
              alreadySent += nalLead;
              nalLead = 0;
            }
            if (!ThisNaluSize){
              ThisNaluSize = Bit::btohl(dataPointer + i);
              if (ThisNaluSize + i + 4 > dataLen){
                WARN_MSG("Too big NALU detected (%" PRIu64 " > %" PRIu64 ") - skipping!",
                         ThisNaluSize + i + 4, dataLen);
                break;
              }
              if (alreadySent + 4 > watKunnenWeIn1Ding){
                nalLead = 4 - (watKunnenWeIn1Ding - alreadySent);
                fillPacket("\000\000\000\001", watKunnenWeIn1Ding - alreadySent, firstPack, video,
                           keyframe, pkgPid, contPkg);
                i += watKunnenWeIn1Ding - alreadySent;
                alreadySent += watKunnenWeIn1Ding - alreadySent;
              }else{
                fillPacket("\000\000\000\001", 4, firstPack, video, keyframe, pkgPid, contPkg);
                alreadySent += 4;
                i += 4;
              }
            }
            if (alreadySent + ThisNaluSize > watKunnenWeIn1Ding){
              fillPacket(dataPointer + i, watKunnenWeIn1Ding - alreadySent, firstPack, video,
                         keyframe, pkgPid, contPkg);
              i += watKunnenWeIn1Ding - alreadySent;
              ThisNaluSize -= watKunnenWeIn1Ding - alreadySent;
              alreadySent += watKunnenWeIn1Ding - alreadySent;
            }else{
              fillPacket(dataPointer + i, ThisNaluSize, firstPack, video, keyframe, pkgPid, contPkg);
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
      }else{
        uint64_t offset = thisPacket.getInt("offset") * 90;
        bs = TS::Packet::getPESVideoLeadIn(0, packTime, offset, true, M.getBps(thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);

        fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
      }
    }else if (type == "audio"){
      size_t tempLen = dataLen;
      if (codec == "AAC"){
        tempLen += 7;
        // Make sure TS timestamp is sample-aligned, if possible
        uint32_t freq = M.getRate(thisIdx);
        if (freq){
          uint64_t aacSamples = (packTime / 90) * freq / 1000;
          // round to nearest packet, assuming all 1024 samples (probably wrong, but meh)
          aacSamples += 512;
          aacSamples /= 1024;
          aacSamples *= 1024;
          // Get closest 90kHz clock time to perfect sample alignment
          packTime = aacSamples * 90000 / freq;
        }
      }
      bs = TS::Packet::getPESAudioLeadIn(tempLen, packTime, M.getBps(thisIdx));
      fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      if (codec == "AAC"){
        bs = TS::getAudioHeader(dataLen, M.getInit(thisIdx));
        fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      }
      fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
    }else if (type == "meta"){
      long unsigned int tempLen = dataLen;
      bs = TS::Packet::getPESMetaLeadIn(tempLen, packTime, M.getBps(thisIdx));
      fillPacket(bs.data(), bs.size(), firstPack, video, keyframe, pkgPid, contPkg);
      fillPacket(dataPointer, dataLen, firstPack, video, keyframe, pkgPid, contPkg);
    }
    if (packData.getBytesFree() < 184){
      packData.addStuffing();
      fillPacket(0, 0, firstPack, video, keyframe, pkgPid, contPkg);
    }
  }
}// namespace Mist
