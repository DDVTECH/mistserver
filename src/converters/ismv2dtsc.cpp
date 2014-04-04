#include <string>
#include <iostream>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>
#include <mist/mp4_encryption.h>
#include <mist/json.h>
#include <arpa/inet.h>

bool ParseSmoothFragment(std::string & frag, std::string & trunOut, std::string & mdatOut, std::string & encOut, int & trackID) {
  std::string tmpStor = frag;
  #define moofBox ((MP4::MOOF&)boxedFrag)
  MP4::Box boxedFrag;
  trunOut = "";
  mdatOut = "";
  encOut = "";
  trackID = -1;
  if ( !boxedFrag.read(tmpStor)){
    trunOut = "";
    mdatOut = "";
    encOut = "";
    trackID = -1;
    return false;
  }
  for (unsigned int i = 0; i < moofBox.getContentCount(); i++){
    if (moofBox.getContent(i).isType("traf")){
      MP4::TRAF trafBox = (MP4::TRAF&)moofBox.getContent(i);
      for (unsigned int j = 0; j < trafBox.getContentCount(); j++){
        if (trafBox.getContent(j).isType("trun")){
          MP4::TRUN trunBox = (MP4::TRUN&)trafBox.getContent(j);
          trunOut = std::string(trunBox.asBox(), trunBox.boxedSize());
        }
        if (trafBox.getContent(j).isType("tfhd")){
          trackID = ((MP4::TFHD&)trafBox.getContent(j)).getTrackID();
        }
        if (trafBox.getContent(j).isType("uuid")){
          if (((MP4::UUID&)trafBox.getContent(j)).getUUID() == "a2394f52-5a9b-4f14-a244-6c427c648df4"){
            MP4::UUID uuidBox = (MP4::UUID&)trafBox.getContent(j);
            encOut = std::string(uuidBox.asBox(), uuidBox.boxedSize());
          }
        }
      }
    }
  }
  if ( !boxedFrag.read(tmpStor)){
    trunOut = "";
    mdatOut = "";
    encOut = "";
    trackID = -1;
    return false;
  }
  mdatOut = std::string(boxedFrag.payload(), boxedFrag.payloadSize());
  frag = tmpStor;
  return true;
}

