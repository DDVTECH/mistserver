#include <iostream>
#include <mist/defines.h>
#include <sstream>
#include <string>
#include <unistd.h>

#include "analyser_dtsc.h"
#include <mist/config.h>
#include <mist/h264.h>
#include <mist/json.h>

dtscAnalyser::dtscAnalyser(Util::Config config) : analysers(config) {
  conn = Socket::Connection(fileno(stdout), fileno(stdin));
  std::cout << "connection initialized" << std::endl;

  F.reInit(conn);
  totalBytes = 0;

  //  F = DTSC::Packet(config.getString("filename"));
  if (!F) {
    std::cerr << "Not a valid DTSC file" << std::endl;
    mayExecute = false;
    return;
  }

  if (F.getVersion() == DTSC::DTSC_HEAD) // for meta
  {
    DTSC::Meta m(F);

    if (detail > 0) {
      JSON::Value result;
      for (std::map<unsigned int, DTSC::Track>::iterator it = m.tracks.begin(); it != m.tracks.end(); it++) {
        JSON::Value track;
        if (it->second.type == "video") {
          std::stringstream tStream;
          track["resolution"] = JSON::Value((long long)it->second.width).asString() + "x" + JSON::Value((long long)it->second.height).asString();
          track["fps"] = (long long)((double)it->second.fpks / 1000);
          track["fpks"] = it->second.fpks;
          tStream << it->second.bps * 8 << " b/s, " << (double)it->second.bps * 8 / 1024 << " kb/s, " << (double)it->second.bps * 8 / 1024 / 1024
                  << " mb/s";
          track["bitrate"] = tStream.str();
          tStream.str("");
          track["keyframe_duration"] = (long long)((float)(it->second.lastms - it->second.firstms) / it->second.keys.size());
          tStream << ((double)(it->second.lastms - it->second.firstms) / it->second.keys.size()) / 1000;
          track["keyframe_interval"] = tStream.str();

          tStream.str("");
          if (it->second.codec == "H264") {
            h264::sequenceParameterSet sps;
            sps.fromDTSCInit(it->second.init);
            h264::SPSMeta spsData = sps.getCharacteristics();
            track["encoding"]["width"] = spsData.width;
            track["encoding"]["height"] = spsData.height;
            tStream << spsData.fps;
            track["encoding"]["fps"] = tStream.str();
            track["encoding"]["profile"] = spsData.profile;
            track["encoding"]["level"] = spsData.level;
          }
        }
        if (it->second.type == "audio") {
          std::stringstream tStream;
          tStream << it->second.bps * 8 << " b/s, " << (double)it->second.bps * 8 / 1024 << " kb/s, " << (double)it->second.bps * 8 / 1024 / 1024
                  << " mb/s";
          track["bitrate"] = tStream.str();
          track["keyframe_interval"] = (long long)((float)(it->second.lastms - it->second.firstms) / it->second.keys.size());
        }
        result[it->second.getWritableIdentifier()] = track;
      }
      std::cout << result.toString();
    }

    if (m.vod || m.live) { m.toPrettyString(std::cout, 0, 0x03); }
  }
}

bool dtscAnalyser::packetReady() {
  return (F.getDataLen() > 0);
}

bool dtscAnalyser::hasInput() {
  return F;
}

int dtscAnalyser::doAnalyse() {
  if (analyse) { // always analyse..?
    switch (F.getVersion()) {
    case DTSC::DTSC_V1: {
      std::cout << "DTSCv1 packet: " << F.getScan().toPrettyString() << std::endl;
      break;
    }
    case DTSC::DTSC_V2: {
      std::cout << "DTSCv2 packet (Track " << F.getTrackId() << ", time " << F.getTime() << "): " << F.getScan().toPrettyString() << std::endl;
      break;
    }
    case DTSC::DTSC_HEAD: {
      std::cout << "DTSC header: " << F.getScan().toPrettyString() << std::endl;
      break;
    }
    case DTSC::DTCM: {
      std::cout << "DTCM command: " << F.getScan().toPrettyString() << std::endl;
      break;
    }
    default: DEBUG_MSG(DLVL_WARN, "Invalid dtsc packet @ bpos %llu", totalBytes); break;
    }
  }

  totalBytes += F.getDataLen();

  F.reInit(conn); 
  return totalBytes;
}

int main(int argc, char **argv) {
  Util::Config conf = Util::Config(argv[0]);

  analysers::defaultConfig(conf);
  conf.parseArgs(argc, argv);

  dtscAnalyser A(conf);

  A.Run();

  return 0;
}
