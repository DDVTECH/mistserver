#include "output_push.h"
#include <mist/http_parser.h>
#include <mist/shared_memory.h>
#include <sys/stat.h>
#include <mist/tinythread.h>


#define PUSH_INDEX_SIZE 5 //Build index based on most recent X segments

Util::Config * pConf;
std::string sName;

std::string baseURL;

long long srcPort;
std::string srcHost;

std::string dstHost;
long long dstPort;
std::string dstUrl;

//Used to keep track of all segments that can be pushed
std::map<std::string, std::map<int, std::string> > pushableSegments;
//Used to keep track of the timestamp of each pushableSegment
std::map<std::string, std::map<int, int> > pushableTimes;
//Used to keep track of the duration of each pushableSegment
std::map<std::string, std::map<int, int> > pushableDurations;


//For each quality, store the latest number found in the push list
std::map<std::string, int> latestNumber;

//For each quality, store whether it is currently being pushed.
std::map<std::string, bool> parsing;

//For each quality, store an fprint-style string of the relative url to the index_<beginTime>_<endTime>.m3u8
std::map<std::string, std::string> qualityIndex;
//For each quality, store an fprint-style string of the relative url to the segment.
std::map<std::string, std::string> qualitySegment;

//For each quality, store the last PUSH_INDEX_SIZE - 1 timestamps. Used to generate a time-constrained index.m3u8.
std::map<std::string, std::deque<int> > qualityBeginTimes;


//Parses a uri of the form 'http://<host>[:<port>]/<url>, and split it into variables
void parseURI(const std::string & uri, std::string & host, long long & port, std::string & url){
  int loc = 0;
  if (uri.find("http://") == 0){
    loc += 7;
  }
  host = uri.substr(loc, uri.find_first_of(":/", 7) - 7);
  loc += host.size();
  if (uri[loc] == ':'){
    port = atoll(uri.c_str() + loc + 1);
    loc = uri.find("/", loc);
  }
  url = uri.substr(loc);
}

//Do an HTTP request, and route it into a post request on a different socket.
void proxyToPost(Socket::Connection & src, const std::string & srcUrl, Socket::Connection & dst, const std::string & dstUrl){
  INFO_MSG("Routing %s to %s", srcUrl.c_str(), dstUrl.c_str());
  
  //Send the initial request
  HTTP::Parser H;
  H.url = srcUrl;
  H.SendRequest(src);
  H.Clean();

  //Read only the headers of the reply
  H.headerOnly = true;
  while (src.connected()){
    if (src.Received().size() || src.spool()){
      if (H.Read(src)){
        break;
      }
    }
  }
  H.headerOnly = false;

  INFO_MSG("Reply from %s: %s %s", src.getHost().c_str(), H.url.c_str(), H.method.c_str());

  //Change the headers of the reply to form a post request
  H.method = "POST";
  H.url = dstUrl;
  H.protocol = "HTTP/1.1";
  H.SetHeader("Host", dstHost);
  //Start the post request
  H.SendRequest(dst);
  //Route the original payload.
  H.Proxy(src, dst);

  H.Clean();
  while (dst.connected()){
    if (dst.Received().size() || dst.spool()){
      if (H.Read(dst)){
        break;
      }
    }
  }
  INFO_MSG("Reply from %s: %s %s", dst.getHost().c_str(), H.url.c_str(), H.method.c_str());
}


