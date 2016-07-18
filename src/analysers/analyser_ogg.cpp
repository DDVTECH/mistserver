#include <string>
#include "analyser_ogg.h"

oggAnalyser::oggAnalyser(Util::Config config) : analysers(config)
{
  std::cout << "ogg constr" << std::endl;

  filter = conf.getInteger("filter");
  FLV::Tag flvData; // Temporary storage for incoming FLV data.
  endTime = 0;

}

void oggAnalyser::doValidate()
{
  std::cout << upTime << ", " << finTime << ", " << (finTime-upTime) << ", " << flvData.tagTime() << std::endl;
}

bool oggAnalyser::packetReady()
{
  return flvData.FileLoader(stdin);
}

int oggAnalyser::doAnalyse()
{
  if (analyse){ //always analyse..?
    if (!filter || filter == flvData.data[0]){
      std::cout << "[" << flvData.tagTime() << "+" << flvData.offset() << "] " << flvData.tagType() << std::endl;
    }
  }
  endTime = flvData.tagTime();

  return endTime;
}

int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filter", JSON::fromString("{\"arg\":\"num\", \"short\":\"f\", \"long\":\"filter\", \"default\":0, \"help\":\"Only print info about this tag type (8 = audio, 9 = video, 0 = all)\"}"));
  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("filename", JSON::fromString( "{\"arg_num\":1, \"arg\":\"string\", \"default\":\"\", \"help\":\"Filename of the FLV file to analyse.\"}"));
  conf.parseArgs(argc, argv);

  oggAnalyser A(conf);

  A.Run();

  return 0;
}

