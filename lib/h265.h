#pragma once
#include <deque>
#include <map>
#include <set>

#include "nal.h"
#include "mp4_generic.h"
#include "bitstream.h"

namespace h265 {
  std::deque<nalu::nalData> analysePackets(const char * data, unsigned long len);

  void updateProfileTierLevel(Utils::bitstream & bs, MP4::HVCC & hvccBox, unsigned long maxSubLayersMinus1);

  class initData {
    public:
      initData();
      void addUnit(char * data);
      bool haveRequired();
      std::string generateHVCC();
    protected:
      std::map<unsigned int, std::set<std::string> > nalUnits;
  };

  class vpsUnit {
    public:
      vpsUnit(const std::string & _data);
      void updateHVCC(MP4::HVCC & hvccBox);
    private:
      std::string data;
  };

  class spsUnit {
    public:
      spsUnit(const std::string & _data);
      void updateHVCC(MP4::HVCC & hvccBox);
    private:
      std::string data;
  };

  //NOTE: no ppsUnit, as the only information it contains is parallelism mode, which can be set to 0 for 'unknown'
}
