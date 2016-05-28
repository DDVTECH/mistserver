/// \file dash_analyzer.cpp
/// Contains the code for the DASH Analysing tool.
/// Currently, only mp4 is supported, and the xml parser assumes a representation id tag exists


#include <mist/config.h>
#include <mist/timing.h>
#include <mist/defines.h>
#include <mist/http_parser.h>
#include <iostream>
#include <fstream>
#include <set>
#include <mist/mp4.h>
#include <mist/mp4_generic.h>


#define OTHER 0x00
#define VIDEO 0x01
#define AUDIO 0x02
  
///\brief simple struct for storage of stream-specific data
struct StreamData{
  long timeScale;
  std::string media;
  std::string initialization;
  std::string initURL;
  long trackID;        
  unsigned int adaptationSet;
  unsigned char trackType;    
};

StreamData tempSD;  //temp global

  
///\brief another simple structure used for ordering byte seek positions.
struct seekPos {
  ///\brief Less-than comparison for seekPos structures.
  ///\param rhs The seekPos to compare with.
  ///\return Whether this object is smaller than rhs.
  bool operator < (const seekPos & rhs) const {
    if ((seekTime*rhs.timeScale) < (rhs.seekTime*timeScale)) {
      return true;
    } else {
      if ( (seekTime*rhs.timeScale) == (rhs.seekTime*timeScale)){
        if (adaptationSet < rhs.adaptationSet){
          return true;
        } else if (adaptationSet == rhs.adaptationSet){
          if (trackID < rhs.trackID) {
            return true;
          }
        }          
      }
    }
    return false;
  }
  
  long timeScale;
  long long unsigned int bytePos;     /// ?
  long long unsigned int seekTime;        ///start
  long long unsigned int duration;     ///duration
  unsigned int trackID;               ///stores representation ID
  unsigned int adaptationSet;                 ///stores type
  unsigned char trackType;                 ///stores type
  std::string url;
  
  
};

bool getDelimBlock(std::string & data, std::string name, size_t &blockStart, size_t &blockEnd, std::string delim){
  size_t offset=data.find(name);  
  if(offset==std::string::npos){ 
    return false; //name string not found.
  }  
  //expected: delim character BEFORE blockstart.
  offset--;
  
  blockStart=data.find(delim,offset);
  //DEBUG_MSG(DLVL_INFO, "offset: %i blockStart: %i ", offset, blockStart);
  offset=blockStart+1;//skip single character!
  blockEnd=data.find(delim,offset);
  
  //DEBUG_MSG(DLVL_INFO, "offset: %i blockEnd: %i ", offset, blockEnd);
  if(blockStart==std::string::npos || blockEnd==std::string::npos){ 
    return false; //no start/end quotes found
  }
 
  blockEnd++; //include delim
  //DEBUG_MSG(DLVL_INFO, "getDelimPos: data.size() %i start %i end %i num %i", data.size(), blockStart,blockEnd,(blockEnd-blockStart)  );
  return true;
}


bool getValueBlock(std::string & data, std::string name, size_t &blockStart, size_t &blockEnd, std::string delim){
  size_t offset=data.find(name);  
  if(offset==std::string::npos){ 
    return false; //name string not found.
  }  
  blockStart=data.find(delim,offset);
  //DEBUG_MSG(DLVL_INFO, "offset: %i blockStart: %i ", offset, blockStart);
  blockStart++; //clip off quote characters
  offset=blockStart;//skip single character!
  blockEnd=data.find(delim,offset);
  //DEBUG_MSG(DLVL_INFO, "offset: %i blockEnd: %i ", offset, blockEnd);
  if(blockStart==std::string::npos || blockEnd==std::string::npos){ 
    return false; //no start/end quotes found
  }  
  //DEBUG_MSG(DLVL_INFO, "getValueBlock: data.size() %i start %i end %i num %i", data.size(), blockStart,blockEnd,(blockEnd-blockStart)  );
  return true;
}

bool getString(std::string &data, std::string name, std::string &output){
  size_t blockStart=0;
  size_t blockEnd=0;
  
  if(!getValueBlock(data, name, blockStart,blockEnd, "\"")){
    //DEBUG_MSG(DLVL_FAIL, "could not find \"%s\" in data block", name.c_str());
    return false; //could not find value in this data block.
  }  
   //DEBUG_MSG(DLVL_INFO, "data.size() %i start %i end %i num %i", data.size(), blockStart,blockEnd,(blockEnd-blockStart)  )
   output=data.substr(blockStart,(blockEnd-blockStart));  
  //looks like this function is working as expected
  //DEBUG_MSG(DLVL_INFO, "data in getstring %s", (data.substr(blockStart,(blockEnd-blockStart))).c_str());
  return true;
}

