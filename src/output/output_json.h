#include "output.h"


namespace Mist {
  class OutJSON : public Output {
    public:
      OutJSON(Socket::Connection & conn);
      ~OutJSON();
      static void init(Util::Config * cfg);
      void onRequest();
      bool onFinish();
      void sendNext();
      void sendHeader();
    protected:
      std::string jsonp;
      bool first;
  };
}

typedef Mist::OutJSON mistOut;
