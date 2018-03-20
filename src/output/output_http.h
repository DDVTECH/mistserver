#pragma once
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
      virtual void onFail();
      virtual void onHTTP(){};
      virtual void requestHandler();
      virtual void preHTTP();
      static bool listenMode(){return false;}
      void reConnector(std::string & connector);
      std::string getHandler();
      bool parseRange(uint64_t & byteStart, uint64_t & byteEnd);
  protected:
      HTTP::Parser H;
      std::string getConnectedHost();//LTS
      std::string getConnectedBinHost();//LTS
      bool isTrustedProxy(const std::string & ip);//LTS
  };
}
