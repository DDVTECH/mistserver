#include "output_http.h"
#include "output_ts_base.h"

namespace Mist{
  class OutHLS : public TSOutputHTTP{
  public:
    OutHLS(Socket::Connection &conn);
    ~OutHLS();
    static void init(Util::Config *cfg);
    void sendTS(const char *tsData, size_t len = 188);
    void sendNext();
    void onHTTP();
    bool isReadyForPlay();
    virtual void onFail(const std::string &msg, bool critical = false);
    virtual std::string getStatsName(){return Output::getStatsName();}

  protected:
    std::string h264init(const std::string &initData);
    std::string h265init(const std::string &initData);
    std::string liveIndex();
    std::string liveIndex(size_t tid, const std::string &sessId, const std::string &urlPrefix = "");

    size_t vidTrack;
    size_t audTrack;
    uint64_t until;
  };
}// namespace Mist

#ifndef ONE_BINARY
typedef Mist::OutHLS mistOut;
#endif
