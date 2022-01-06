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
    if (config->getBool("sync")){
      setSyncMode(true);
    }else{
      setSyncMode(false);
    }
    initialize();
    initialSeek();
    sortSet.clear();
    if (!M.getLive()){
      realTime = 0;
      for (std::map<size_t, Comms::Users>::const_iterator it = userSelect.begin();
           it != userSelect.end(); it++){
        keyPart temp;
        temp.trackID = it->first;
        temp.time = M.getFirstms(it->first); // timeplace of frame
        DTSC::Parts parts(M.parts(it->first));
        temp.endTime = M.getFirstms(it->first) + parts.getDuration(parts.getFirstValid());
        temp.size = parts.getSize(parts.getFirstValid()); // bytesize of frame (alle parts all together)
        temp.index = 0;
        sortSet.insert(temp);
      }
      if (config->getInteger("seek")){
        uint64_t seekPoint = config->getInteger("seek");

        while (!sortSet.empty() && sortSet.begin()->time < seekPoint){
          keyPart temp = *sortSet.begin();
          temp.index++;
          DTSC::Parts parts(M.parts(temp.trackID));
          if (temp.index < parts.getEndValid()){// only insert when there are parts left
            temp.time = temp.endTime;             // timeplace of frame
            temp.endTime = temp.time + parts.getDuration(temp.index);
            temp.size = parts.getSize(temp.index);
            ; // bytesize of frame
            sortSet.insert(temp);
          }
          // remove highest keyPart
          sortSet.erase(sortSet.begin());
        }
        seek(seekPoint);
      }
    }

  }

  void OutSanityCheck::init(Util::Config *cfg){
    Output::init(cfg);
    capa["name"] = "SanityCheck";
    capa["desc"] = "Does sanity check on a stream";
    capa["codecs"][0u][0u].append("+*");
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":"
                                                  "\"stream\",\"help\":\"The name of the stream "
                                                  "that this connector will transmit.\"}"));
    cfg->addOption(
        "seek", JSON::fromString("{\"arg\":\"string\",\"short\":\"k\",\"long\":\"seek\",\"help\":"
                                 "\"Time in ms to check from - by default start of stream\"}"));
    cfg->addOption(
        "sync", JSON::fromString("{\"short\":\"y\",\"long\":\"sync\",\"help\":"
                                 "\"Retrieve tracks in sync (default async)\"}"));
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

  void OutSanityCheck::sendNext(){
    static std::map<size_t, uint64_t> trkTime;
    if (M.getLive()){
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
      return;
    }

    if (thisIdx != sortSet.begin()->trackID || thisPacket.getTime() != sortSet.begin()->time){
      while (packets.size()){
        std::cout << packets.front() << std::endl;
        packets.pop_front();
      }
      std::cout << "Input is inconsistent! Expected " << sortSet.begin()->trackID << ":"
                << sortSet.begin()->time << " but got " << thisIdx << ":" << thisPacket.getTime()
                << " (expected part " << sortSet.begin()->index << " in "
                << M.getCodec(sortSet.begin()->trackID) << " track)" << std::endl;
      myConn.close();
      return;
    }


    size_t keyIndex = M.getKeyIndexForTime(getMainSelectedTrack(), thisPacket.getTime());
    uint64_t keyTime = M.getTimeForKeyIndex(getMainSelectedTrack(), keyIndex);
    if (keyTime > thisPacket.getTime()){
      std::cout << "Corruption? Our time is " << thisPacket.getTime() << ", but our key time is " << keyTime << std::endl;
      myConn.close();
      return;
    }

    // Packet is normally sent here
    packets.push_back(thisPacket.toSummary());
    while (packets.size() > 10){packets.pop_front();}

    // keep track of where we are
    if (!sortSet.empty()){
      keyPart temp = *sortSet.begin();
      temp.index++;
      DTSC::Parts parts(M.parts(temp.trackID));
      if (temp.index < parts.getEndValid()){// only insert when there are parts left
        temp.time = temp.endTime;             // timeplace of frame
        temp.endTime = temp.time + parts.getDuration(temp.index);
        temp.size = parts.getSize(temp.index); // bytesize of frame
        sortSet.insert(temp);
      }
      // remove highest keyPart
      sortSet.erase(sortSet.begin());
    }
  }

}// namespace Mist
