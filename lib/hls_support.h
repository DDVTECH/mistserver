#pragma once

#include "comms.h"
#include "dtsc.h"

namespace HLS {

  class Generator {
    public:
      Generator();
      void setParam(const std::string & name, const std::string & value);
      void setExt(const std::string & value);
      void setListLimit(uint64_t value);
      void setPartTarget(uint32_t value);
      void setUrlPrefix(const std::string & value);
      void setMuxed(bool value);

      std::string masterPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack);

      std::string subPlaylist(const DTSC::Meta & M, const std::map<size_t, Comms::Users> & userSelect, size_t mainTrack,
                              size_t requestedTrack = INVALID_TRACK_ID);

    protected:
      std::map<std::string, std::string> params;
      std::string ext; ///< File extension for segments
      std::string urlPrefix;
      uint64_t listLimit{0};
      uint32_t partTargetMs{500};
      bool muxed{false};
  };

} // namespace HLS
