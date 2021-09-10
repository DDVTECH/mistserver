#pragma once
#include "dtsc.h"
#include <string>

namespace Subtitle{

  struct Packet{
    std::string subtitle;
    uint64_t duration;
  };

  Packet getSubtitle(DTSC::Packet packet, DTSC::Meta meta);

}// namespace Subtitle
