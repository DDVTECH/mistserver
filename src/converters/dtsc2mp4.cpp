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
    ftypBox.setMajorBrand(0x6D703431);//mp41
    ftypBox.setMinorVersion(0);
    ftypBox.setCompatibleBrands(0x6D703431,0);
    std::cout << std::string(ftypBox.asBox(),ftypBox.boxedSize());

    
    
    //moov box
    MP4::MOOV moovBox;
      MP4::MVHD mvhdBox;
      mvhdBox.setVersion(0);
      mvhdBox.setCreationTime(0);
      mvhdBox.setModificationTime(0);
      mvhdBox.setTimeScale(1000);
      mvhdBox.setRate(0x10000);
      mvhdBox.setDuration(input.getMeta()["lastms"].asInt() + input.getMeta()["firstms"].asInt());
      mvhdBox.setTrackID(0);
      mvhdBox.setVolume(256);
      mvhdBox.setMatrix(0x00010000,0);
      mvhdBox.setMatrix(0,1);
      mvhdBox.setMatrix(0,2);
      mvhdBox.setMatrix(0,3);
      mvhdBox.setMatrix(0x00010000,4);
      mvhdBox.setMatrix(0,5);
      mvhdBox.setMatrix(0,6);
      mvhdBox.setMatrix(0,7);
      mvhdBox.setMatrix(0x40000000,8);
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
        int timescale = 0;
        MP4::TRAK trakBox;
          MP4::TKHD tkhdBox;
          //std::cerr << it->second["trackid"].asInt() << std::endl;
          tkhdBox.setVersion(0);
          tkhdBox.setFlags(15);
          tkhdBox.setTrackID(it->second["trackid"].asInt());
          tkhdBox.setDuration(it->second["lastms"].asInt() + it->second["firsms"].asInt());
          
          if (it->second["type"].asString() == "video"){
            tkhdBox.setWidth(it->second["width"].asInt() << 16);
            tkhdBox.setHeight(it->second["height"].asInt() << 16);
            tkhdBox.setVolume(0);
          }else{
            tkhdBox.setVolume(256);
            tkhdBox.setAlternateGroup(1);
          }
          tkhdBox.setMatrix(0x00010000,0);
          tkhdBox.setMatrix(0,1);
          tkhdBox.setMatrix(0,2);
          tkhdBox.setMatrix(0,3);
          tkhdBox.setMatrix(0x00010000,4);
          tkhdBox.setMatrix(0,5);
          tkhdBox.setMatrix(0,6);
          tkhdBox.setMatrix(0,7);
          tkhdBox.setMatrix(0x40000000,8);
          trakBox.setContent(tkhdBox, 0);
          
          MP4::MDIA mdiaBox;
            MP4::MDHD mdhdBox(0);/// \todo fix constructor mdhd in lib
            mdhdBox.setCreationTime(0);
            mdhdBox.setModificationTime(0);
            if(it->second["type"].asString() == "video"){
              timescale = 11988;
            }else{
              timescale = 48000;
            }
            mdhdBox.setTimeScale(timescale);
            mdhdBox.setDuration(((it->second["lastms"].asInt() + it->second["firsms"].asInt()) * ((double)timescale / 1000)));
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
                vmhdBox.setFlags(1);
                minfBox.setContent(vmhdBox,0);
              }else if (tmpStr == "audio"){
                MP4::SMHD smhdBox;
                minfBox.setContent(smhdBox,0);
              }
              MP4::DINF dinfBox;
                MP4::DREF drefBox;/// \todo fix constructor dref in lib
                  drefBox.setVersion(0);
                  MP4::URL urlBox;
                  urlBox.setFlags(1);
                  drefBox.setDataEntry(urlBox,0);
                dinfBox.setContent(drefBox,0);
              minfBox.setContent(dinfBox,1);
              
              MP4::STBL stblBox;
                MP4::STSD stsdBox;
                  stsdBox.setVersion(0);
                  if (tmpStr == "video"){//boxname = codec
                    MP4::VisualSampleEntry vse;
                    std::string tmpStr2 = it->second["codec"];
                    if (tmpStr2 == "H264"){
                      vse.setCodec("avc1");
                    }
                    vse.setDataReferenceIndex(1);
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
                      ase.setDataReferenceIndex(1);
                    }
                    ase.setSampleRate(it->second["rate"].asInt());
                    //ase.setChannelCount(it->second["channels"].asInt());
                    ase.setChannelCount(2);
                    ase.setSampleSize(it->second["size"].asInt());
                      MP4::ESDS esdsBox;
                      esdsBox.setESDescriptorTypeLength(32+it->second["init"].asString().size());
                      esdsBox.setESID(2);
                      esdsBox.setStreamPriority(0);
                      esdsBox.setDecoderConfigDescriptorTypeLength(18+it->second["init"].asString().size());
                      esdsBox.setByteObjectTypeID(0x40);
                      esdsBox.setStreamType(5);
                      esdsBox.setReservedFlag(1);
                      esdsBox.setBufferSize(1250000);
                      esdsBox.setMaximumBitRate(it->second["bps"].asInt() * 8);
                      esdsBox.setAverageBitRate(it->second["bps"].asInt() * 8);
                      esdsBox.setConfigDescriptorTypeLength(5);
                      esdsBox.setESHeaderStartCodes(it->second["init"].asString());
                      esdsBox.setSLConfigDescriptorTypeTag(0x6);
                      esdsBox.setSLConfigExtendedDescriptorTypeTag(0x808080);
                      esdsBox.setSLDescriptorTypeLength(1);
                      esdsBox.setSLValue(2);
                      ase.setCodecBox(esdsBox);
                    stsdBox.setEntry(ase,0);
                  }
                stblBox.setContent(stsdBox,0);
                
                /// \todo update following stts lines
                MP4::STTS sttsBox;//current version probably causes problems
                sttsBox.setVersion(0);
                int tmpParts = 0;
                for (JSON::ArrIter tmpIt = it->second["keys"].ArrBegin(); tmpIt != it->second["keys"].ArrEnd(); tmpIt++){
                  tmpParts += (*tmpIt)["parts"].size();
                }
                  MP4::STTSEntry newEntry;
                  newEntry.sampleCount = tmpParts;
                  newEntry.sampleDelta = ((it->second["lastms"].asInt() / tmpParts) * ((double)timescale / 1000));
                  sttsBox.setSTTSEntry(newEntry, 0);
                stblBox.setContent(sttsBox,1);
                
                if (it->second["type"] == "video"){
                  //STSS Box here
                  MP4::STSS stssBox;
                    stssBox.setVersion(0);
                    int tmpCount = 1;
                    for (int i = 0; i < it->second["keys"].size(); i++){
                      stssBox.setSampleNumber(tmpCount,i);
                      tmpCount += it->second["keys"][i]["parts"].size();
                    }
                  stblBox.setContent(stssBox,2);
                }

                int offset = (it->second["type"] == "video");

                
                MP4::STSC stscBox;//probably wrong
                stscBox.setVersion(0);
                uint32_t total = 0;
