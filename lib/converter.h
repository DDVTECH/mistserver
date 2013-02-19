#include <map>
#include <string>

#include "json.h"

typedef std::map<std::string,std::string> codecInfo;
typedef std::map<std::string,codecInfo> converterInfo;

namespace Converter {
  class Converter {
    public:
      Converter();
      converterInfo & getCodecs();
    private:
      void fillFFMpegEncoders();
      converterInfo allCodecs;
  };
}
