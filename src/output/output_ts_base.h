#include <mist/defines.h>
#include "output.h"
#include "output_http.h"
#include <mist/mp4_generic.h>
#include <mist/ts_packet.h>

#ifndef TS_BASECLASS
#define TS_BASECLASS Output
#endif

namespace Mist {

  class TSOutput : public TS_BASECLASS {
    public:
      TSOutput(Socket::Connection & conn);
      virtual ~TSOutput(){};
      virtual void sendNext();      
      virtual void sendTS(const char * tsData, unsigned int len=188){};
      void fillPacket(char const * data, size_t dataLen, bool & firstPack, bool video, bool keyframe, uint32_t pkgPid, int & contPkg);    
    protected:
      std::map<unsigned int, bool> first;
      std::map<unsigned int, int> contCounters;
      int contPAT;
      int contPMT;
      int contSDT;
      unsigned int packCounter; ///\todo update constructors?
      TS::Packet packData;
      bool haveAvcc;
      MP4::AVCC avccbox;
      bool appleCompat;
      /*LTS-START*/
      bool haveHvcc;
      MP4::HVCC hvccbox;
      /*LTS-END*/
      uint64_t sendRepeatingHeaders; ///< Amount of ms between PAT/PMT. Zero means do not repeat.
      uint64_t lastHeaderTime; ///< Timestamp last PAT/PMT were sent.
      uint64_t ts_from; ///< Starting time to subtract from timestamps
  };
}
