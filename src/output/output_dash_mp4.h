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
      void sendHeader();
      void initialize();
    protected:
      std::string makeTime(long long unsigned int time);
      std::string buildManifest();
      void buildFtyp(unsigned int trackid);
      void buildStyp(unsigned int trackid);
      std::string buildMoov(unsigned int trackid);
      std::string buildSidx(unsigned int trackid);
      std::string buildSidx(unsigned int trackid, unsigned int keynum);
      std::string buildMoof(unsigned int trackid, unsigned int keynum);
      void buildMdat(unsigned int trackid, unsigned int keynum);
      std::map<unsigned int, std::map<unsigned int, long long unsigned int> > fragmentSizes;
      std::string buildNalUnit(unsigned int len, const char * data);
      void parseRange(std::string header, long long & byteStart, long long & byteEnd);
      int getKeyFromRange(unsigned int tid, long long int byteStart);
      std::map<int,std::string> moovBoxes;
  };
}

typedef Mist::OutDashMP4 mistOut;
