/// \file dtscfix.cpp
/// Contains the code that will attempt to fix the metadata contained in an DTSC file.

#include <string>
#include <mist/dtsc.h>
#include <mist/json.h>
#include <mist/config.h>
#include <mist/encryption.h>
#include <mist/base64.h>
#include <iomanip>
#include <ctime>
#include <cstdlib>

///\brief Holds everything unique to converters.
namespace Converters {

  std::string intToBin(long long int number){
    std::string result;
    result.resize(8);
    for( int i = 7; i >= 0; i--){
      result[i] = number & 0xFF;
      number >>= 8;
    }
    return result;
  }


  ///\brief Reads a DTSC file and attempts to fix the metadata in it.
  ///\param conf The current configuration of the program.
  ///\return The return code for the fixed program.
  int DTSCCrypt(Util::Config & conf){
    std::map<int,unsigned long long int> iVecs;
    srand(time(NULL));
    DTSC::File F(conf.getString("filename"));
    std::string key = Base64::decode(conf.getString("contentkey"));
    if (key == ""){
      std::string tmpSeed = Base64::decode(conf.getString("keyseed"));
      std::string tmpID = Base64::decode(conf.getString("keyid"));
      std::string guid = Encryption::PR_GuidToByteArray(tmpID);
      key = Encryption::PR_GenerateContentKey(tmpSeed, guid);
    }
    F.seek_bpos(0);
    F.parseNext();
    JSON::Value oriheader = F.getPacket().toJSON();
    DTSC::Meta meta = F.getMeta();
    ///\todo Add check for !ivecSize || keySize == ivecSize
    meta.reset();
    JSON::Value pack;

    if ( !oriheader.isMember("moreheader")){
      std::cerr << "This file is too old to encrypt - please reconvert." << std::endl;
      return 1;
    }

    DTSC::File outFile(conf.getString("outputfile"), true);
    meta.moreheader = 0ll;
    for (std::map<int,DTSC::Track>::iterator it = meta.tracks.begin(); it != meta.tracks.end(); it++){
      it->second.ivecs.clear();
    }
    std::string metaWrite = meta.toJSON().toPacked();
    outFile.writeHeader(metaWrite, true);

    F.parseNext();
    while (F.getPacket()){
      int tid = F.getPacket().getTrackId();
      if (tid){
        if (F.getPacket().hasMember("data")){
          JSON::Value tmp = F.getPacket().toJSON();
          std::string ivec;
          if (F.getPacket().hasMember("ivec")){
            F.getPacket().getString("ivec", ivec);
          }else{
            if (iVecs.find(tid) == iVecs.end()){
              iVecs[tid] = ((long long unsigned int)rand() << 32) + rand(); 
            }
            ivec = intToBin(iVecs[tid]);
            iVecs[tid] ++;
            tmp["ivec"] = ivec;
          }
          std::string data;
          F.getPacket().getString("data", data);
          std::string result;
          if(meta.tracks[tid].codec == "H264"){
            std::string toCrypt = data.substr(5);
            result = data.substr(0,5);
            result += Encryption::AES_Crypt(toCrypt, key, ivec);
            tmp["data"] = result;
            tmp.netPrepare();
            outFile.writePacket(tmp);
          }else{
            result = Encryption::AES_Crypt(data, key, ivec);
            tmp["data"] = result;
            tmp.netPrepare();
            outFile.writePacket(tmp);
          }
        }
      }
      F.parseNext();
    }
    return 0;
  } //DTSCCrypt
}

/// Entry point for DTSCCrypt, simply calls Converters::DTSCCrypt().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the file to attempt to fix.\"}"));
  conf.addOption("outputfile", JSON::fromString("{\"arg_num\":2, \"arg\":\"string\", \"help\":\"Filename of the outputfile.\"}"));
  conf.addOption("contentkey", JSON::fromString("{\"value\":[\"\"], \"short\":\"c\", \"long\":\"contentkey\", \"arg\":\"string\", \"help\":\"Base64-encoded version of the key to encrypt/decrypt with. Optional if both keyseed and keyid are given.\"}"));
  conf.addOption("keyseed", JSON::fromString("{\"value\":[\"\"], \"short\":\"s\", \"long\":\"keyseed\", \"arg\":\"string\", \"help\":\"Base64-encoded version of the keyseed through which to generate the contentkey. Optional if contentkey is given.\"}"));
  conf.addOption("keyid", JSON::fromString("{\"value\":[\"\"], \"short\":\"i\", \"long\":\"keyid\", \"arg\":\"string\", \"help\":\"Base64-encoded version of the keyid with which to generate the contentkey. Optional if contentkey is given.\"}"));
  conf.addOption("force", JSON::fromString("{\"short\":\"f\", \"long\":\"force\", \"default\":0, \"help\":\"Force fixing.\"}"));
  if ( !conf.parseArgs(argc, argv)){
    conf.printHelp(std::cout);
    return 1;
  }
  if (conf.getString("contentkey") == "" && (conf.getString("keyseed") == "" || conf.getString("keyid") == "")){
    std::cout << "Either the contentkey or both the keyseed and keyid must be provided." << std::endl;
    conf.printHelp(std::cout);
    return 1;
  }
  return Converters::DTSCCrypt(conf);
} //main
