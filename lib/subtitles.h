#pragma once
#include <string>
#include "dtsc.h"

namespace Subtitle {
 
  struct Packet {
    std::string subtitle;
    uint64_t duration;
  };

  Packet getSubtitle(DTSC::Packet packet, DTSC::Meta meta);

}

