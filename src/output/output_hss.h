#include "output_http.h"
#include <mist/http_parser.h>

namespace Mist {
  class OutHSS : public HTTPOutput {
    public:
      OutHSS(Socket::Connection & conn);
      ~OutHSS();
      static void init(Util::Config * cfg);
      void onHTTP();
      void sendNext();
      void sendHeader();
    protected:
      std::string protectionHeader();/*LTS*/
      std::string smoothIndex();
      void loadEncryption();/*LTS*/
      int canSeekms(unsigned int ms);
      int keysToSend;
      int myTrackStor;
      int myKeyStor;
      unsigned long long playUntil;
  };
}

typedef Mist::OutHSS mistOut;
