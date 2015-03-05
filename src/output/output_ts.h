#include "output_ts_base.h"

namespace Mist {
  class OutTS : public TSOutput{
    public:
      OutTS(Socket::Connection & conn);
      ~OutTS();
      static void init(Util::Config * cfg);
      void sendTS(const char * tsData, unsigned int len=188);       
  };
}

typedef Mist::OutTS mistOut;
