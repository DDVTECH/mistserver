#include "subtitles.h"
#include "bitfields.h"

#include "defines.h"
namespace Subtitle {
  
  Packet getSubtitle(DTSC::Packet packet, DTSC::Meta meta) {
    char * tmp = 0;
    uint16_t length = 0;
    unsigned int len;

    Packet output;
    long int trackId= packet.getTrackId();
    if(meta.tracks[trackId].codec != "TTXT" && meta.tracks[trackId].codec != "SRT") {
      //no subtitle track
      return output;
    }

    if(packet.hasMember("duration")) {
      output.duration = packet.getInt("duration");
    } else {
      //get parts from meta
      //calculate duration

    }
    
    packet.getString("data", output.subtitle);
    if(meta.tracks[trackId].codec == "TTXT") {
      unsigned short size = Bit::btohs(output.subtitle.c_str());
      output.subtitle = output.subtitle.substr(2,size);
    }

    return output;
  } 



}
