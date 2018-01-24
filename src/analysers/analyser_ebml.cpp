#include "analyser_ebml.h"
#include <iostream>
#include <mist/ebml.h>

void AnalyserEBML::init(Util::Config &conf){
  Analyser::init(conf);
}

AnalyserEBML::AnalyserEBML(Util::Config &conf) : Analyser(conf){
  curPos = prePos = 0;
}

bool AnalyserEBML::parsePacket(){
  prePos = curPos;
  // Read in smart bursts until we have enough data
  while (isOpen() && dataBuffer.size() < neededBytes()){
    uint64_t needed = neededBytes();
    dataBuffer.reserve(needed);
    for (uint64_t i = dataBuffer.size(); i < needed; ++i){
      dataBuffer += std::cin.get();
      ++curPos;
      if (!std::cin.good()){dataBuffer.erase(dataBuffer.size() - 1, 1);}
    }
  }

  if (dataBuffer.size() < neededBytes()){return false;}

  EBML::Element E(dataBuffer.data(), true);
  HIGH_MSG("Read an element at position %d", prePos);
  if (detail >= 2){std::cout << E.toPrettyString(depthStash.size() * 2, detail);}
  if (depthStash.size()){
    depthStash.front() -= E.getOuterLen();
  }
  if (E.getType() == EBML::ELEM_MASTER){
    depthStash.push_front(E.getPayloadLen());
  }
  while (depthStash.size() && !depthStash.front()){
    depthStash.pop_front();
  }
  ///\TODO update mediaTime with the current timestamp
  dataBuffer.erase(0, E.getOuterLen());
  return true;
}

/// Calculates how many bytes we need to read a whole box.
uint64_t AnalyserEBML::neededBytes(){
  return EBML::Element::needBytes(dataBuffer.data(), dataBuffer.size(), true);
}