bool getLong(std::string &data, std::string name, long &output){
  size_t blockStart, blockEnd;
  if(!getValueBlock(data, name, blockStart,blockEnd, "\"")){
    //DEBUG_MSG(DLVL_FAIL, "could not find \"%s\" in data block", name.c_str());
    return false; //could not find value in this data block.
  }  
  //DEBUG_MSG(DLVL_INFO, "name: %s data in atol %s",name.c_str(), (data.substr(blockStart,(blockEnd-blockStart))).c_str());
  output=atol( (data.substr(blockStart,(blockEnd-blockStart))).c_str() );
  return true;
}

//block expecting separate name and /name occurence, or name and /> before another occurence of <.
bool getBlock(std::string & data, std::string name, int offset, size_t &blockStart, size_t &blockEnd){
  blockStart=data.find("<"+name+">",offset);    
  if(blockStart==std::string::npos){    
    blockStart=data.find("<"+name+" ",offset);    //this considers both valid situations <name> and <name bla="bla"/>
  }  
  
  if(blockStart==std::string::npos){    
    DEBUG_MSG(DLVL_INFO, "no block start found for name: %s at offset: %i",name.c_str(), offset);
    return false;
  }  

  blockEnd=data.find("/" + name+ ">", blockStart);  
  if(blockEnd==std::string::npos){         
    blockEnd=data.find("/>", blockStart);
    if(blockEnd==std::string::npos){ 
      DEBUG_MSG(DLVL_INFO, "no block end found.");
      return false;
    }
    size_t temp=data.find("<", blockStart+1, (blockEnd-blockStart-1)); //the +1 is to avoid re-interpreting the starting < //TODO!!
    if(temp!=std::string::npos){ //all info is epxected between <name ... />
      DEBUG_MSG(DLVL_FAIL, "block start found before block end. offset: %lu block: %s", temp, data.c_str());
      return false;
    }
    //DEBUG_MSG(DLVL_FAIL, "special block end found");
    blockEnd+=2;  //position after />
  } else {  
    blockEnd += name.size()+2;  //position after /name>
  }  
  
  //DEBUG_MSG(DLVL_INFO, "getBlock: start: %i end: %i",blockStart,blockEnd);
  return true;
}

