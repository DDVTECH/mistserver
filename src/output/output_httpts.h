#include "output_ts_base.h"
#include "output_http.h"

namespace Mist {
  class OutHTTPTS : public TSOutput{
    public:
      OutHTTPTS(Socket::Connection & conn);
      ~OutHTTPTS();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendTS(const char * tsData, unsigned int len=188);
    protected:      
      int keysToSend;
      long long int playUntil;      
      long long unsigned int lastVid;
      long long unsigned int until;
      unsigned int vidTrack;
      unsigned int audTrack;
  };
}

typedef Mist::OutHTTPTS mistOut;
