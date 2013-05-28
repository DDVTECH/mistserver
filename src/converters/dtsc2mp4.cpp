/// \file dtsc2mp4.cpp
/// Contains the code that will transform any valid DTSC input into valid MP4s.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <mist/json.h>
#include <mist/dtsc.h> //DTSC support
#include <mist/mp4.h> //MP4 support
#include <mist/ts_packet.h> //TS support ff
#include <mist/config.h>

///\brief Holds everything unique to converters.
namespace Converters {

  ///\brief Converts DTSC from file to MP4 on stdout.
  ///\return The return code for the converter.
  int DTSC2MP4(Util::Config & conf){
    DTSC::File input(conf.getString("filename"));
    //ftyp box
    /// \todo fill ftyp with non hardcoded values from file
    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand(0x69736f6d);
    ftypBox.setMinorVersion(512);
    ftypBox.setCompatibleBrands(0x69736f6d,0);
    ftypBox.setCompatibleBrands(0x69736f32,1);
    ftypBox.setCompatibleBrands(0x61766331,2);
    ftypBox.setCompatibleBrands(0x6d703431,3);
    std::cout << std::string(ftypBox.asBox(),ftypBox.boxedSize());
    
    
    //moov box
    MP4::MOOV moovBox;
      MP4::MVHD mvhdBox;
      moovBox.setContent(mvhdBox, 0);
      
      //start arbitrary track addition
      int boxOffset = 1;
      input.getMeta()["tracks"]["audio0"] = input.getMeta()["audio"];
      input.getMeta()["tracks"]["audio0"]["type"] = "audio";
      input.getMeta()["tracks"]["audio0"]["trackid"] = 1;
      input.getMeta()["tracks"]["video0"] = input.getMeta()["video"];
      input.getMeta()["tracks"]["video0"]["keylen"] = input.getMeta()["keylen"];
      input.getMeta()["tracks"]["video0"]["trackid"] = 2;
      input.getMeta()["tracks"]["video0"]["type"] = "video";
      for (JSON::ObjIter it = input.getMeta()["tracks"].ObjBegin(); it != input.getMeta()["tracks"].ObjEnd(); it++){
        MP4::TRAK trakBox;
          MP4::TKHD tkhdBox;
          //std::cerr << it->second["trackid"].asInt() << std::endl;
          tkhdBox.setTrackID(it->second["trackid"].asInt());
          
          if (it->second["type"].asString() == "video"){
            tkhdBox.setWidth(it->second["width"].asInt() << 16);
            tkhdBox.setHeight(it->second["height"].asInt() << 16);
          }
          trakBox.setContent(tkhdBox, 0);
          
          MP4::MDIA mdiaBox;
            MP4::MDHD mdhdBox;
            mdiaBox.setContent(mdhdBox, 0);
            
            MP4::HDLR hdlrBox;
            mdiaBox.setContent(hdlrBox, 1);
            MP4::MINF minfBox;
              MP4::DINF dinfBox;
                MP4::DREF drefBox;
                dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,0);
              
              MP4::STBL stblBox;
                MP4::STSD stsdBox;
                  /*std::string tmpStr = it->second["type"].asString();
                  if (tmpStr == "video"){//boxname = codec
                    MP4::VisualSampleEntry vse;
                    std::string tmpStr2 = it->second["codec"];
                    if (tmpStr2 == "H264"){
                      vse.setCodec("avc1");
                    }
                    vse.setWidth(it->second["width"].asInt());
                    vse.setHeight(it->second["height"].asInt());
                    stsdBox.setEntry(vse,0);
                  }else if(tmpStr == "audio"){//boxname = codec
                    MP4::AudioSampleEntry ase;
                    std::string tmpStr2 = it->second["codec"];
                    if (tmpStr2 == "AAC"){
                      ase.setCodec("aac ");
                    }
                    ase.setSampleRate(it->second["rate"].asInt());
                    ase.setChannelCount(it->second["channels"].asInt());
                    ase.setSampleSize(it->second["length"].asInt());
                    stsdBox.setEntry(ase,0);
                  }*/
                stblBox.setContent(stsdBox,0);

                MP4::STTS sttsBox;
                for (int i = 0; i < it->second["keylen"].size(); i++){
                  MP4::STTSEntry newEntry;
                  newEntry.sampleCount = 1;
                  newEntry.sampleDelta = it->second["keylen"][i].asInt();
                  sttsBox.setSTTSEntry(newEntry, i);
                }
                stblBox.setContent(sttsBox,1);

                MP4::STSC stscBox;
                for (int i = 0; i < it->second["keylen"].size(); i++){
                  MP4::STSCEntry newEntry;
                  newEntry.firstChunk = i;
                  newEntry.samplesPerChunk = 1;
                  newEntry.sampleDescriptionIndex = i;
                  stscBox.setSTSCEntry(newEntry, i);
                }
                  
                stblBox.setContent(stscBox,2);

                MP4::STSZ stszBox;
                /// \todo calculate byte position of DTSCkeyframes in MP4Sample
                stszBox.setSampleSize(0);
                for (int i = 0; i < it->second["keylen"].size(); i++){
                  stszBox.setEntrySize(0, i);
                }
                stblBox.setContent(stszBox,3);
                  
                MP4::STCO stcoBox;
                for (int i = 0; i < it->second["keylen"].size(); i++){
                  stcoBox.setChunkOffset(0, i);
                }
                stblBox.setContent(stcoBox,4);
              minfBox.setContent(stblBox,1);
            mdiaBox.setContent(minfBox, 2);
          trakBox.setContent(mdiaBox, 1);
        moovBox.setContent(trakBox, boxOffset);
        boxOffset++;
      }
      //end arbitrary
    std::cout << std::string(moovBox.asBox(),moovBox.boxedSize());

    //mdat box alot
    //video
    //while() 
    //for(input.seekNext(); input.getJSON(); input.seekNext())
    //cout << input.getJSON["data"].asString()
  
    //audio
//    ToPack.append(TS::GetAudioHeader(Strm.lastData().size(), Strm.metadata["audio"]["init"].asString()));
//    ToPack.append(Strm.lastData());
    printf("%c%c%c%cmdat", 0x00,0x00,0x01,0x00);
    //std::cout << "\200\000\000\010mdat";
    for(input.seekNext(); input.getJSON(); input.seekNext()){
      if(input.getJSON()["datatype"] == "video" /*|| input.getJSON()["datatype"] == "audio"*/){
        std::cout << input.getJSON()["data"].asString();
      }
    }

    return 0;
  } //DTSC2MP4

} //Converter namespace

/// Entry point for DTSC2FLV, simply calls Converters::DTSC2FLV().
int main(int argc, char ** argv){
  Util::Config conf = Util::Config(argv[0], PACKAGE_VERSION);
  conf.addOption("filename", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"Filename of the input file to convert.\"}"));
  conf.parseArgs(argc, argv);
  return Converters::DTSC2MP4(conf);
} //main
