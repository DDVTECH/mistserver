#include "analyser_ebml.h"
#include <iostream>
#include <mist/ebml.h>
#include <mist/timing.h>

void AnalyserEBML::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserEBML::AnalyserEBML(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
  lastSeekId = 0;
  lastSeekPos = 0;
}

bool AnalyserEBML::parsePacket(){
  prePos = curPos;
 
  while (dataBuffer.size() < neededBytes()) {
    uint64_t needed = neededBytes();
    if (needed > 1024 * 1024) {
      dataBuffer.erase(0, 1);
    }

    dataBuffer.reserve(needed);

    while (!buffer.available(needed - dataBuffer.size())) {
      if (uri.isEOF()) {
        FAIL_MSG("End of file");
        return false;
      }
      uri.readSome(needed - buffer.bytes(needed), *this);
    }

    uint64_t appending = needed - dataBuffer.size();
  dataBuffer.append(buffer.remove(appending));
  curPos += appending; 
  }

  if (dataBuffer.size() < neededBytes()){return false;}

  EBML::Element E(dataBuffer.data(), true);
  HIGH_MSG("Read an element at position %zu", prePos);
  if(!validate){
    if (detail >= 2){std::cout << E.toPrettyString(depthStash.size() * 2, detail);}
  }
  switch (E.getID()){
  case EBML::EID_SEGMENT:
    segmentOffset = prePos + E.getHeaderLen();
    if(!validate){
      std::cout << "[OFFSET INFORMATION] Segment offset is " << segmentOffset << std::endl;
    }
    break;
  case EBML::EID_CLUSTER:
    if(!validate){
      std::cout << "[OFFSET INFORMATION] Cluster at " << (prePos - segmentOffset) << std::endl;
    }
    break;
  case EBML::EID_SEEKID: lastSeekId = E.getValUInt(); break;
  case EBML::EID_SEEKPOSITION: lastSeekPos = E.getValUInt(); break;
  case EBML::EID_INFO:
  case EBML::EID_TRACKS:
  case EBML::EID_TAGS:
  case EBML::EID_CUES:{
    if(!validate){
      uint32_t sID = E.getID();
      std::cout << "Encountered " << sID << std::endl;
      if (seekChecks.count(sID)){
        std::cout << "[OFFSET INFORMATION] Segment " << EBML::Element::getIDString(sID) << " is at "
                  << prePos << ", expected was " << seekChecks[sID] << std::endl;
      }
    }
  }break;
  }
  if (depthStash.size()){depthStash.front() -= E.getOuterLen();}
  if (E.getType() == EBML::ELEM_MASTER){depthStash.push_front(E.getPayloadLen());}
  while (depthStash.size() && !depthStash.front()){
    depthStash.pop_front();
    if (lastSeekId){
      if (lastSeekId > 0xFFFFFF){
        lastSeekId &= 0xFFFFFFF;
      }else{
        if (lastSeekId > 0xFFFF){
          lastSeekId &= 0x1FFFFF;
        }else{
          if (lastSeekId > 0xFF){
            lastSeekId &= 0x3FFF;
          }else{
            lastSeekId &= 0x7F;
          }
        }
      }
      seekChecks[lastSeekId] = segmentOffset + lastSeekPos;
      if(!validate){
        std::cout << "[OFFSET INFORMATION] Segment offset for " << EBML::Element::getIDString(lastSeekId)
                  << " (" << lastSeekId << ") is " << (segmentOffset + lastSeekPos) << std::endl;
      }
      lastSeekId = 0;
      lastSeekPos = 0;
    }
  }
  ///\TODO update mediaTime with the current timestamp
  dataBuffer.erase(0, E.getOuterLen());
  return true;
}

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserEBML::neededBytes(){
  return EBML::Element::needBytes(dataBuffer.data(), dataBuffer.size(), true);
}

void AnalyserEBML::dataCallback(const char *ptr, size_t size) {
  buffer.append(ptr, size);
}
