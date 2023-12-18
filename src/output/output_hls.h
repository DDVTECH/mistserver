#include "output_http.h"
#include "output_ts_base.h"

namespace Mist{
  class OutHLS : public TSOutput{
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
    static bool listenMode();
    virtual void stats(bool force = false);
    // Prevent selecting tracks early
    virtual void preHTTP(){}
  protected:
    std::string h264init(const std::string &initData);
    std::string h265init(const std::string &initData);
    std::string liveIndex();
    std::string liveIndex(size_t tid, const std::string &sessId, const std::string &urlPrefix = "");

    uint64_t until;
    bool CDNMode;
  };
}// namespace Mist

typedef Mist::OutHLS mistOut;