///Push the first registered segment for this quality
void pushFirstElement(std::string qId) {
  std::string semName = "MstPushLock" + sName;
  IPC::semaphore pushLock(semName.c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);

  std::string url;
  int time;
  int beginTime;
  int duration;
  //Wait for exclusive access to all globals
  pushLock.wait();
  //Retrieve all globals for the segment to be pushed
  if (pushableSegments[qId].size()){
    url = pushableSegments[qId].begin()->second;
    time = pushableTimes[qId].begin()->second;
    duration = pushableDurations[qId].begin()->second;
    if (qualityBeginTimes[qId].size()){
      beginTime = qualityBeginTimes[qId].front();
    }else{
      beginTime = time;
    }
  }
  //Give up exclusive access to all globals
  pushLock.post();
  //Return if we do not have a segment to push
  if (url == ""){
    return;
  }

  //Create both source and destination connections
  Socket::Connection srcConn(srcHost, srcPort, true);
  Socket::Connection dstConn(dstHost, dstPort, true);

  //Set the locations to push to for this segment
  std::string srcLocation = baseURL + url;
  std::string dstLocation = dstUrl.substr(0, dstUrl.rfind("/")) + url;

  //Push the segment
  proxyToPost(srcConn, srcLocation, dstConn, dstLocation);
  
  
  srcConn = Socket::Connection(srcHost, srcPort, true);

  //Set the location to push to for the index containing this segment.
  //The index will contain (at most) the last PUSH_INDEX_SIZE segments.
  char srcIndex[200];
  snprintf(srcIndex, 200, qualityIndex[qId].c_str(), beginTime , time + duration); 
  srcLocation = baseURL + srcIndex;
  dstLocation = dstLocation.substr(0, dstLocation.rfind("/")) + "/index.m3u8";

  //Push the index
  proxyToPost(srcConn, srcLocation, dstConn, dstLocation);


  srcConn = Socket::Connection(srcHost, srcPort, true);

  //Set the location to push to for the global index containing all qualities.
  srcLocation = baseURL + "/push/index.m3u8";
  dstLocation = dstLocation.substr(0, dstLocation.rfind("/"));
  dstLocation = dstLocation.substr(0, dstLocation.rfind("/")) + "/index.m3u8";

  //Push the global index
  proxyToPost(srcConn, srcLocation, dstConn, dstLocation);

  //Close both connections
  ///\todo Make the dstConn "persistent" for each thread?
  srcConn.close();
  dstConn.close();

  //Wait for exclusive access to all globals
  pushLock.wait();
  //Update all globals to indicate the segment has been pushed correctly
  pushableSegments[qId].erase(pushableSegments[qId].begin());
  pushableTimes[qId].erase(pushableTimes[qId].begin());
  pushableDurations[qId].erase(pushableDurations[qId].begin());
  qualityBeginTimes[qId].push_back(time);
  //Remove the first elements fromt he beginTimes map to make sure we have PUSH_INDEX_SIZE elements in our index.
  //We use -1 here, because we use the segment to currently push as well as everything stored in the map
  while (qualityBeginTimes[qId].size() > PUSH_INDEX_SIZE - 1){
    qualityBeginTimes[qId].pop_front();
  }
  //Give up exclusive access to all globals
  pushLock.post();
}

///Thread used to push data.
void pushThread(void * nullPointer){
  std::string myThread;

  //Attempt to claim a non-claimed quality.
  std::string semName = "MstPushClaim" + sName;
  IPC::semaphore pushThreadLock(semName.c_str(), O_CREAT | O_RDWR, ACCESSPERMS, 1);

  pushThreadLock.wait();
  for (std::map<std::string, std::map<int, std::string> >::iterator it = pushableSegments.begin(); it != pushableSegments.end(); it++){
    if (it->second.size()){//Make sure we dont try to "claim" pushing an empty track
      if (!parsing.count(it->first) || !parsing[it->first]){
        INFO_MSG("Claiming thread %s", it->first.c_str());
        myThread = it->first;
        parsing[it->first] = true;
        break;
      }
    }
  }
  pushThreadLock.post();

  //Return if we were unable to claim a quality
  if (myThread == ""){
    INFO_MSG("No thread claimed");
    return;
  }

  //While this output is active, push the first element in the list
  while (pConf->is_active){
    pushFirstElement(myThread);
    if (!pushableSegments[myThread].size()){
      Util::wait(1000);
    }
  }

  parsing[myThread] = false;
}


namespace Mist {

  OutPush::OutPush(Socket::Connection & conn) : Output(conn){
    config->activate();
  }
  OutPush::~OutPush(){}
  
