/// \file dtscfix.cpp
/// Contains the code that will attempt to merge 2 files into a single DTSC file.

#include <string>
#include <vector>
#include <mist/config.h>
#include <mist/dtsc.h>

namespace Converters {
  int DTSCMerge(int argc, char ** argv){
    Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
    conf.addOption("output", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the output file.\"}"));
    conf.addOption("input", JSON::fromString("{\"arg_num\":2, \"arg\":\"string\", \"help\":\"Filename of the first input file.\"}"));
    conf.addOption("[additional_inputs ...]", JSON::fromString("{\"arg_num\":3, \"arg\":\"string\", \"help\":\"Filenames of any number of aditional inputs.\"}"));
    conf.parseArgs(argc, argv);

    DTSC::File outFile("/dev/null",true);
    JSON::Value meta;
    std::map<std::string,std::map<int, int> > trackMapping;
    int nextTrackID = 1;

    bool fullSort = true;
    std::map<std::string, DTSC::File> inFiles;
    std::string tmpFileName;
    for (int i = 2; i < argc; i++){
      tmpFileName = argv[i];
      if (tmpFileName == std::string(argv[1])){
        fullSort = false;
      }else{
        inFiles.insert(std::pair<std::string, DTSC::File>(tmpFileName, DTSC::File(tmpFileName)));
      }
    }
    if (fullSort){
      outFile = DTSC::File(argv[1], true);
    }else{
      outFile = DTSC::File(argv[1]);
      meta = outFile.getMeta();
    }
    JSON::Value newMeta = meta;

    for (std::map<std::string,DTSC::File>::iterator it = inFiles.begin(); it != inFiles.end(); it++){
      JSON::Value tmpMeta = it->second.getMeta();
      //for (JSON::ObjIter oIt = 
    }

    return 0;
  }
}

int main(int argc, char ** argv){
  return Converters::DTSCMerge(argc, argv);
}
