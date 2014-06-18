#include <map>
#include <string>

#include "json.h"

///\brief A typedef to simplify accessing all codecs
typedef std::map<std::string, std::string> codecInfo;
///\brief A typedef to simplify accessing all encoders
typedef std::map<std::string, codecInfo> converterInfo;

///\brief A namespace containing all functions for handling the conversion API
namespace Converter {

  ///\brief A class containing the basic conversion API functionality
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
      JSON::Value parseFFMpegStatus(std::string statusLine);
    private:
      void fillFFMpegEncoders();
      ///\brief Holds a list of all current known codecs
      converterInfo allCodecs;
      ///\brief Holds a list of all the current conversions
      std::map<std::string, JSON::Value> allConversions;
      ///\brief Stores the status of all conversions, and the history
      std::map<std::string, std::string> statusHistory;
  };
}
