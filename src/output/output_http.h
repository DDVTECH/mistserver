#pragma once
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <mist/websocket.h>
#include "output.h"

namespace Mist {

  class HTTPOutput : public Output {
    public:
      HTTPOutput(Socket::Connection & conn);
      virtual ~HTTPOutput();
      static void init(Util::Config * cfg);
      void onRequest();
      virtual void onFail(const std::string & msg, bool critical = false);
      virtual void onHTTP(){};
      virtual void onIdle(){};
      virtual void onWebsocketFrame(){};
      virtual void onWebsocketConnect(){};
      virtual void preWebsocketConnect(){};
      virtual void requestHandler();
      virtual void preHTTP();
      static bool listenMode(){return false;}
      virtual bool doesWebsockets(){return false;}
      void reConnector(std::string & connector);
      std::string getHandler();
      bool parseRange(uint64_t & byteStart, uint64_t & byteEnd);
  protected:
      HTTP::Parser H;
      HTTP::Websocket * webSock;
      uint32_t idleInterval;
      uint64_t idleLast;
      std::string getConnectedHost();//LTS
      std::string getConnectedBinHost();//LTS
      bool isTrustedProxy(const std::string & ip);//LTS
  };
}
