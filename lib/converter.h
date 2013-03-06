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
      JSON::Value queryPath(std::string myPath);
      void startConversion(std::string name, JSON::Value parameters);
      void updateStatus();
      JSON::Value getStatus();
      void clearStatus();
    private:
      void fillFFMpegEncoders();
      converterInfo allCodecs;
      std::map<std::string,JSON::Value> allConversions;
      std::map<std::string,std::string> statusHistory;
  };
}
