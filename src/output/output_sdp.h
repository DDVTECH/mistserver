#include "output_http.h"
#include <mist/h264.h>
#include <mist/http_parser.h>
#include <mist/rtp.h>
#include <mist/sdp.h>
#include <mist/socket.h>
#include <mist/url.h>

namespace Mist{
  class OutSDP : public HTTPOutput{
  public:
    OutSDP(Socket::Connection &conn);
    ~OutSDP();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();
    bool onFinish();
    std::string getConnectedHost();
    std::string getConnectedBinHost();

  private:
    void initTracks(uint32_t & port, std::string targetIP);
    void checkForRTCP(uint64_t thisIdx);
    std::string generateSDP(std::string targetAddress, std::string streamName);
    SDP::State sdpState;
    uint32_t prevRTCP;
    bool exitOnNoRTCP;
    bool isFileTarget(){
      INFO_MSG("Checking file target! %s", isRecording()?"yes":"no");
      return isRecording();
    }
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutSDP mistOut;
#endif
