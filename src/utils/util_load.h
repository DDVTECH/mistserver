#pragma once

#include <mist/config.h>
#include <mist/websocket.h>
#include <mist/tinythread.h>
#include <string>




#define CONFSTREAMSKEY "conf_streams"
#define TAGSKEY "tags"
#define STREAMSKEY "streams"
#define OUTPUTSKEY "outputs"

#define STATE_OFF 0
#define STATE_BOOT 1
#define STATE_ERROR 2
#define STATE_ONLINE 3
#define STATE_GODOWN 4
#define STATE_REQCLEAN 5
const char *stateLookup[] ={"Offline",           "Starting monitoring",
                             "Monitored (error)", "Monitored (online)",
                             "Requesting stop",   "Requesting clean"};
#define HOSTNAMELEN 1024
#define MAXHOSTS 1000
#define LBNAMELEN 1024
#define MAXLB 1000


class delimiterParser{
  private:
  std::string s;
  std::string delimiter;

  public:
  delimiterParser(std::string s, std::string delimiter) : s(s), delimiter(delimiter){}
  std::string next();
  int nextInt();
};

class IpPolicy{
  private:
  std::set<std::string> andp;

  std::string getNextFrame(delimiterParser pol);

  public:
  IpPolicy(std::string policy);
  bool match(std::string ip);
};

class streamDetails {
public:
  uint64_t total;
  uint32_t inputs;
  uint32_t bandwidth;
  uint64_t prevTotal;
  uint64_t bytesUp;
  uint64_t bytesDown;
  streamDetails(){};
  streamDetails(uint64_t total, uint32_t inputs, uint32_t bandwidth, uint64_t prevTotal, uint64_t bytesUp, uint64_t bytesDown) : total(total), inputs(inputs), bandwidth(bandwidth), prevTotal(prevTotal), bytesUp(bytesUp), bytesDown(bytesDown){};
  JSON::Value stringify();
  streamDetails* destringify(JSON::Value j);
};


class LoadBalancer {
  private:
  tthread::recursive_mutex mutable *LoadMutex;
  HTTP::Websocket* ws;
  std::string name;
  tthread::thread* t;
  
  public:
  bool mutable Go_Down;
  LoadBalancer(HTTP::Websocket* ws, std::string name);
  virtual ~LoadBalancer();

  std::string getName() const;
  bool operator < (const LoadBalancer &other) const;
  bool operator > (const LoadBalancer &other) const;
  bool operator == (const LoadBalancer &other) const;
  bool operator == (const std::string &other) const;
  
  void send(std::string ret) const;
  

};

class outUrl {
public:
  std::string pre, post;
  outUrl();;
  outUrl(const std::string &u, const std::string &host);
  JSON::Value stringify();
  outUrl destringify(JSON::Value j);
};


/** 
 * this class is a host
 * this load balancer does not need to monitor a server for this class 
 */
class hostDetails{
  private:
  JSON::Value fillStateOut;
  JSON::Value fillStreamsOut;
  uint64_t scoreSource;
  uint64_t scoreRate;
  
  std::set<LoadBalancer*> LB;
  
  
  protected:
  std::map<std::string, streamDetails> streams;
  tthread::recursive_mutex *hostMutex;
  uint64_t cpu;
  std::map<std::string, outUrl> outputs;
  std::set<std::string> conf_streams;
  std::set<std::string> tags;
  char* name;

  public:
  char binHost[16];
  std::string host;
  double servLati, servLongi;

  hostDetails(std::set<LoadBalancer*> LB, char* name);
  virtual ~hostDetails();

  /**
   *  Fills out a by reference given JSON::Value with current state.
   */
  void fillState(JSON::Value &r);
  
  /** 
   * Fills out a by reference given JSON::Value with current streams viewer count.
   */
  void fillStreams(JSON::Value &r);

  /**
   * Fills out a by reference given JSON::Value with current stream statistics.
   */ 
  void fillStreamStats(const std::string &s,JSON::Value &r);

  long long getViewers(const std::string &strm);

  uint64_t rate(std::string &s, const std::map<std::string, int32_t>  &tagAdjust, double lati, double longi);

  uint64_t source(const std::string &s, const std::map<std::string, int32_t> &tagAdjust, uint32_t minCpu, double lati, double longi);
  
  std::string getUrl(std::string &s, std::string &proto);
  