bool parseAdaptationSet(std::string & data, std::set<seekPos> &currentPos){
  //DEBUG_MSG(DLVL_INFO, "Parsing adaptationSet: %s", data.c_str());
  size_t offset =0;  
  size_t blockStart, blockEnd;  
  tempSD.trackType=OTHER;
  //get value: mimetype //todo: handle this!
  std::string mimeType;
  if(!getString(data,"mimeType", mimeType)){ //get first occurence of mimeType. --> this will break if multiple mimetypes should be read from this block because no offset is provided. solution: use this on a substring containing the desired information.
    DEBUG_MSG(DLVL_FAIL, "mimeType not found");
    return false;
  }

  DEBUG_MSG(DLVL_INFO, "mimeType: %s",mimeType.c_str());  //checked, OK

  if(mimeType.find("video")!=std::string::npos){tempSD.trackType=VIDEO;}
  if(mimeType.find("audio")!=std::string::npos){tempSD.trackType=AUDIO;}
  if(tempSD.trackType==OTHER){
    DEBUG_MSG(DLVL_FAIL, "no audio or video type found. giving up.");
    return false;
  }
  
  //find an ID within this adaptationSet block.
  if(!getBlock(data,(std::string)"Representation", offset, blockStart, blockEnd)){
    DEBUG_MSG(DLVL_FAIL, "Representation not found");
    return false;
  }
  
  //representation string 
  
  std::string block=data.substr(blockStart,(blockEnd-blockStart));
  DEBUG_MSG(DLVL_INFO, "Representation block: %s",block.c_str());
  //check if block is not junk?  
  
  if(!getLong(block,"id", tempSD.trackID) ){
    DEBUG_MSG(DLVL_FAIL, "Representation id not found in block %s",block.c_str());
    return false;
  }
  DEBUG_MSG(DLVL_INFO, "Representation/id: %li",tempSD.trackID); //checked, OK

  offset =0; 
  //get values from SegmentTemplate
  if(!getBlock(data,(std::string)"SegmentTemplate", offset, blockStart, blockEnd)){
    DEBUG_MSG(DLVL_FAIL, "SegmentTemplate not found");
    return false;
  }
  block=data.substr(blockStart,(blockEnd-blockStart));
  //DEBUG_MSG(DLVL_INFO, "SegmentTemplate block: %s",block.c_str());  //OK
  

  getLong(block,"timescale", tempSD.timeScale);
  getString(block,"media", tempSD.media);  
  getString(block,"initialization", tempSD.initialization);    
  
  size_t tmpBlockStart=0;
  size_t tmpBlockEnd=0;
  if(!getDelimBlock(tempSD.media,"RepresentationID",tmpBlockStart,tmpBlockEnd, "$")){
    DEBUG_MSG(DLVL_FAIL, "Failed to find and replace $RepresentationID$ in %s",tempSD.media.c_str());
    return false;
  } 
  tempSD.media.replace(tmpBlockStart,(tmpBlockEnd-tmpBlockStart),"%d");

  if(!getDelimBlock(tempSD.media,"Time",tmpBlockStart,tmpBlockEnd, "$")){
    DEBUG_MSG(DLVL_FAIL, "Failed to find and replace $Time$ in %s",tempSD.media.c_str());
    return false;
  }  
  tempSD.media.replace(tmpBlockStart,(tmpBlockEnd-tmpBlockStart),"%d");
  
  if(!getDelimBlock(tempSD.initialization,"RepresentationID",tmpBlockStart,tmpBlockEnd, "$")){
    DEBUG_MSG(DLVL_FAIL, "Failed to find and replace $RepresentationID$ in %s",tempSD.initialization.c_str());
    return false;
  } 
  tempSD.initialization.replace(tmpBlockStart,(tmpBlockEnd-tmpBlockStart),"%d");

  //get segment timeline block from within segment template: 
  size_t blockOffset=0; //offset should be 0 because this is a new block
  if(!getBlock(block,"SegmentTimeline", blockOffset, blockStart, blockEnd)){
    DEBUG_MSG(DLVL_FAIL, "SegmentTimeline block not found");
    return false;
  } 
    
  std::string block2=block.substr(blockStart,(blockEnd-blockStart)); //overwrites previous block (takes just the segmentTimeline part  
  //DEBUG_MSG(DLVL_INFO, "SegmentTimeline block: %s",block2.c_str()); //OK
  
  int numS=0;
  offset=0;
  long long unsigned int totalDuration=0;
  long timeValue;
  while(1){
    if(!getBlock(block2,"S",offset, blockStart, blockEnd)){  
      if(numS==0){
        DEBUG_MSG(DLVL_FAIL, "no S found within SegmentTimeline");
        return false;
      } else {
        DEBUG_MSG(DLVL_INFO, "all S found within SegmentTimeline %i", numS);
        return true; //break;  //escape from while loop (to return true)
      }
    } 
    numS++;     
    //stuff S data into: currentPos
    //searching for t(start position)
    std::string sBlock=block2.substr(blockStart,(blockEnd-blockStart));    
    //DEBUG_MSG(DLVL_INFO, "S found. offset: %i blockStart: %i blockend: %i block: %s",offset,blockStart, blockEnd, sBlock.c_str());    //OK!
    if(getLong(sBlock,"t", timeValue)){
      totalDuration=timeValue; //reset totalDuration to value of t
    }
    if(!getLong(sBlock,"d", timeValue)){ //expected duration in every S.
      DEBUG_MSG(DLVL_FAIL, "no d found within S");
      return false;
    }
    //stuff data with old value (start of block)    
    //DEBUG_MSG(DLVL_INFO, "stuffing info from S into set");
    seekPos thisPos;
    thisPos.trackType=tempSD.trackType;
    thisPos.trackID=tempSD.trackID;
    thisPos.adaptationSet=tempSD.adaptationSet;
    //thisPos.trackID=id;
    thisPos.seekTime=totalDuration; //previous total duration is start time of this S.
    thisPos.duration=timeValue;
    thisPos.timeScale=tempSD.timeScale;
    
    static char charBuf[512];
    snprintf(charBuf, 512, tempSD.media.c_str(), tempSD.trackID, totalDuration);
    thisPos.url.assign(charBuf);
    //DEBUG_MSG(DLVL_INFO, "media url (from rep.ID %d, startTime %d): %s", tempSD.trackID, totalDuration,thisPos.url.c_str());
    
    currentPos.insert(thisPos);     //assumes insert copies all data in seekPos struct.
    totalDuration+=timeValue;//update totalDuration   
    offset=blockEnd; //blockEnd and blockStart are absolute values within string, offset is not relevant.
  }  
  return true;
}

