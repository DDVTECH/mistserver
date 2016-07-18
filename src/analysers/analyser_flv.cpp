#include "analyser_flv.h"
#include <mist/config.h>
#include <mist/defines.h>
#include <string>

flvAnalyser::flvAnalyser(Util::Config config) : analysers(config) {
  
  if(fileinput && open(filename.c_str(), O_RDONLY) <= 0)
  {
    mayExecute = false;
    return;
  }

  filter = conf.getInteger("filter");
  FLV::Tag flvData; // Temporary storage for incoming FLV data.


  //check for flv data
  char flvHeader[3];
  std::cin.read(flvHeader,3);
  
  if(flvHeader[0] != 0x46 || flvHeader[1] != 0x4C || flvHeader[2] != 0x56)
  {
    FAIL_MSG("No FLV Signature found!");
    mayExecute = false;
    return;
  }
  std::cin.seekg(0);
}

/*
void flvAnalyser::doValidate()
{
  std::cout << "dfasdfsdafdsf" << std::endl;
  std::cout << upTime << ", " << finTime << ", " << (finTime-upTime) << ", " << flvData.tagTime() << std::endl;
}
*/

bool flvAnalyser::hasInput() {
  return !feof(stdin);
}

bool flvAnalyser::packetReady() {
  return flvData.FileLoader(stdin);
}

int flvAnalyser::doAnalyse() {
  //  std::cout<< "do analyse" << std::endl;

  if (analyse) { // always analyse..?
    if (!filter || filter == flvData.data[0]) {
      std::cout << "[" << flvData.tagTime() << "+" << flvData.offset() << "] " << flvData.tagType() << std::endl;
    }
  }
  endTime = flvData.tagTime();

  return endTime;
}

int main(int argc, char **argv) {
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filter", JSON::fromString("{\"arg\":\"num\", \"short\":\"f\", \"long\":\"filter\", \"default\":0, \"help\":\"Only print info "
                                            "about this tag type (8 = audio, 9 = video, 0 = all)\"}"));

  analysers::defaultConfig(conf);
  conf.parseArgs(argc, argv);

  flvAnalyser A(conf);

  A.Run();

  return 0;
}
