#include <mist/defines.h>
#include <mist/checksum.h>
#include <mist/bitfields.h>
#include "output_sanitycheck.h"

namespace Mist {
  OutSanityCheck::OutSanityCheck(Socket::Connection & conn) : Output(conn){
    streamName = config->getString("streamname");
    parseData = true;
    wantRequest = false;
    initialize();
    initialSeek();
    sortSet.clear();
    for (std::set<long unsigned int>::iterator subIt = selectedTracks.begin(); subIt != selectedTracks.end(); subIt++) {
      keyPart temp;
      temp.trackID = *subIt;
      temp.time = myMeta.tracks[*subIt].firstms;//timeplace of frame
      temp.endTime = myMeta.tracks[*subIt].firstms + myMeta.tracks[*subIt].parts[0].getDuration();
      temp.size = myMeta.tracks[*subIt].parts[0].getSize();//bytesize of frame (alle parts all together)
      temp.index = 0;
      sortSet.insert(temp);
    }
    realTime = 0;

    if (config->getInteger("seek")){
      uint64_t seekPoint = config->getInteger("seek");

      while (!sortSet.empty() && sortSet.begin()->time < seekPoint) {
        keyPart temp;
        temp.index = sortSet.begin()->index + 1;
        temp.trackID = sortSet.begin()->trackID;
        if (temp.index < myMeta.tracks[temp.trackID].parts.size()) { //only insert when there are parts left
          temp.time = sortSet.begin()->endTime;//timeplace of frame
          temp.endTime = sortSet.begin()->endTime + myMeta.tracks[temp.trackID].parts[temp.index].getDuration();
          temp.size = myMeta.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame
          sortSet.insert(temp);
        }
        //remove highest keyPart
        sortSet.erase(sortSet.begin());
      }
      seek(seekPoint);
    }


  }

  void OutSanityCheck::init(Util::Config * cfg) {
    Output::init(cfg);
    capa["name"] = "SanityCheck";
    capa["desc"] = "Does sanity check on a stream";
    capa["codecs"][0u][0u].append("*");
    cfg->addOption("streamname", JSON::fromString("{\"arg\":\"string\",\"short\":\"s\",\"long\":\"stream\",\"help\":\"The name of the stream that this connector will transmit.\"}"));
    cfg->addOption("seek", JSON::fromString("{\"arg\":\"string\",\"short\":\"S\",\"long\":\"seek\",\"help\":\"Time in ms to check from - by default start of stream\"}"));
    cfg->addBasicConnectorOptions(capa);
    config = cfg;
  }

  void OutSanityCheck::sendNext() {
    if ((unsigned long)thisPacket.getTrackId() != sortSet.begin()->trackID || thisPacket.getTime() != sortSet.begin()->time) {
      while (packets.size()){
        std::cout << packets.front() << std::endl;
        packets.pop_front();
      }
      std::cout << "Input is inconsistent! Expected " << sortSet.begin()->trackID << ":" << sortSet.begin()->time << " but got " << thisPacket.getTrackId() << ":" << thisPacket.getTime() << " (part " << sortSet.begin()->index << " in " << myMeta.tracks[sortSet.begin()->trackID].codec << " track)" << std::endl;
      myConn.close();
      return;
    }

    //Packet is normally sent here
    packets.push_back(thisPacket.toSummary());
    while (packets.size() > 10){packets.pop_front();}

    //keep track of where we are
    if (!sortSet.empty()) {
      keyPart temp;
      temp.index = sortSet.begin()->index + 1;
      temp.trackID = sortSet.begin()->trackID;
      if (temp.index < myMeta.tracks[temp.trackID].parts.size()) { //only insert when there are parts left
        temp.time = sortSet.begin()->endTime;//timeplace of frame
        temp.endTime = sortSet.begin()->endTime + myMeta.tracks[temp.trackID].parts[temp.index].getDuration();
        temp.size = myMeta.tracks[temp.trackID].parts[temp.index].getSize();//bytesize of frame
        sortSet.insert(temp);
      }
      //remove highest keyPart
      sortSet.erase(sortSet.begin());
    }

  }

}

