#include "output_http.h"
#include <mist/mp4_generic.h>
#include <mist/http_parser.h>

namespace Mist {
  class OutDashMP4 : public HTTPOutput {
    public:
      OutDashMP4(Socket::Connection & conn);
      ~OutDashMP4();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader(){};
    protected:
      void addSegmentTimeline(std::stringstream & r, DTSC::Track & Trk, bool live);
      std::string makeTime(uint64_t time);
      std::string buildManifest();
      void sendMoov(uint32_t trackid);
      void sendMoof(uint32_t trackid, uint32_t fragIndice);
      void sendMdat(uint32_t trackid, uint32_t fragIndice);
      std::string buildNalUnit(unsigned int len, const char * data);
      uint64_t targetTime;

      std::string h264init(const std::string & initData);
      std::string h265init(const std::string & initData);
  };
}

typedef Mist::OutDashMP4 mistOut;
