#include "output_http.h"
#include <mist/http_parser.h>
#include <mist/ogg.h>

namespace Mist{
  class OutProgressiveOGG : public HTTPOutput{
  public:
    OutProgressiveOGG(Socket::Connection &conn);
    ~OutProgressiveOGG();
    static void init(Util::Config *cfg);
    void onRequest();
    void sendNext();
    void sendHeader();
    bool onFinish();
    bool parseInit(std::string &initData, std::deque<std::string> &output);

  protected:
    HTTP::Parser HTTP_R;                                    // Received HTTP
    HTTP::Parser HTTP_S;                                    // Sent HTTP
    std::map<long long unsigned int, OGG::Page> pageBuffer; // OGG specific variables
  };
}// namespace Mist

typedef Mist::OutProgressiveOGG mistOut;
