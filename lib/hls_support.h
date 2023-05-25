#include "comms.h"
#include "dtsc.h"
#include <cmath>

namespace HLS {

  class Generator {
    public:
      Generator();
      void setParam(const std::string & name, const std::string & value);
      void setExt(const std::string & value);
      void setListLimit(uint64_t value);

      std::string masterPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack);

      std::string subPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack);

    protected:
      std::map<std::string, std::string> params;
      std::string ext; ///< File extension for segments
      uint64_t listLimit;
  };

} // namespace HLS
