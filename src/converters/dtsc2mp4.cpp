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
    /*MP4::FTYP ftypBox;
    ftypBox.setMajorBrand(0x69736f6d);
    ftypBox.setMinorVersion(512);
    ftypBox.setCompatibleBrands(0x69736f6d,0);
    ftypBox.setCompatibleBrands(0x69736f32,1);
    ftypBox.setCompatibleBrands(0x61766331,2);
    ftypBox.setCompatibleBrands(0x6d703431,3);
    std::cout << std::string(ftypBox.asBox(),ftypBox.boxedSize());*/
    MP4::FTYP ftypBox;
    ftypBox.setMajorBrand(0x6D703431);//mp41
    ftypBox.setMinorVersion(0);
    ftypBox.setCompatibleBrands(0x6D703431,0);
    std::cout << std::string(ftypBox.asBox(),ftypBox.boxedSize());

    
    
    //moov box
    MP4::MOOV moovBox;
      MP4::MVHD mvhdBox;
      mvhdBox.setCreationTime(0);
      mvhdBox.setModificationTime(0);
      mvhdBox.setTimeScale(1000);
      mvhdBox.setDuration(input.getMeta()["lastms"].asInt());
      /// \todo mvhd setTrackID automatic fix (next track ID)
      moovBox.setContent(mvhdBox, 0);
      
      //start arbitrary track addition
      int boxOffset = 1;
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
            MP4::MDHD mdhdBox;/// \todo fix constructor mdhd in lib
            mdhdBox.setCreationTime(0);
            mdhdBox.setModificationTime(0);
            mdhdBox.setTimeScale(1000);
            mdhdBox.setDuration(input.getMeta()["lastms"].asInt());
            mdiaBox.setContent(mdhdBox, 0);
            
            std::string tmpStr = it->second["type"].asString();
            MP4::HDLR hdlrBox;/// \todo fix constructor hdlr in lib
            if (tmpStr == "video"){
              hdlrBox.setHandlerType(0x76696465);//vide
            }else if (tmpStr == "audio"){
              hdlrBox.setHandlerType(0x736F756E);//soun
            }
            hdlrBox.setName(it->first);
            mdiaBox.setContent(hdlrBox, 1);
            
            MP4::MINF minfBox;
              if (tmpStr == "video"){
                MP4::VMHD vmhdBox;
                minfBox.setContent(vmhdBox,0);
              }else if (tmpStr == "audio"){
                MP4::SMHD smhdBox;
                minfBox.setContent(smhdBox,0);
              }
              MP4::DINF dinfBox;
                MP4::DREF drefBox;/// \todo fix constructor dref in lib
                  MP4::URN urnBox;
                  urnBox.setName("Name Here");
                  urnBox.setLocation("Location Here");
                  drefBox.setDataEntry(urnBox,0);
                dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,1);
              
              MP4::STBL stblBox;
                MP4::STSD stsdBox;
                  if (tmpStr == "video"){//boxname = codec
                    MP4::VisualSampleEntry vse;
                    std::string tmpStr2 = it->second["codec"];
                    if (tmpStr2 == "H264"){
                      vse.setCodec("avc1");
                      
                    }
                    vse.setWidth(it->second["width"].asInt());
                    vse.setHeight(it->second["height"].asInt());
                      MP4::AVCC avccBox;
                      avccBox.setPayload(it->second["init"].asString());
                      vse.setCLAP(avccBox);
                    stsdBox.setEntry(vse,0);
                  }else if(tmpStr == "audio"){//boxname = codec
                    MP4::AudioSampleEntry ase;
                    std::string tmpStr2 = it->second["codec"];
                    if (tmpStr2 == "AAC"){
                      ase.setCodec("mp4a");
                    }
                    ase.setSampleRate(it->second["rate"].asInt());
                    //ase.setChannelCount(it->second["channels"].asInt());
                    ase.setChannelCount(2);
                    ase.setSampleSize(it->second["size"].asInt());
                      MP4::ESDS esdsBox;
                      esdsBox.setPayload(it->second["init"].asString());
                      ase.setCLAP(esdsBox);
                    stsdBox.setEntry(ase,0);
                  }
                stblBox.setContent(stsdBox,0);
                
                /// \todo update following stts lines
                MP4::STTS sttsBox;//current version probably causes problems
                for (int i = 0; i < it->second["keys"].size(); i++){
                  MP4::STTSEntry newEntry;
                  newEntry.sampleCount = it->second["keys"][i]["parts"].size();
                  newEntry.sampleDelta = it->second["keys"][i]["len"].asInt(); // it->second["keys"][i]["parts"].size();
                  sttsBox.setSTTSEntry(newEntry, i);
                }
                stblBox.setContent(sttsBox,1);
                
                //STSS Box here
                
                MP4::STSC stscBox;//probably wrong
                uint32_t total = 0;
                for (int i = 0; i < it->second["keys"].size(); i++){
                  /*MP4::STSCEntry newEntry;
                  newEntry.firstChunk = it->second["keys"][i]["num"].asInt();
                  newEntry.samplesPerChunk = it->second["keys"][i]["parts"].size();
                  newEntry.sampleDescriptionIndex = 1;
                  stscBox.setSTSCEntry(newEntry, i);*/
                  for (int o = 0; o < it->second["keys"][i]["parts"].size(); o++){
                    MP4::STSCEntry newEntry;
                    newEntry.firstChunk = total+1;
                    newEntry.samplesPerChunk = 1;
                    newEntry.sampleDescriptionIndex = 1;
                    stscBox.setSTSCEntry(newEntry, total);
                    total++;
                  }
                }
                stblBox.setContent(stscBox,2);

                MP4::STSZ stszBox;
                total = 0;
                for (int i = 0; i < it->second["keys"].size(); i++){
                  for (int o = 0; o < it->second["keys"][i]["parts"].size(); o++){
                    stszBox.setEntrySize(it->second["keys"][i]["parts"][o].asInt(), total);//in bytes in file
                    total++;
                  }
                }
                stblBox.setContent(stszBox,3);
                  
                MP4::STCO stcoBox;
                total = 0;
                for (int i = 0; i < it->second["keys"].size(); i++){
                  for (int o = 0; o < it->second["keys"][i]["parts"].size(); o++){
                    stcoBox.setChunkOffset(it->second["keys"][i]["parts"][o].asInt(), total);//in bytes in file
                    total++;
                  }
                }
                stblBox.setContent(stcoBox,4);
              minfBox.setContent(stblBox,2);
            mdiaBox.setContent(minfBox, 2);
          trakBox.setContent(mdiaBox, 1);
        moovBox.setContent(trakBox, boxOffset);
        boxOffset++;
      }
      //end arbitrary
      //initial offset lengte ftyp, length moov + 8
      unsigned long long int byteOffset = ftypBox.boxedSize() + moovBox.boxedSize() + 8;
      //update all STCO
      //for tracks
      for (unsigned int i = 1; i < moovBox.getContentCount(); i++){
        //10 lines to get the STCO box.
        MP4::TRAK checkTrakBox;
        MP4::MDIA checkMdiaBox;
        MP4::MINF checkMinfBox;
        MP4::STBL checkStblBox;
        MP4::STCO checkStcoBox;
        checkTrakBox = ((MP4::TRAK&)moovBox.getContent(i));
        checkMdiaBox = ((MP4::MDIA&)checkTrakBox.getContent(1));
        checkMinfBox = ((MP4::MINF&)checkMdiaBox.getContent(2));
        checkStblBox = ((MP4::STBL&)checkMinfBox.getContent(2));
        checkStcoBox = ((MP4::STCO&)checkStblBox.getContent(4));
        
        //std::cerr << std::string(checkStcoBox.asBox(),checkStcoBox.boxedSize()) << std::endl;
        for (unsigned int o = 0; o < checkStcoBox.getEntryCount(); o++){
          uint64_t temp;
          temp = checkStcoBox.getChunkOffset(o);
          checkStcoBox.setChunkOffset(byteOffset, o);
          byteOffset += temp;
          //std::cerr << "FNURF "<< byteOffset << ", " << temp << std::endl;
        }
      }
    std::cout << std::string(moovBox.asBox(),moovBox.boxedSize());

    //mdat box alot
    //video
    //while() 
    //for(input.seekNext(); input.getJSON(); input.seekNext())
    //cout << input.getJSON["data"].asString()
  
    
    std::set<int> selector;
    for (JSON::ObjIter trackIt = input.getMeta()["tracks"].ObjBegin(); trackIt != input.getMeta()["tracks"].ObjEnd(); trackIt++){
      selector.insert(trackIt->second["trackid"].asInt());
    }
    input.selectTracks(selector);

    input.seek_time(0);

    input.seekNext();
    std::vector<std::string> dataParts;
    while (input.getJSON()){
      //if not in vector, create;
      if (dataParts.size() < input.getJSON()["trackid"].asInt()){
        dataParts.resize(input.getJSON()["trackid"].asInt());
      }
      //putting everything in its place
      dataParts[input.getJSON()["trackid"].asInt()-1] += input.getJSON()["data"].asString();
      input.seekNext();
    }
    uint32_t mdatSize = 0;
    for (unsigned int x = 0; x < dataParts.size(); x++){
      mdatSize += dataParts[x].size();
    }
    //std::cerr << "Total Data size: " << mdatSize << std::endl;
    printf("%c%c%c%cmdat", (mdatSize>>24) & 0x000000FF,(mdatSize>>16) & 0x000000FF,(mdatSize>>8) & 0x000000FF,mdatSize & 0x000000FF);
    for (unsigned int x = 0; x < dataParts.size(); x++){
      std::cout << dataParts[x];
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
