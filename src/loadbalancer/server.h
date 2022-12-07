#ifndef server
#define server
#include "communication_defines.h"

#define STATE_OFF 0
#define STATE_BOOT 1
#define STATE_ERROR 2
#define STATE_ONLINE 3
#define STATE_GODOWN 4
#define STATE_REQCLEAN 5
#define STATE_ACTIVE 6
extern const char *stateLookup[];
#define HOSTNAMELEN 1024
extern tthread::mutex globalMutex;

// server balancing weights
extern size_t weight_cpu;
extern size_t weight_ram;
extern size_t weight_bw;
extern size_t weight_geo;
extern size_t weight_bonus;

extern Util::Config *cfg;

extern std::map<std::string, int32_t> blankTags;

/**
 * class to keep track of stream data
 */
class streamDetails{
public:
  uint64_t total;
  uint32_t inputs;
  uint32_t bandwidth;
  uint64_t prevTotal;
  uint64_t bytesUp;
  uint64_t bytesDown;
  streamDetails(){};
  streamDetails(uint64_t total, uint32_t inputs, uint32_t bandwidth, uint64_t prevTotal,
                uint64_t bytesUp, uint64_t bytesDown)
      : total(total), inputs(inputs), bandwidth(bandwidth), prevTotal(prevTotal), bytesUp(bytesUp),
        bytesDown(bytesDown){};
  JSON::Value stringify() const;
  streamDetails *destringify(JSON::Value j);
};

/**
 * class to store the url of a stream
 */
class outUrl{
public:
  std::string pre, post;
  outUrl();
  ;
  outUrl(const std::string &u, const std::string &host);
  JSON::Value stringify() const;
  outUrl destringify(JSON::Value j);
};

/**
 * class to handle communication with other load balancers
 * connection needs to be authenticated beforehand
 */
class LoadBalancer{
private:
  tthread::recursive_mutex mutable *LoadMutex;
  HTTP::Websocket *ws;
  std::string name;
  std::string ident;

public:
  bool mutable state;
  bool mutable Go_Down;
  LoadBalancer(HTTP::Websocket *ws, std::string name, std::string ident);
  virtual ~LoadBalancer();

  std::string getIdent() const;
  std::string getName() const;
  bool operator<(const LoadBalancer &other) const;
  bool operator>(const LoadBalancer &other) const;
  bool operator==(const LoadBalancer &other) const;
  bool operator==(const std::string &other) const;

  void send(std::string ret) const;
};

extern std::set<LoadBalancer *> loadBalancers; // array holding all load balancers in the mesh
extern std::string identifier;
extern std::set<std::string> identifiers;

int32_t applyAdjustment(const std::set<std::string> &tags, const std::string &match, int32_t adj);

/**
 * this class is a server not necessarilly monitored by this load balancer
 * this load balancer does not need to monitor a server for this class
 */
class hostDetails{
private:
  JSON::Value fillStateOut;
  JSON::Value fillStreamsOut;
  uint64_t scoreSource;
  uint64_t scoreRate;
  uint64_t toAddB;

protected:
  std::map<std::string, streamDetails> streams;
  tthread::recursive_mutex mutable *hostMutex;
  std::map<std::string, outUrl> outputs;
  std::set<std::string> conf_streams;
  std::set<std::string> tags;
  char *name;
  uint64_t ramCurr;
  uint64_t ramMax;
  uint64_t cpu;
  uint64_t currBandwidth;

public:
  uint64_t availBandwidth;
  uint64_t prevAddBandwidth;
  uint64_t addBandwidth;
  char binHost[16];
  std::string host;
  double servLati, servLongi;
  int balanceCPU;
  int balanceRAM;
  int balanceBW;
  std::string balanceRedirect;

  hostDetails(char *name);
  virtual ~hostDetails();

  uint64_t getAddBandwidth();

  uint64_t getAvailBandwidth(){return availBandwidth;}
  uint64_t getRamCurr(){return ramCurr;}
  uint64_t getRamMax(){return ramMax;}
  uint64_t getCpu(){return cpu;}
  uint64_t getCurrBandwidth(){return currBandwidth;}

  JSON::Value getServerData();

