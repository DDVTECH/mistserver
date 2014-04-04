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
      std::string protectionHeader(JSON::Value & encParams);/*LTS*/
      /*LTS
      std::string smoothIndex();
      LTS*/
      std::string smoothIndex(JSON::Value encParams = JSON::Value());/*LTS*/
      int canSeekms(unsigned int ms);
      int keysToSend;
      int myTrackStor;
      int myKeyStor;
      long long int playUntil;
  };
}

typedef Mist::OutHSS mistOut;
