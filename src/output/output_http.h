#include <mist/defines.h>
#include <mist/http_parser.h>
#include "output.h"

namespace Mist {

  class HTTPOutput : public Output {
    public:
      HTTPOutput(Socket::Connection & conn);
      virtual ~HTTPOutput(){};
      static void init(Util::Config * cfg);
      void onRequest();
      void onFail();
      virtual void onHTTP(){};
      virtual void requestHandler();
      static bool listenMode(){return false;}
      void reConnector(std::string & connector);
      std::string getHandler();
  protected:
      HTTP::Parser H;
  };
}
