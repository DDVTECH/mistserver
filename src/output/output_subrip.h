#include "output_http.h"

namespace Mist{
  class OutSubRip : public HTTPOutput{
  public:
    OutSubRip(Socket::Connection &conn);
    ~OutSubRip();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendNext();
    void sendHeader();

  protected:
    bool webVTT;
    size_t lastNum;
    uint32_t filter_from;
    uint32_t filter_to;
    uint32_t index;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutSubRip mistOut;
#endif
