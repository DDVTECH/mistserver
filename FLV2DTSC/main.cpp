/// \file FLV2DTSC/main.cpp
/// Contains the code that will transform any valid FLV input into valid DTSC.

#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "../util/flv_tag.h" //FLV support
#include "../util/dtsc.h" //DTSC support
#include "../util/amf.h" //AMF support

// String  onMetaData
// ECMA Array
//   Bool hasVideo 1
//   Number videocodecid 4 (2 = H263, 4 = VP6, 7 = H264)
//   Number width 320
//   Number height 240
//   Number framerate 23.976 (/ 1000)
//   Number videodatarate 500.349 (kbps)
//   Bool hasAudio 1
//   Bool stereo 1
//   Number audiodelay 0
//   Number audiosamplerate 11025
//   Number audiosamplesize 16
//   Number audiocodecid 2 (2 = MP3, 10 = AAC)
//   Number audiodatarate 64.3269 (kbps)


/// Holds all code that converts filetypes to DTSC.
namespace Converters{

  /// Inserts std::string type metadata into the passed DTMI object.
  /// \arg meta The DTMI object to put the metadata into.
  /// \arg cat Metadata category to insert into.
  /// \arg elem Element name to put into the category.
  /// \arg val Value to put into the element name.
  void Meta_Put(DTSC::DTMI & meta, std::string cat, std::string elem, std::string val){
    if (meta.getContentP(cat) == 0){meta.addContent(DTSC::DTMI(cat));}
    meta.getContentP(cat)->addContent(DTSC::DTMI(elem, val));
    std::cerr << "Metadata " << cat << "." << elem << " = " << val << std::endl;
  }

  /// Inserts uint64_t type metadata into the passed DTMI object.
  /// \arg meta The DTMI object to put the metadata into.
  /// \arg cat Metadata category to insert into.
  /// \arg elem Element name to put into the category.
  /// \arg val Value to put into the element name.
  void Meta_Put(DTSC::DTMI & meta, std::string cat, std::string elem, uint64_t val){
    if (meta.getContentP(cat) == 0){meta.addContent(DTSC::DTMI(cat));}
    meta.getContentP(cat)->addContent(DTSC::DTMI(elem, val));
    std::cerr << "Metadata " << cat << "." << elem << " = " << val << std::endl;
  }

  /// Returns true if the named category and elementname are available in the metadata.
  /// \arg meta The DTMI object to check.
  /// \arg cat Metadata category to check.
  /// \arg elem Element name to check.
  bool Meta_Has(DTSC::DTMI & meta, std::string cat, std::string elem){
    if (meta.getContentP(cat) == 0){return false;}
    if (meta.getContentP(cat)->getContentP(elem) == 0){return false;}
    return true;
  }