  /**
   * Sends update to original load balancer to add a viewer.
   */
  void virtual addViewer(std::string &s, bool resend);
 
  /**
   * Update precalculated host vars.
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, std::map<std::string, outUrl> outputs, std::set<std::string> conf_streams, std::map<std::string, streamDetails> streams, std::set<std::string> tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host);
  
    /**
   * Update precalculated host vars without protected vars
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate);
  
  /**
   * allow for json inputs instead of sets and maps for update function
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource, uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams, JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati, double servLongi, const char* binHost, std::string host);
  
};

/**
 * Class to calculate all precalculated vars of its parent class.
 * Requires a server to monitor.
 */
class hostDetailsCalc : public hostDetails {
  private:
  uint64_t ramMax;
  uint64_t ramCurr;
  uint64_t upSpeed;
  uint64_t downSpeed;
  uint64_t total;
  uint64_t upPrev;
  uint64_t downPrev;
  uint64_t prevTime;
  uint64_t addBandwidth;

  public:
  uint64_t availBandwidth;
  JSON::Value geoDetails;
  std::string servLoc;

  hostDetailsCalc(char* name);
  virtual ~hostDetailsCalc();
  
  void badNess();

  /// Fills out a by reference given JSON::Value with current state.
  void fillState(JSON::Value &r);

  /// Fills out a by reference given JSON::Value with current streams viewer count.
  void fillStreams(JSON::Value &r);

  /// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
  uint64_t rate();

  /// Scores this server as a source
  /// 0 means not possible, the higher the better.
  uint64_t source();

  /**
   * calculate and update precalculated variables
  */
  void calc();
  
  /**
   * add the viewer to this host
   * updates all precalculated host vars
   */
  void addViewer(std::string &s, bool resend);

  /**
   * update vars from server
   */
  void update(JSON::Value &d);
  
  
};

/// Fixed-size struct for holding a host's name and details pointer
struct hostEntry{
  uint8_t state; // 0 = off, 1 = booting, 2 = running, 3 = requesting shutdown, 4 = requesting clean
  char name[HOSTNAMELEN];          // host+port for server
  hostDetails *details;    /// hostDetails pointer
  tthread::thread *thread; /// thread pointer
};




/**
 * class to encase the api
 */
class API {
public:
  /**
   * function to select the api function wanted
   */
  int static handleRequests(Socket::Connection &conn, HTTP::Websocket* webSock, LoadBalancer* LB);
  int static handleRequest(Socket::Connection &conn);
  
  /**
   * set and get weights
   */
  JSON::Value static setWeights(delimiterParser path);
  void static setWeights(const JSON::Value weights);
  /**
   * remove server from ?
   */
  JSON::Value static delServer(const std::string delserver);

  /**
   * receive server updates and adds new foreign hosts if needed
   */
  void static updateHost(JSON::Value newVals);
  
  /**
   * add server to be monitored
   */
  void static addServer(JSON::Value ret, const std::string addserver);
   
  /**
   * remove server from load balancer( both monitored and foreign )
   */
  void static removeHost(const std::string removeHost);
   
  /**
   * remove load balancer from mesh
   */
  void static removeLB(std::string removeLoadBalancer, const std::string resend);
  
  /**
   * add load balancer to mesh
   */
  void static addLB(void* p);
  



private:
  /**
  * returns load balancer list
  */
  std::string static getLoadBalancerList();
  
  /**
   * return viewer counts of streams
   */
  JSON::Value static getViewers();
  
  /**
   * return the best source of a stream
   */
  void static getSource(Socket::Connection conn, HTTP::Parser H, const std::string source, const std::string fback);
  
  /**
   * get view count of a stream
   */
  uint64_t static getStream(const std::string stream);
  
  /**
   * return server list
   */
  JSON::Value static serverList();
 
  /**
   * return ingest point
   */
  void static getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest, const std::string fback);
  
  /**
   * create stream
   */
  void static stream(Socket::Connection conn, HTTP::Parser H, std::string proto, std::string stream);
  
  /**
   * return stream stats
   */
  JSON::Value static getStreamStats(const std::string streamStats);
 
  /**
   * add viewer to stream on server
   */
  void static addViewer(std::string stream, const std::string addViewer);

  /**
   * return server data of a server
   */
  JSON::Value static getHostState(const std::string host);
  
  /**
   * return all server data
   */
  JSON::Value static getAllHostStates();
};
