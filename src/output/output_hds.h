#include "output.h"
#include <mist/http_parser.h>
#include <mist/ts_packet.h>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>

namespace Mist {
  class OutHDS : public Output {
    public:
      OutHDS(Socket::Connection & conn);
      ~OutHDS();
      static void init(Util::Config * cfg);
      
      void onRequest();
      void onFail();
      void sendNext();
    protected:
      void getTracks();
      std::string dynamicBootstrap(int tid);
      std::string dynamicIndex();
      HTTP::Parser HTTP_S;
      HTTP::Parser HTTP_R;
      std::set<int> videoTracks;///<< Holds valid video tracks for playback
      long long int audioTrack;///<< Holds audio track ID for playback
      long long unsigned int playUntil;
      FLV::Tag tag;
  };
}

typedef Mist::OutHDS mistOut;
