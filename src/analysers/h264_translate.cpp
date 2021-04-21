#include <cstdio>
#include <iostream>
#include <string>

#include <mist/bitfields.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/h264.h>

///\brief Holds everything unique to the analysers.
namespace Analysers{
  int analyseH264(Util::Config conf){
    FILE *F = fopen(conf.getString("filename").c_str(), "r+b");
    if (!F){FAIL_MSG("No such file");}

    h264::nalUnit *nalPtr = h264::nalFactory(F);
    while (nalPtr){
      if (nalPtr->getType() == 0x07){
        Utils::bitstream br;
        br << nalPtr->payload;

        Utils::bitWriter bw;
        bw.append(br.get(8), 8); // nalType
        bw.append(br.get(8), 8); // profile_idc
        bw.append(br.get(8), 8); // constraint flags
        bw.append(br.get(8), 8); // level_idc

        br.getUExpGolomb();
        bw.appendUExpGolomb(0); // seq_parameter_set_id

        while (br.size() >= 64){bw.append(br.get(64), 64);}
        size_t remainder = br.size();
        if (remainder){bw.append(br.get(remainder), remainder);}
        nalPtr->payload = bw.str();
      }else if (nalPtr->getType() == 0x08){
        Utils::bitstream br;
        br << nalPtr->payload;

        Utils::bitWriter bw;
        bw.append(br.get(8), 8); // nalType
        br.getUExpGolomb();
        bw.appendUExpGolomb(0); // pic_parameter_set_id
        br.getUExpGolomb();
        bw.appendUExpGolomb(0); // seq_parameter_set_id

        while (br.size() >= 64){bw.append(br.get(64), 64);}
        size_t remainder = br.size();
        if (remainder){bw.append(br.get(remainder), remainder);}
        nalPtr->payload = bw.str();
      }else if (nalPtr->getType() == 0x01 || nalPtr->getType() == 0x05 || nalPtr->getType() == 0x19){
        Utils::bitstream br;
        br << nalPtr->payload;
        Utils::bitWriter bw;
        bw.append(br.get(8), 8);                 // nalType
        bw.appendUExpGolomb(br.getUExpGolomb()); // first_mb_in_slice
        bw.appendUExpGolomb(br.getUExpGolomb()); // slice_type
        br.getUExpGolomb();
        bw.appendUExpGolomb(0); // pic_parameter_set_id
        while (br.size() >= 64){bw.append(br.get(64), 64);}
        size_t remainder = br.size();
        if (remainder){bw.append(br.get(remainder), remainder);}
        nalPtr->payload = bw.str();
      }
      nalPtr->write(std::cout);
      delete nalPtr;
      nalPtr = h264::nalFactory(F);
    }
    return 0;
  }
}// namespace Analysers

int main(int argc, char **argv){
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Full "
                                              "path of the file to analyse.\"}"));
  conf.parseArgs(argc, argv);
  return Analysers::analyseH264(conf);
}
