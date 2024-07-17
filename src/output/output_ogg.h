#include "output_http.h"
#include <mist/http_parser.h>
#include <mist/ogg.h>

namespace Mist{
  class OutOGG : public HTTPOutput{
  public:
    OutOGG(Socket::Connection &conn);
    ~OutOGG();
    static void init(Util::Config *cfg);
    void onRequest();
    void sendNext();
    void sendHeader();
    bool onFinish();
    bool parseInit(const std::string &initData, std::deque<std::string> &output);

  protected:
    HTTP::Parser HTTP_R;                    // Received HTTP
    HTTP::Parser HTTP_S;                    // Sent HTTP
    std::map<size_t, OGG::Page> pageBuffer; // OGG specific variables
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutOGG mistOut;
#endif
