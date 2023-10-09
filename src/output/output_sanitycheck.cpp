#include "output_sanitycheck.h"
#include <iomanip>
#include <mist/bitfields.h>
#include <mist/checksum.h>
#include <mist/defines.h>

namespace Mist{
  OutSanityCheck::OutSanityCheck(Socket::Connection &conn) : Output(conn){
    streamName = config->getString("streamname");
    //if (config->getOption("fakepush", true).size()){
      //pushMultiplier = config->getInteger("fakepush");
    //  if (!allowPush("testing")){onFinish();}
    //  return;
    //}
    parseData = true;
    wantRequest = false;
    syncMode = true;
    if (config->getBool("async")){
      setSyncMode(false);
      syncMode = false;
    }else{
      setSyncMode(true);
    }
  }

  void OutSanityCheck::sendHeader(){
    Output::sendHeader();
    sortSet.clear();
    realTime = 0;
    if (syncMode){
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
           it != userSelect.end(); it++){
        trkTime[it->first] = 0;
        Util::sortedPageInfo temp;
        temp.tid = it->first;
        DTSC::Keys keys = M.getKeys(it->first);
        size_t firstKey = keys.getFirstValid();
        temp.time = keys.getTime(firstKey);
        temp.partIndex = keys.getFirstPart(firstKey);
        INFO_MSG("Enabling part ordering checker: expecting track %zu, part %zu at time %" PRIu64, temp.tid, temp.partIndex, temp.time);
        sortSet.insert(temp);
      }
      if (config->getInteger("seek")){
        uint64_t seekPoint = config->getInteger("seek");

        while (sortSet.size() && sortSet.begin()->time < seekPoint){
          Util::sortedPageInfo temp = *sortSet.begin();
          temp.partIndex++;
          DTSC::Parts parts(M.parts(temp.tid));
          if (temp.partIndex < parts.getEndValid()){// only insert when there are parts left
            temp.time += parts.getDuration(temp.partIndex - 1);             // timeplace of frame
            sortSet.replaceFirst(temp);
          }else{
            sortSet.dropTrack(temp.tid);
          }
        }
        seek(seekPoint);
      }
    }
  }

  void OutSanityCheck::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "SanityCheck";
    capa["friendly"] = "Development tool: Sanity checker";
    capa["desc"] = "Does sanity check on a stream";
    capa["codecs"][0u][0u].append("+*");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Ignored, only exists to handle targetParams";
    cfg->addOption("target", opt);



    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream "
                                                  "that this connector will transmit.\"}"));
    cfg->addOption(
        "seek", JSON::fromString("{\"arg\":\"string\",\"short\":\"k\",\"long\":\"seek\",\"help\":"
                                 "\"Time in ms to check from - by default start of stream\"}"));
    cfg->addOption(
        "async", JSON::fromString("{\"short\":\"y\",\"long\":\"async\",\"help\":"
                                 "\"Retrieve tracks in async track sorting mode (default sync)\"}"));
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }

  /*
  void OutSanityCheck::requestHandler(){
    if (!pushing){
      Output::requestHandler();
      return;
    }
  }
  */

#define printTime(t) std::setfill('0') << std::setw(2) << (t / 3600000) << ":" << std::setw(2) << ((t % 3600000) / 60000) << ":" << std::setw(2) << ((t % 60000) / 1000) << "." << std::setw(3) << (t % 1000)

  void OutSanityCheck::writeContext(){
    std::cout << "Last few good packets:" << std::endl;
    while (packets.size()){
      std::cout << "  " << packets.front() << std::endl;
      packets.pop_front();
    }
    std::cout << "Nearby keyframes for this track (" << thisIdx << ", " << M.getType(thisIdx) << "):" << std::endl;
    size_t keyIndex = M.getKeyIndexForTime(thisIdx, thisTime);
    DTSC::Keys keys = M.getKeys(thisIdx);
    size_t from = keyIndex - 5;
    size_t to = keyIndex + 5;

    if (from < keys.getFirstValid()){from = keys.getFirstValid();}
    if (to > keys.getEndValid()){to = keys.getEndValid();}
    for (size_t i = from; i <= to; ++i){
      std::cout << "  " << i << ": " << keys.getTime(i) << std::endl;
    }



  }


  void OutSanityCheck::sendNext(){

    if (thisTime < trkTime[thisIdx]){
      std::cout << "Time error in track " << thisIdx << ": ";
      std::cout << printTime(thisTime) << " < " << printTime(trkTime[thisIdx]) << std::endl << std::endl;
    }else{
      trkTime[thisIdx] = thisTime;
    }
    std::cout << "\033[A";
    for (std::map<size_t, uint64_t>::iterator it = trkTime.begin(); it != trkTime.end(); ++it){
      uint64_t t = M.getLastms(it->first);
      std::cout << it->first << ":" << printTime(it->second) << "/" << printTime(t) << ", ";
    }
    std::cout << std::endl;

    if (syncMode){
      if (thisIdx != sortSet.begin()->tid || thisPacket.getTime() != sortSet.begin()->time){
        std::cout << "Input is inconsistent! Expected " << sortSet.begin()->tid << ":"
                  << sortSet.begin()->time << " but got " << thisIdx << ":" << thisPacket.getTime()
                  << " (expected part " << sortSet.begin()->partIndex << " in "
                  << M.getCodec(sortSet.begin()->tid) << " track)" << std::endl;
        writeContext();
        myConn.close();
        return;
      }
    }

    size_t keyIndex = M.getKeyIndexForTime(thisIdx, thisTime);
    uint64_t keyTime = M.getTimeForKeyIndex(thisIdx, keyIndex);
    bool isKey = thisPacket.getFlag("keyframe");
    if (keyTime > thisTime){
      std::cout << "Corruption? Our time is " << thisTime << ", but our key time is " << keyTime << std::endl;
      writeContext();
      myConn.close();
      return;
    }else{
      if (M.getType(thisIdx) == "video"){
        if (keyTime == thisTime){
          if (!isKey){
            std::cout << "Corruption? Video packet at time " << thisTime << " should be a keyframe, but isn't!" << std::endl;
            writeContext();
            myConn.close();
            return;
          }
        }else{
          if (isKey){
            std::cout << "Corruption? Video packet at time " << thisTime << " should not be a keyframe, but is!" << std::endl;
            writeContext();
            myConn.close();
            return;
          }
        }
      }
    }

    // Packet is normally sent here
    packets.push_back(thisPacket.toSummary());
    while (packets.size() > 10){packets.pop_front();}

    // keep track of where we are
    if (syncMode && sortSet.size()){
      Util::sortedPageInfo temp = *sortSet.begin();
      temp.partIndex++;
      DTSC::Parts parts(M.parts(temp.tid));
      if (temp.partIndex < parts.getEndValid()){// only insert when there are parts left
        temp.time += parts.getDuration(temp.partIndex-1);
        sortSet.replaceFirst(temp);
      }else{
        sortSet.dropTrack(temp.tid);
      }
    }
  }

}// namespace Mist
