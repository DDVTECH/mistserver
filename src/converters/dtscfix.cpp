/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {
  ///\brief Reads a DTSC file and attempts to fix the metadata in it.
  ///\param conf The current configuration of the program.
  ///\return The return code for the fixed program.
  int DTSCFix(Util::Config & conf){
    DTSC::File F(conf.getString("filename"));
    F.seek_bpos(0);
    F.parseNext();
    JSON::Value oriheader = F.getPacket().toJSON();
    DTSC::Meta meta(F.getMeta());

    if (meta.isFixed() && !conf.getBool("force")){
      std::cerr << "This file was already fixed or doesn't need fixing - cancelling." << std::endl;
      return 0;
    }

    meta.reset();
    int bPos = F.getBytePos();
    F.parseNext();
    JSON::Value newPack;
    while ( F.getPacket()){
      newPack = F.getPacket().toJSON();
      newPack["bpos"] = bPos;
      meta.update(newPack);
      bPos = F.getBytePos();
      F.parseNext();
    }
    int temp = 0;
    for (std::map<int,DTSC::Track>::iterator it = meta.tracks.begin(); it != meta.tracks.end(); it++){
      temp++;
      if (it->second.fragments.size()){
        it->second.fragments.rbegin()->setDuration(it->second.fragments.rbegin()->getDuration() - it->second.lastms);
      }
    }

    //append the revised header
    std::string loader = meta.toJSON().toPacked();
    oriheader["moreheader"] = F.addHeader(loader);
    if ( !oriheader["moreheader"].asInt()){
      std::cerr << "Failure appending new header." << std::endl;
      return -1;
    }
    //re-write the original header with information about the location of the new one
    loader = oriheader.toPacked();
    if (F.writeHeader(loader)){
      return 0;
    }else{
      std::cerr << "Failure rewriting header." << std::endl;
      return -1;
    }
  } //DTSCFix
}

/// Entry point for DTSCFix, simply calls Converters::DTSCFix().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the file to attempt to fix.\"}"));
  conf.addOption("force", JSON::fromString("{\"short\":\"f\", \"long\":\"force\", \"default\":0, \"help\":\"Force fixing.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSCFix(conf);
} //main
