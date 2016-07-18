#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <signal.h>
#include <string.h>
#include <string>
#include <unistd.h>
#include <vector>

#include <mist/config.h>
#include <mist/defines.h>
#include <mist/mp4.h>
#include <mist/timing.h>
#include <sys/sysinfo.h>

#include "analyser_mp4.h"

mp4Analyser::mp4Analyser(Util::Config config) : analysers(config) {

  curPos = 0;
  dataSize = 0;
}

int mp4Analyser::doAnalyse() {
  DEBUG_MSG(DLVL_DEVEL, "Read a box at position %d", curPos);
  std::cerr << mp4Data.toPrettyString(0) << std::endl;

  return dataSize; // endtime?
}

bool mp4Analyser::hasInput() {
  if (!std::cin.good()) { return false; }
  mp4Buffer += std::cin.get();
  dataSize++;

  if (!std::cin.good()) {
    mp4Buffer.erase(mp4Buffer.size() - 1, 1);
    dataSize--;
  }

  return true;
}

bool mp4Analyser::packetReady() {
  return mp4Data.read(mp4Buffer);
}

mp4Analyser::~mp4Analyser() {
  INFO_MSG("Stopped parsing at position %d", curPos);
}

int main(int argc, char **argv) {
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filter", JSON::fromString("{\"arg\":\"num\", \"short\":\"f\", \"long\":\"filter\", \"default\":0, \"help\":\"Only print info "
                                            "about this tag type (8 = audio, 9 = video, 0 = all)\"}"));
  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to "
                                          "do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("filename",
                 JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"default\":\"\", \"help\":\"Filename of the FLV file to analyse.\"}"));

  conf.parseArgs(argc, argv);

  mp4Analyser A(conf);
  // FlvAnalyser A(conf);

  A.Run();

  return 0;
}
