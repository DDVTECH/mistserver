#include "output.h"
#include <mist/http_parser.h>

namespace Mist {
  class OutHSS : public Output {
    public:
      OutHSS(Socket::Connection & conn);
      ~OutHSS();
      static void init(Util::Config * cfg);
      
      void onRequest();
      void sendNext();
      void initialize();
      void onFail();
      void sendHeader();
    protected:
      HTTP::Parser HTTP_S;
      HTTP::Parser HTTP_R;
      JSON::Value encryption;
      std::string smoothIndex();
      int canSeekms(unsigned int ms);
      int keysToSend;
      int myTrackStor;
      int myKeyStor;
      unsigned long long playUntil;
  };
}

typedef Mist::OutHSS mistOut;