static char c2hex( int c){
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

std::string toHex(std::string & input){
  std::string result;
  result.reserve( input.size() / 2 );
  for (unsigned int i = 0; i < input.size(); i+=2 ){
    result += (char)((c2hex(input[i]) << 4) + c2hex(input[i+1]));
  }
  return result;
}

int main( int argc, char * argv[] ) {
  MP4::Box tmpBox;
  MP4::MOOV moovBox;
  MP4::TRUN lastTrun;
  MP4::UUID_SampleEncryption lastEnc;
  MP4::Box lastMdat;
  bool isEncrypted = false;

  JSON::Value metaData;
  metaData["moreheader"] = 0LL;

  std::string strBuf;
  strBuf.reserve(1048576);
  bool readHeader = false; 
  std::string lastTrunStr;
  std::string lastMdatStr;
  std::string lastEncStr;
  int lastTrackID;

  int lastEntry = 0;
  unsigned int lastPos = 0;
  std::map<int,int> currentDuration;

  while (std::cin.good()){
    for (int i = 0; i < 1024 && std::cin.good(); i++){
      strBuf += std::cin.get();
    }

    if ( !readHeader){
      while (tmpBox.read(strBuf)){
        if ( tmpBox.isType("moov")){
          std::string tmp = std::string(tmpBox.asBox(),tmpBox.boxedSize());
          moovBox.read(tmp);
          for (unsigned int i = 0; i < moovBox.getContentCount(); i++){
            if (moovBox.getContent(i).isType("mvhd")){
              MP4::MVHD content = (MP4::MVHD&)moovBox.getContent(i);
              metaData["lastms"] = (long long int)(content.getDuration() / (content.getTimeScale() / 10000));
            }
            if (moovBox.getContent(i).isType("trak")){
              MP4::TRAK content = (MP4::TRAK&)moovBox.getContent(i);
              std::stringstream trackId;
              trackId << "track";
              for (unsigned int j = 0; j < content.getContentCount(); j++){
                if (content.getContent(j).isType("tkhd")){
                  MP4::TKHD subContent = (MP4::TKHD&)content.getContent(j);
                  trackId << subContent.getTrackID();
                  metaData["tracks"][trackId.str()]["trackid"] = subContent.getTrackID();
                }
                if (content.getContent(j).isType("mdia")){
                  MP4::MDIA subContent = (MP4::MDIA&)content.getContent(j);
                  for (unsigned int k = 0; k < subContent.getContentCount(); k++){
                    if (subContent.getContent(k).isType("hdlr")){
                      MP4::HDLR subsubContent = (MP4::HDLR&)subContent.getContent(k);
                      if (subsubContent.getHandlerType() == "soun"){
                        metaData["tracks"][trackId.str()]["type"] = "audio";
                      }
                      if (subsubContent.getHandlerType() == "vide"){
                        metaData["tracks"][trackId.str()]["type"] = "video";
                      }
                    }
                    if (subContent.getContent(k).isType("minf")){
                      MP4::MINF subsubContent = (MP4::MINF&)subContent.getContent(k);
                      for (unsigned int l = 0; l < subsubContent.getContentCount(); l++){
                        if (subsubContent.getContent(l).isType("stbl")){
                          MP4::STBL stblBox = (MP4::STBL&)subsubContent.getContent(l);
                          for (unsigned int m = 0; m < stblBox.getContentCount(); m++){
                            if (stblBox.getContent(m).isType("stsd")){
                              MP4::STSD stsdBox = (MP4::STSD&)stblBox.getContent(m);
                              for (unsigned int n = 0; n < stsdBox.getEntryCount(); n++){
                                if (stsdBox.getEntry(n).isType("mp4a") || stsdBox.getEntry(n).isType("enca")){
                                  MP4::MP4A mp4aBox = (MP4::MP4A&)stsdBox.getEntry(n);
                                  metaData["tracks"][trackId.str()]["codec"] = "AAC";
                                  std::string tmpStr;
                                  tmpStr += (char)((mp4aBox.toAACInit() & 0xFF00 ) >> 8);
                                  tmpStr += (char)(mp4aBox.toAACInit() & 0x00FF);
                                  metaData["tracks"][trackId.str()]["init"]  = tmpStr;
                                  metaData["tracks"][trackId.str()]["channels"] = mp4aBox.getChannelCount();
                                  metaData["tracks"][trackId.str()]["size"] = mp4aBox.getSampleSize();
                                  metaData["tracks"][trackId.str()]["rate"] = mp4aBox.getSampleRate();
                                  if (mp4aBox.isType("enca")){
                                    metaData["encrypted"] = 1ll;
                                    MP4::SINF sinfBox = (MP4::SINF&)mp4aBox.getSINFBox();
                                    for (int o = 0; o < 4; o++){//sinf has a maximum of 4 entries.
                                      if (sinfBox.getEntry(o).isType("schi")){
                                        metaData["kid"] = ((MP4::UUID_TrackEncryption&)((MP4::SCHI&)sinfBox.getEntry(0)).getContent()).getDefaultKID();
                                      }
                                    }
                                  }
                                }
                                if (stsdBox.getEntry(n).isType("avc1") || stsdBox.getEntry(n).isType("encv")){
                                  MP4::AVC1 avc1Box = (MP4::AVC1&)stsdBox.getEntry(n);
                                  metaData["tracks"][trackId.str()]["height"] = avc1Box.getHeight();
                                  metaData["tracks"][trackId.str()]["width"] = avc1Box.getWidth();
                                  metaData["tracks"][trackId.str()]["init"] = std::string(avc1Box.getCLAP().payload(), avc1Box.getCLAP().payloadSize());
                                  metaData["tracks"][trackId.str()]["codec"] = "H264";
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          metaData["firstms"] = 0;
          metaData["length"] = metaData["lastms"].asInt() / 1000;
          std::cout << metaData.toNetPacked();
          readHeader = true;
          fprintf(stderr,"\n\n%s\n",metaData.toPrettyString().c_str());
          break;
        }
      }
    }
    if (readHeader){
      isEncrypted = false;
      while(ParseSmoothFragment(strBuf, lastTrunStr, lastMdatStr, lastEncStr, lastTrackID)){
        if (lastEncStr != ""){
          isEncrypted = true;
          lastEnc.clear();
        }else{
          isEncrypted = false;
        }
        std::stringstream trackName;
        trackName << "track" << lastTrackID;
        lastEntry = 0;
        lastPos = 0;
        if (currentDuration.find(lastTrackID) == currentDuration.end()){
          currentDuration[lastTrackID] = 0;
        }
        lastTrun.read(lastTrunStr);
        if (isEncrypted){
          lastEnc.read(lastEncStr);
        }
        while (lastPos < lastMdatStr.size()){
          JSON::Value myVal;
          myVal["time"] = currentDuration[lastTrackID] / 10000;
          myVal["trackid"] = lastTrackID;
          myVal["data"] = lastMdatStr.substr(lastPos,lastTrun.getSampleInformation(lastEntry).sampleSize);
          if (isEncrypted){
            myVal["ivec"] = lastEnc.getSample(lastEntry).InitializationVector;
          }
          myVal["duration"] = lastTrun.getSampleInformation(lastEntry).sampleDuration;
          if (metaData["tracks"][trackName.str()]["type"] == "video"){
            if (lastEntry){
              myVal["interframe"] = 1LL;
            }else{
              myVal["keyframe"] = 1LL;
            }
            myVal["nalu"] = 1LL;
            myVal["offset"] = lastTrun.getSampleInformation(lastEntry).sampleOffset / 10000;
            unsigned int offset = 0;
            while( offset < myVal["data"].asString().size() ) {
              int size = ntohl(((int*)(myVal["data"].asString().c_str()+offset))[0]);
              offset += 4 + size;
            }
          }else{
            if (!lastEntry){
              myVal["keyframe"] = 1LL;
            }
          }
          std::cout << myVal.toNetPacked();
          currentDuration[lastTrackID] += lastTrun.getSampleInformation(lastEntry).sampleDuration;
          lastPos += lastTrun.getSampleInformation(lastEntry).sampleSize;
          lastEntry ++;
        }
      }
    }
  }

  return 0;
}
