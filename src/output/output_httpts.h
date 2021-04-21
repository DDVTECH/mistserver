#include "output_http.h"
#include "output_ts_base.h"

namespace Mist{
  class OutHTTPTS : public TSOutput{
  public:
    OutHTTPTS(Socket::Connection &conn);
    ~OutHTTPTS();
    static void init(Util::Config *cfg);
    void onHTTP();
    void sendTS(const char *tsData, size_t len = 188);
    void initialSeek();

  private:
    bool isRecording();
    bool isFileTarget(){
      return isRecording() && config->getString("target").substr(0, 8) != "ts-exec:";
    }
  };
}// namespace Mist

typedef Mist::OutHTTPTS mistOut;