  /**
   *  Fills out a by reference given JSON::Value with current state.
   */
  void fillState(JSON::Value &r) const;

  /**
   * Fills out a by reference given JSON::Value with current streams viewer count.
   */
  void fillStreams(JSON::Value &r) const;

  /**
   * Fills out a by reference given JSON::Value with current stream statistics.
   */
  void fillStreamStats(const std::string &s, JSON::Value &r) const;

  long long getViewers(const std::string &strm) const;

  uint64_t rate(std::string &s, const std::map<std::string, int32_t> &tagAdjust, double lati = 0,
                double longi = 0) const;

  uint64_t source(const std::string &s, const std::map<std::string, int32_t> &tagAdjust,
                  uint32_t minCpu, double lati, double longi) const;

  std::string getUrl(std::string &s, std::string &proto) const;

  /**
   * Sends update to original load balancer to add a viewer.
   */
  void virtual addViewer(std::string &s, bool resend);

  /**
   * Update precalculated host vars.
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
              uint64_t scoreRate, std::map<std::string, outUrl> outputs, std::set<std::string> conf_streams,
              std::map<std::string, streamDetails> streams, std::set<std::string> tags, uint64_t cpu,
              double servLati, double servLongi, const char *binHost, std::string host, uint64_t toAdd,
              uint64_t currBandwidth, uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax);

  /**
   * Update precalculated host vars without protected vars
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
              uint64_t scoreRate, uint64_t toAdd);

  /**
   * allow for json inputs instead of sets and maps for update function
   */
  void update(JSON::Value fillStateOut, JSON::Value fillStreamsOut, uint64_t scoreSource,
              uint64_t scoreRate, JSON::Value outputs, JSON::Value conf_streams,
              JSON::Value streams, JSON::Value tags, uint64_t cpu, double servLati,
              double servLongi, const char *binHost, std::string host, uint64_t toAdd,
              uint64_t currBandwidth, uint64_t availBandwidth, uint64_t currRam, uint64_t ramMax);
};

/**
 * Class to calculate all precalculated vars of its parent class
 * Monitored hostDetails class
 */
class hostDetailsCalc : public hostDetails{
private:
  uint64_t total;
  uint64_t upPrev;
  uint64_t downPrev;
  uint64_t prevTime;
  uint64_t upSpeed;
  uint64_t downSpeed;

public:
  JSON::Value geoDetails;
  std::string servLoc;

  hostDetailsCalc(char *name);
  virtual ~hostDetailsCalc();

  void badNess();

  /// Fills out a by reference given JSON::Value with current state.
  void fillState(JSON::Value &r) const;

  /// Fills out a by reference given JSON::Value with current streams viewer count.
  void fillStreams(JSON::Value &r) const;

  /// Scores a potential new connection to this server
  /// 0 means not possible, the higher the better.
  uint64_t rate() const;

  /// Scores this server as a source
  /// 0 means not possible, the higher the better.
  uint64_t source() const;

  /**
   * calculate and update precalculated variables
   */
  void calc();

  /**
   * update vars from server
   */
  void update(JSON::Value &d);
};

/// Fixed-size struct for holding a host's name and details pointer
struct hostEntry{
  uint8_t state; // 0 = off, 1 = booting, 2 = running, 3 = requesting shutdown, 4 = requesting clean
  char name[HOSTNAMELEN];  // host+port for server
  hostDetails *details;    /// hostDetails pointer
  tthread::thread *thread; /// thread pointer
  bool standByLock;
}typedef hostEntry;

extern std::set<hostEntry *> hosts; // array holding all hosts

/**
 * remove a server from standby mode
 * cannot remove a locked standby server
 */
void removeStandBy(hostEntry *H);

/**
 * puts a server in standby mode
 * \param lock if standby mode needs to be locked
 * \param H the server this needs to act on
 */
void setStandBy(hostEntry *H, bool lock);

/**
 * convert degrees to radians
 */
inline double toRad(double degree);
/**
 * calcuate distance between 2 coordinates
 */
double geoDist(double lat1, double long1, double lat2, double long2);

/**
 * convert maps<string, object> \param s to json where the object has a stringify function
 */
template <typename data> JSON::Value convertMapToJson(std::map<std::string, data> s);

#endif // server