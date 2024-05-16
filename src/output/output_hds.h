#include "output_http.h"
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/ts_packet.h>

namespace Mist{
  class OutHDS : public HTTPOutput{
  public:
    OutHDS(Socket::Connection &conn);
    ~OutHDS();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();

  protected:
    void getTracks();
    std::string dynamicBootstrap(size_t idx);
    std::string dynamicIndex();
    std::set<size_t> videoTracks; ///<< Holds valid video tracks for playback
    size_t audioTrack;            ///<< Holds audio track ID for playback
    uint64_t playUntil;
    FLV::Tag tag;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutHDS mistOut;
#endif