//                for (int i = 0; i < it->second["keys"].size(); i++){
                  MP4::STSCEntry stscEntry;
                  stscEntry.firstChunk = 1;
                  stscEntry.samplesPerChunk = 1;
                  stscEntry.sampleDescriptionIndex = 1;
                  stscBox.setSTSCEntry(stscEntry, 0);
 //               }
                stblBox.setContent(stscBox,2 + offset);

                MP4::STSZ stszBox;
                stszBox.setVersion(0);
                total = 0;
                for (int i = 0; i < it->second["keys"].size(); i++){
                  for (int o = 0; o < it->second["keys"][i]["parts"].size(); o++){
                    stszBox.setEntrySize(it->second["keys"][i]["parts"][o].asInt(), total);//in bytes in file
                    total++;
                  }
                }
                stblBox.setContent(stszBox,3 + offset);
                  
                MP4::STCO stcoBox;
                stcoBox.setVersion(0);
                total = 0;
                //Inserting wrong values on purpose here, will be fixed later.
                for (unsigned int i = 0; i < it->second["keys"].size(); i++){
                  for (unsigned int o = 0; o < it->second["keys"][i]["parts"].size(); o++){
                    stcoBox.setChunkOffset(it->second["keys"][i]["parts"][o].asInt(), total);
                    total++;
                  }
                }
                stblBox.setContent(stcoBox,4 + offset);

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
        for (int j = 0; j < checkTrakBox.getContentCount(); j++){
          if (checkTrakBox.getContent(j).isType("mdia")){
            checkMdiaBox = ((MP4::MDIA&)checkTrakBox.getContent(j));
            break;
          }
        }
        for (int j = 0; j < checkMdiaBox.getContentCount(); j++){
          if (checkMdiaBox.getContent(j).isType("minf")){
            checkMinfBox = ((MP4::MINF&)checkMdiaBox.getContent(j));
            break;
          }
        }
        for (int j = 0; j < checkMinfBox.getContentCount(); j++){
          if (checkMinfBox.getContent(j).isType("stbl")){
            checkStblBox = ((MP4::STBL&)checkMinfBox.getContent(j));
            break;
          }
        }
        for (int j = 0; j < checkStblBox.getContentCount(); j++){
          if (checkStblBox.getContent(j).isType("stco")){
            checkStcoBox = ((MP4::STCO&)checkStblBox.getContent(j));
            break;
          }
        }
        
        //std::cerr << std::string(checkStcoBox.asBox(),checkStcoBox.boxedSize()) << std::endl;
        //editing STCO box
        for (unsigned int o = 0; o < checkStcoBox.getEntryCount(); o++){
          uint64_t temp;
          temp = checkStcoBox.getChunkOffset(o);
          checkStcoBox.setChunkOffset(byteOffset, o);
          byteOffset += temp;
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