  /// Reads FLV from STDIN, outputs DTSC to STDOUT.
  int FLV2DTSC() {
    FLV::Tag FLV_in; // Temporary storage for incoming FLV data.
    AMF::Object meta_in; // Temporary storage for incoming metadata.
    DTSC::DTMI meta_out; // Storage for outgoing DTMI header data.
    DTSC::DTMI pack_out; // Storage for outgoing DTMI data.
    std::stringstream prebuffer; // Temporary buffer before sending real data
    bool sending = false;
    unsigned int counter = 0;
    
    while (!feof(stdin)){
      if (FLV_in.FileLoader(stdin)){
        if (!sending){
          counter++;
          if (counter > 10){
            sending = true;
            meta_out.Pack(true);
            meta_out.packed.replace(0, 4, DTSC::Magic_Header);
            std::cout << meta_out.packed;
            std::cout << prebuffer.rdbuf();
            prebuffer.str("");
            std::cerr << "Buffer done, starting real-time output..." << std::endl;
          }
        }
        if (FLV_in.data[0] == 0x12){
          meta_in = AMF::parse((unsigned char*)FLV_in.data+11, FLV_in.len-15);
          if (meta_in.getContentP(0) && (meta_in.getContentP(0)->StrValue() == "onMetaData") && meta_in.getContentP(1)){
            AMF::Object * tmp = meta_in.getContentP(1);
            if (tmp->getContentP("videocodecid")){
              switch ((unsigned int)tmp->getContentP("videocodecid")->NumValue()){
                case 2: Meta_Put(meta_out, "video", "codec", "H263"); break;
                case 4: Meta_Put(meta_out, "video", "codec", "VP6"); break;
                case 7: Meta_Put(meta_out, "video", "codec", "H264"); break;
                default: Meta_Put(meta_out, "video", "codec", "?"); break;
              }
            }
            if (tmp->getContentP("audiocodecid")){
              switch ((unsigned int)tmp->getContentP("audiocodecid")->NumValue()){
                case 2: Meta_Put(meta_out, "audio", "codec", "MP3"); break;
                case 10: Meta_Put(meta_out, "audio", "codec", "AAC"); break;
                default: Meta_Put(meta_out, "audio", "codec", "?"); break;
              }
            }
            if (tmp->getContentP("width")){
              Meta_Put(meta_out, "video", "width", tmp->getContentP("width")->NumValue());
            }
            if (tmp->getContentP("height")){
              Meta_Put(meta_out, "video", "height", tmp->getContentP("height")->NumValue());
            }
            if (tmp->getContentP("framerate")){
              Meta_Put(meta_out, "video", "fpks", tmp->getContentP("framerate")->NumValue()*1000);
            }
            if (tmp->getContentP("videodatarate")){
              Meta_Put(meta_out, "video", "bps", (tmp->getContentP("videodatarate")->NumValue()*1024)/8);
            }
            if (tmp->getContentP("audiodatarate")){
              Meta_Put(meta_out, "audio", "bps", (tmp->getContentP("audiodatarate")->NumValue()*1024)/8);
            }
            if (tmp->getContentP("audiosamplerate")){
              Meta_Put(meta_out, "audio", "rate", tmp->getContentP("audiosamplerate")->NumValue());
            }
            if (tmp->getContentP("audiosamplesize")){
              Meta_Put(meta_out, "audio", "size", tmp->getContentP("audiosamplesize")->NumValue());
            }
            if (tmp->getContentP("stereo")){
              if (tmp->getContentP("stereo")->NumValue() == 1){
                Meta_Put(meta_out, "audio", "channels", 2);
              }else{
                Meta_Put(meta_out, "audio", "channels", 1);
              }
            }
          }
        }
        if (FLV_in.data[0] == 0x08){
          char audiodata = FLV_in.data[11];
          if (FLV_in.needsInitData() && FLV_in.isInitData()){
            if ((audiodata & 0xF0) == 0xA0){
              Meta_Put(meta_out, "audio", "init", std::string((char*)FLV_in.data+13, (size_t)FLV_in.len-17));
            }else{
              Meta_Put(meta_out, "audio", "init", std::string((char*)FLV_in.data+12, (size_t)FLV_in.len-16));
            }
            continue;//skip rest of parsing, get next tag.
          }
          pack_out = DTSC::DTMI("audio", DTSC::DTMI_ROOT);
          pack_out.addContent(DTSC::DTMI("datatype", "audio"));
          pack_out.addContent(DTSC::DTMI("time", FLV_in.tagTime()));
          if (!Meta_Has(meta_out, "audio", "codec")){
            switch (audiodata & 0xF0){
              case 0x20: Meta_Put(meta_out, "audio", "codec", "MP3"); break;
              case 0xA0: Meta_Put(meta_out, "audio", "codec", "AAC"); break;
              default: Meta_Put(meta_out, "audio", "codec", "?"); break;
            }
          }
          if (!Meta_Has(meta_out, "audio", "rate")){
            switch (audiodata & 0x0C){
              case 0x0: Meta_Put(meta_out, "audio", "rate", 5500); break;
              case 0x4: Meta_Put(meta_out, "audio", "rate", 11000); break;
              case 0x8: Meta_Put(meta_out, "audio", "rate", 22000); break;
              case 0xC: Meta_Put(meta_out, "audio", "rate", 44000); break;
            }
          }
          if (!Meta_Has(meta_out, "audio", "size")){
            switch (audiodata & 0x02){
              case 0x0: Meta_Put(meta_out, "audio", "size", 8); break;
              case 0x2: Meta_Put(meta_out, "audio", "size", 16); break;
            }
          }
          if (!Meta_Has(meta_out, "audio", "channels")){
            switch (audiodata & 0x01){
              case 0x0: Meta_Put(meta_out, "audio", "channels", 1); break;
              case 0x1: Meta_Put(meta_out, "audio", "channels", 2); break;
            }
          }
          if ((audiodata & 0xF0) == 0xA0){
            pack_out.addContent(DTSC::DTMI("data", std::string((char*)FLV_in.data+13, (size_t)FLV_in.len-17)));
          }else{
            pack_out.addContent(DTSC::DTMI("data", std::string((char*)FLV_in.data+12, (size_t)FLV_in.len-16)));
          }
          if (sending){
            std::cout << pack_out.Pack(true);
          }else{
            prebuffer << pack_out.Pack(true);
          }
        }
        if (FLV_in.data[0] == 0x09){
          char videodata = FLV_in.data[11];
          if (FLV_in.needsInitData() && FLV_in.isInitData()){
            if ((videodata & 0x0F) == 7){
              Meta_Put(meta_out, "video", "init", std::string((char*)FLV_in.data+16, (size_t)FLV_in.len-20));
            }else{
              Meta_Put(meta_out, "video", "init", std::string((char*)FLV_in.data+12, (size_t)FLV_in.len-16));
            }
            continue;//skip rest of parsing, get next tag.
          }
          if (!Meta_Has(meta_out, "video", "codec")){
            switch (videodata & 0x0F){
              case 2: Meta_Put(meta_out, "video", "codec", "H263"); break;
              case 4: Meta_Put(meta_out, "video", "codec", "VP6"); break;
              case 7: Meta_Put(meta_out, "video", "codec", "H264"); break;
              default: Meta_Put(meta_out, "video", "codec", "?"); break;
            }
          }
          pack_out = DTSC::DTMI("video", DTSC::DTMI_ROOT);
          pack_out.addContent(DTSC::DTMI("datatype", "video"));
          switch (videodata & 0xF0){
            case 0x10: pack_out.addContent(DTSC::DTMI("keyframe", 1)); break;
            case 0x20: pack_out.addContent(DTSC::DTMI("interframe", 1)); break;
            case 0x30: pack_out.addContent(DTSC::DTMI("disposableframe", 1)); break;
            case 0x40: pack_out.addContent(DTSC::DTMI("keyframe", 1)); break;
            case 0x50: continue; break;//the video info byte we just throw away - useless to us...
          }
          if ((videodata & 0x0F) == 7){
            switch (FLV_in.data[12]){
              case 1: pack_out.addContent(DTSC::DTMI("nalu", 1)); break;
              case 2: pack_out.addContent(DTSC::DTMI("nalu_end", 1)); break;
            }
            int offset = 0;
            ((char*)(&offset))[0] = FLV_in.data[13];
            ((char*)(&offset))[1] = FLV_in.data[14];
            ((char*)(&offset))[2] = FLV_in.data[15];
            offset >>= 8;
            pack_out.addContent(DTSC::DTMI("offset", offset));
          }
          pack_out.addContent(DTSC::DTMI("time", FLV_in.tagTime()));
          pack_out.addContent(DTSC::DTMI("data", std::string((char*)FLV_in.data+12, (size_t)FLV_in.len-16)));
          if (sending){
            std::cout << pack_out.Pack(true);
          }else{
            prebuffer << pack_out.Pack(true);
          }
        }
      }
    }

    // if the FLV input is very short, do output it correctly...
    if (!sending){
      std::cerr << "EOF - outputting buffer..." << std::endl;
      meta_out.Pack(true);
      meta_out.packed.replace(0, 4, DTSC::Magic_Header);
      std::cout << meta_out.packed;
      std::cout << prebuffer.rdbuf();
    }
    std::cerr << "Done!" << std::endl;
    
    return 0;
  }//FLV2DTSC

};//Buffer namespace

/// Entry point for FLV2DTSC, simply calls Converters::FLV2DTSC().
int main(){
  return Converters::FLV2DTSC();
}//main
