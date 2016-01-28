#include <mist/socket.h>

#include "output.h"

namespace Mist {
  class OutPush : public Output {
    public:
      OutPush(Socket::Connection & conn);
      ~OutPush();
      static bool listenMode(){return false;}
      virtual void requestHandler();
      static void init(Util::Config * cfg);
    protected:
      Socket::Connection listConn;
      std::string pushURL;
  };
}

typedef Mist::OutPush mistOut;