bool parseXML(std::string & body, std::set<seekPos> &currentPos, std::vector<StreamData> &streamData){
  //for all adaptation sets 
  //representation ID
  int numAdaptationSet=0;
  size_t currentOffset=0;
  size_t adaptationSetStart;
  size_t adaptationSetEnd;  
  //DEBUG_MSG(DLVL_INFO, "body received: %s", body.c_str());
  
  while(getBlock(body,"AdaptationSet",currentOffset, adaptationSetStart, adaptationSetEnd)){
    tempSD.adaptationSet=numAdaptationSet;
    numAdaptationSet++;      
    DEBUG_MSG(DLVL_INFO, "adaptationSet found. start: %lu end: %lu num: %lu ",adaptationSetStart,adaptationSetEnd,(adaptationSetEnd-adaptationSetStart));
    //get substring: from <adaptationSet... to /adaptationSet> 
    std::string adaptationSet=body.substr(adaptationSetStart,(adaptationSetEnd-adaptationSetStart));
    //function was verified: output as expected.
    
    if(!parseAdaptationSet(adaptationSet, currentPos)){
      DEBUG_MSG(DLVL_FAIL, "parseAdaptationSet returned false."); //this also happens in the case of OTHER mimetype. in that case it might be desirable to continue searching for valid data instead of quitting.
      return false;    
    }
    streamData.push_back(tempSD); //put temp values into adaptation set vector
    currentOffset=adaptationSetEnd;//the getblock function should make sure End is at the correct offset. 
  }
  if(numAdaptationSet==0){
    DEBUG_MSG(DLVL_FAIL, "no adaptationSet found.");
    return false;
  } 
  DEBUG_MSG(DLVL_INFO, "all adaptation sets found. total: %i", numAdaptationSet);  
  return true;
}

