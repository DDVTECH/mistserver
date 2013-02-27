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
      JSON::Value getEncoders();
    private:
      void fillFFMpegEncoders();
      converterInfo allCodecs;
  };
}
