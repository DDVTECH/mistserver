#include "output.h"

namespace Mist{
  class OutJSONLine : public Output{
  public:
    OutJSONLine(Socket::Connection & conn, Util::Config & cfg, JSON::Value & capa);
    ~OutJSONLine();
    static void init(Util::Config *cfg, JSON::Value & capa);
    void sendNext();
    static bool listenMode(Util::Config *config);
    bool isReadyForPlay();
    void requestHandler(bool readable);
  private:
    std::string getStatsName();
    Util::ResizeablePointer dPtr;
    size_t trkIdx;
    uint64_t offset;

  protected:
    inline virtual bool keepGoing() { return config->is_active && (!listenMode(config) || myConn); }
  };
}// namespace Mist

typedef Mist::OutJSONLine mistOut;