int main(int argc, char ** argv) {
  Util::Config conf = Util::Config(argv[0]);
  conf.addOption("mode", JSON::fromString("{\"long\":\"mode\", \"arg\":\"string\", \"short\":\"m\", \"default\":\"analyse\", \"help\":\"What to do with the stream. Valid modes are 'analyse', 'validate', 'output'.\"}"));
  conf.addOption("url", JSON::fromString("{\"arg_num\":1, \"arg\":\"string\", \"help\":\"URL to HLS stream index file to retrieve.\"}"));
  conf.addOption("abort", JSON::fromString("{\"long\":\"abort\", \"short\":\"a\", \"arg\":\"integer\", \"default\":-1, \"help\":\"Abort after this many seconds of downloading. Negative values mean unlimited, which is the default.\"}"));
  conf.parseArgs(argc, argv);
  conf.activate();
  
  unsigned int port = 80;
  std::string url = conf.getString("url");
 
 
  if (url.substr(0, 7) != "http://") {
    DEBUG_MSG(DLVL_FAIL, "The URL must start with http://");
    return -1;
  }
  url = url.substr(7);  //found problem if url is to short!! it gives out of range when entering http://meh.meh 

  std::string server = url.substr(0, url.find('/'));
  url = url.substr(url.find('/'));

  if (server.find(':') != std::string::npos) {
    port = atoi(server.substr(server.find(':') + 1).c_str());
    server = server.substr(0, server.find(':'));
  }

  
  long long int startTime = Util::bootSecs();
  long long int abortTime = conf.getInteger("abort");
  
  Socket::Connection conn(server, port, false);

  //url:
  DEBUG_MSG(DLVL_INFO, "url %s server: %s port: %d", url.c_str(), server.c_str(), port);
  std::string urlPrependStuff= url.substr(0, url.rfind("/")+1);
  DEBUG_MSG(DLVL_INFO, "prepend stuff: %s", urlPrependStuff.c_str());
  if (!conn) {
    conn = Socket::Connection(server, port, false);
  }
  unsigned int pos = 0;
  HTTP::Parser H;
  H.url = url;
  H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString());
  H.SendRequest(conn);
  H.Clean();
  while (conn && (!conn.spool() || !H.Read(conn))) {}
  H.BuildResponse();
  
  std::set<seekPos> currentPos;
  std::vector<StreamData> streamData;  
  
  //DEBUG_MSG(DLVL_INFO, "body received: %s", H.body.c_str()); //keeps giving empty stuff :( 
  
 // DEBUG_MSG(DLVL_INFO, "url %s ", url.c_str());
  //std::ifstream in(url.c_str());  
  //std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if(!parseXML(H.body, currentPos,streamData)){
    DEBUG_MSG(DLVL_FAIL, "Manifest parsing failed. body: \n %s", H.body.c_str()); 
    if (conf.getString("mode") == "validate") {
      long long int endTime = Util::bootSecs();
      std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
    }
    return -1;
  }
  
  
  H.Clean();
  DEBUG_MSG(DLVL_INFO, "*********");
  DEBUG_MSG(DLVL_INFO, "*SUMMARY*");
  DEBUG_MSG(DLVL_INFO, "*********");
  
  DEBUG_MSG(DLVL_INFO, "num streams: %lu", streamData.size());
  for(unsigned int i=0; i<streamData.size();i++){
    DEBUG_MSG(DLVL_INFO, "");
    DEBUG_MSG(DLVL_INFO, "ID in vector %d", i);
    DEBUG_MSG(DLVL_INFO, "trackID %ld", streamData[i].trackID);
    DEBUG_MSG(DLVL_INFO, "adaptationSet %d", streamData[i].adaptationSet);
    DEBUG_MSG(DLVL_INFO, "trackType (audio 0x02, video 0x01) %d", streamData[i].trackType);
    DEBUG_MSG(DLVL_INFO, "TimeScale %ld", streamData[i].timeScale);
    DEBUG_MSG(DLVL_INFO, "Media string %s", streamData[i].media.c_str());
    DEBUG_MSG(DLVL_INFO, "Init string %s", streamData[i].initialization.c_str());
  }
  
  DEBUG_MSG(DLVL_INFO, "");   
  
  for(unsigned int i=0; i<streamData.size();i++){ //get init url     
    static char charBuf[512];
    snprintf(charBuf, 512, streamData[i].initialization.c_str(), streamData[i].trackID);
    streamData[i].initURL.assign(charBuf);
    DEBUG_MSG(DLVL_INFO, "init url for adaptationSet %d trackID %ld: %s ", streamData[i].adaptationSet, streamData[i].trackID, streamData[i].initURL.c_str());
  }
    

  while(currentPos.size() && (abortTime <= 0 || Util::bootSecs() < startTime + abortTime)){
    //DEBUG_MSG(DLVL_INFO, "next url: %s", currentPos.begin()->url.c_str());
    
    //match adaptation set and track id?
    int tempID=0;
    for(unsigned int i=0; i<streamData.size();i++){
      if( streamData[i].trackID ==  currentPos.begin()->trackID && streamData[i].adaptationSet ==  currentPos.begin()->adaptationSet ) tempID=i;
    }
    if (!conn) {
      conn = Socket::Connection(server,port, false);
    }
    HTTP::Parser H;
    H.url = urlPrependStuff;
    H.url.append(currentPos.begin()->url);    
    DEBUG_MSG(DLVL_INFO, "Retrieving segment: %s (%llu-%llu)", H.url.c_str(),currentPos.begin()->seekTime, currentPos.begin()->seekTime+currentPos.begin()->duration);
    H.SetHeader("Host", server + ":" + JSON::Value((long long)port).toString()); //wut?
    H.SendRequest(conn);
    //TODO: get response?
    H.Clean();
    while (conn && (!conn.spool() || !H.Read(conn))) {}  //ehm...
    //std::cout << "leh vomi: "<<H.body <<std::endl;
    //DEBUG_MSG(DLVL_INFO, "zut: %s", H.body.c_str());
    //strBuf[tempID].append(H.body);
    if(!H.body.size()){
      DEBUG_MSG(DLVL_FAIL, "No data downloaded from %s",H.url.c_str());
      break;
    }
    size_t beforeParse = H.body.size();
    MP4::Box mp4Data;
    bool mdatSeen = false;
    while(mp4Data.read(H.body)){
      if (mp4Data.isType("mdat")){
        mdatSeen = true;
      }
    }
    if (!mdatSeen){
      DEBUG_MSG(DLVL_FAIL, "No mdat present. Sadface. :-(");
      break;
    }
    if(H.body.size()){
      DEBUG_MSG(DLVL_FAIL, "%lu bytes left in body. Assuming horrible things...", H.body.size());//,H.body.c_str());
      std::cerr << H.body << std::endl;
      if (beforeParse == H.body.size()){
        break;
      }
    }        
    H.Clean();
    pos = 1000*(currentPos.begin()->seekTime+currentPos.begin()->duration)/streamData[tempID].timeScale;
    
    if (conf.getString("mode") == "validate" && (Util::bootSecs()-startTime+5)*1000 < pos) {
      Util::wait(pos - (Util::bootSecs()-startTime+5)*1000);
    }
    
    currentPos.erase(currentPos.begin());
  }
  
  if (conf.getString("mode") == "validate") {
    long long int endTime = Util::bootSecs();
    std::cout << startTime << ", " << endTime << ", " << (endTime - startTime) << ", " << pos << std::endl;
  }
  
  return 0;
}
