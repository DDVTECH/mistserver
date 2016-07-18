#include "analyser.h"
#include <iostream>
#include <mist/defines.h>
#include <mist/timing.h>

analysers::analysers(Util::Config &config) {
  conf = config;

  //set default detailLevel
  detail = 2;

  if (conf.hasOption("filename")) {
    fileinput = conf.getString("filename").length() > 0;
  } else {
    fileinput = 0;
  }

  analyse = conf.getString("mode") == "analyse";
  validate = conf.getString("mode") == "validate";

  if (validate) {
    // conf.getOption("detail", true)[1] = 0ll;
    detail = 0;
  }

  if (conf.hasOption("detail")) { detail = conf.getInteger("detail"); }

  Prepare();
}

int analysers::doAnalyse() {
  return 0;
}

analysers::~analysers() {
}

void analysers::doValidate() {
  std::cout << upTime << ", " << finTime << ", " << (finTime - upTime) << ", " << endTime << std::endl;
}

bool analysers::packetReady() {
  return false;
}

// Fileinput as stdin
void analysers::Prepare() {

  if (fileinput) {
    filename = conf.getString("filename");
    int fp = open(filename.c_str(), O_RDONLY);

    if (fp <= 0) { 
      FAIL_MSG("Cannot open file: %s", filename.c_str()); 
    }

    dup2(fp, STDIN_FILENO);
    close(fp);
  }
}

bool analysers::hasInput() {
  //  std::cout << std::cin.good() << std::endl;

  return std::cin.good();
  //  return !feof(stdin);
}

int analysers::Run() {

  if(mayExecute)
  {
    std::cout << "start analyser with detailLevel: " << detail << std::endl;
    endTime = 0;
    upTime = Util::bootSecs();

    while (hasInput() && mayExecute) {
      while (packetReady()) {
        // std::cout << "in loop..." << std::endl;
        endTime = doAnalyse();
      }
    }

    finTime = Util::bootSecs();

    if (validate) {
      // std::cout << upTime << ", " << finTime << ", " << (finTime-upTime) << ", " << endTime << std::endl;
      doValidate();
    }
  }
  return 0;
}

void analysers::defaultConfig(Util::Config &conf) {
  conf.addOption("filename",
                 JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"default\":\"\", \"help\":\"Filename of the file to analysed.\"}"));

  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to "
                                          "do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));

  conf.addOption(
      "detail",
      JSON::fromString("{\"long\":\"detail\", \"short\":\"D\", \"arg\":\"num\", \"default\":2, \"help\":\"Detail level of analysis. \"}"));
}