  void OutPush::requestHandler() {
    //Set aal basic data only the first time.
    if (streamName == ""){
      srcPort = 80;
      parseURI(config->getString("pushlist"), srcHost, srcPort, pushURL);
      dstPort = 80;
      parseURI(config->getString("destination"), dstHost, dstPort, dstUrl);


      //Strip "/push/list" from the URL
      baseURL = pushURL.substr(0, pushURL.rfind("/"));
      baseURL = baseURL.substr(0, baseURL.rfind("/"));

      //Locate the streamname from the pushURL
      int loc = baseURL.find("/", 1) + 1;
      streamName = pushURL.substr(loc, pushURL.rfind("/") - loc);
      sName = streamName;

      INFO_MSG("host: %s, port %lld, url %s, baseURL %s, streamName %s", srcHost.c_str(), srcPort, pushURL.c_str(), baseURL.c_str(), streamName.c_str());
    }
    //Reconnect when disconnected
    if (!listConn.connected()){
      listConn = Socket::Connection(srcHost, srcPort, true);
    }
    //Request the push list
    if (listConn.connected()){
      HTTP::Parser hReq;
      hReq.url = baseURL + "/push/list";
      hReq.SendRequest(listConn);
      hReq.Clean();
      //Read the entire response, not just the headers!
      while (!hReq.Read(listConn) && listConn.connected()){
        Util::sleep(100);
        listConn.spool();
      }

      //Construct and parse the json list
      JSON::Value reply = JSON::fromString(hReq.body);
      int numQualities = reply["qualities"].size();
      for (int i = 0; i < numQualities; i++){
        JSON::Value & qRef = reply["qualities"][i];
        std::string qId = qRef["id"].asString();

        //Set both the index and segment urls when not yet set.
        if (!qualityIndex.count(qId)){
          qualityIndex[qId] = qRef["index"].asString();
          qualitySegment[qId] = qRef["segment"].asString();
        }
        
        //Save latest segment number before parsing
        int curLatestNumber = latestNumber[qId];
        
        //Loop over all segments
        for (int j = 0; j < qRef["segments"].size(); j++){
          JSON::Value & segRef = qRef["segments"][j];
          int thisNumber = segRef["number"].asInt();

          //Check if this segment is newer than the newest segment before parsing
          if (thisNumber > curLatestNumber){
            //If it is the highest so far, store its number
            if (thisNumber > latestNumber[qId]){
              latestNumber[qId] = thisNumber;
            }
            //If it is not yet added, add it.
            if (!pushableSegments[qId].count(thisNumber)){
              char segmentUrl[200];
              //The qualitySegment map contains a printf-style string
              snprintf(segmentUrl, 200, qualitySegment[qId].c_str(), segRef["time"].asInt(), segRef["time"].asInt() + segRef["duration"].asInt());
              pushableSegments[qId][segRef["number"].asInt()] = segmentUrl;
              pushableTimes[qId][segRef["number"].asInt()] = segRef["time"].asInt();
              pushableDurations[qId][segRef["number"].asInt()] = segRef["duration"].asInt();
            }
          }
        }
      }
    }
    //Calculate how many qualities are not yet being pushed
    int threadsToSpawn = pushableSegments.size();
    for (std::map<std::string, std::map<int, std::string> >::iterator it = pushableSegments.begin(); it != pushableSegments.end(); it++){
      if (parsing.count(it->first) && parsing[it->first]){
        threadsToSpawn --;
      }
    }
    //And start a thread for each unpushed quality.
    //Threads determine which quality to push for themselves.
    for (int i = 0; i < threadsToSpawn; i++){
      tthread::thread thisThread(pushThread, 0);
      thisThread.detach();
    }
    Util::sleep(100);
  }

  void OutPush::init(Util::Config * cfg){
    Output::init(cfg);
    capa["name"] = "Push";
    capa["desc"] = "Enables HTTP Pushing.";
    capa["required"]["pushlist"]["name"] = "URL location of the pushing list";
    capa["required"]["pushlist"]["help"] = "This is the location that will be checked for pushable data.";
    capa["required"]["pushlist"]["option"] = "--pushlist";
    capa["required"]["pushlist"]["short"] = "p";
    capa["required"]["pushlist"]["type"] = "str";
    capa["required"]["destination"]["name"] = "URL location of the destination";
    capa["required"]["destination"]["help"] = "This is the location that the date will be pushed to.";
    capa["required"]["destination"]["option"] = "--destination";
    capa["required"]["destination"]["short"] = "D";
    capa["required"]["destination"]["type"] = "str";
    cfg->addBasicConnectorOptions(capa);
    pConf = cfg;
    config = cfg;
  }
}
