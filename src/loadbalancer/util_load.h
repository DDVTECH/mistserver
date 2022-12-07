#ifndef util_load
#define util_load

#include "communication_defines.h"
#include "server.h"

// rebalancing
extern int minstandby;
extern int maxstandby;
extern double cappacityTriggerCPUDec; // percentage om cpu te verminderen
extern double cappacitytriggerBWDec;  // percentage om bandwidth te verminderen
extern double cappacityTriggerRAMDec; // percentage om ram te verminderen
extern double cappacityTriggerCPU;    // max capacity trigger for balancing cpu
extern double cappacityTriggerBW;     // max capacity trigger for balancing bandwidth
extern double cappacityTriggerRAM;    // max capacity trigger for balancing ram
extern double highCappacityTriggerCPU; // capacity at which considerd almost full. should be less than cappacityTriggerCPU
extern double highCappacityTriggerBW; // capacity at which considerd almost full. should be less than cappacityTriggerBW
extern double highCappacityTriggerRAM; // capacity at which considerd almost full. should be less than cappacityTriggerRAM
extern double lowCappacityTriggerCPU; // capacity at which considerd almost empty. should be less than cappacityTriggerCPU
extern double lowCappacityTriggerBW; // capacity at which considerd almost empty. should be less than cappacityTriggerBW
extern double lowCappacityTriggerRAM; // capacity at which considerd almost empty. should be less than cappacityTriggerRAM
extern int balancingInterval;
extern int serverMonitorLimit;

#define SALTSIZE 10

// file save and loading vars
extern std::string const fileloc;
extern tthread::thread *saveTimer;
extern std::time_t prevconfigChange; // time of last config change
extern std::time_t prevSaveTime;     // time of last save

// timer vars
extern int prometheusMaxTimeDiff;  // time prometheusnodes stay in system
extern int prometheusTimeInterval; // time prometheusnodes receive data
extern int saveTimeInterval;       // time to save after config change in minutes
extern std::time_t now;

// authentication storage
extern std::map<std::string, std::pair<std::string, std::string> > userAuth; // username: (passhash, salt)
extern std::set<std::string> bearerTokens;
extern std::string passHash;
extern std::set<std::string> whitelist;
extern std::map<std::string, std::time_t> activeSalts;

extern std::string passphrase;
extern std::string fallback;
extern std::string myName;
extern tthread::mutex fileMutex;

/**
 * prometheus data sorted in PROMETHEUSTIMEINTERVA minute intervals
 * each node is stored for PROMETHEUSMAXTIMEDIFF minutes
 */
struct prometheusDataNode{
  std::map<std::string, int> numStreams; // number of new streams connected by this load balancer

  int numSuccessViewer; // new viewer redirects preformed without problem
  int numIllegalViewer; // new viewer redirect requests for stream that doesn't exist
  int numFailedViewer;  // new viewer redirect requests the load balancer can't forfill

  int numSuccessSource; // request best source for stream that occured without problem
  int numFailedSource;  // request best source for stream that can't be forfilled or doesn't exist

  int numSuccessIngest; // http api requests that occured without problem
  int numFailedIngest;  // http api requests the load balancer can't forfill

  std::map<std::string, int> numReconnectServer; // number of times a reconnect is initiated by this load balancer to a server
  std::map<std::string, int> numSuccessConnectServer; // number of times this load balancer successfully connects to a server
  std::map<std::string, int> numFailedConnectServer; // number of times this load balancer failed to connect to a server

  std::map<std::string, int> numReconnectLB; // number of times a reconnect is initiated by this load balancer to another load balancer
  std::map<std::string, int> numSuccessConnectLB; // number of times a load balancer successfully connects to this load balancer
  std::map<std::string, int> numFailedConnectLB; // number of times a load balancer failed to connect to this load balancer

  int numSuccessRequests; // http api requests that occured without problem
  int numIllegalRequests; // http api requests that don't exist
  int numFailedRequests;  // http api requests the load balancer can't forfill

  int numLBSuccessRequests; // websocket requests that occured without problem
  int numLBIllegalRequests; // webSocket requests that don't exist
  int numLBFailedRequests;  // webSocket requests the load balancer can't forfill

  int badAuth;  // number of failed logins
  int goodAuth; // number of successfull logins
}typedef prometheusDataNode;

extern prometheusDataNode lastPromethNode;
extern std::map<time_t, prometheusDataNode> prometheusData;

/**
 * creates new prometheus data node every PROMETHEUSTIMEINTERVAL
 */
void prometheusTimer(void *);

/**
 * return JSON with all prometheus data nodes
 */
JSON::Value handlePrometheus();

/**
 * timer to send the add viewer data
 */
void timerAddViewer(void *);

/**
 * redirects traffic away
 */
bool redirectServer(struct hostEntry *H, bool empty);
/**
 * grabs server from standby and if minstandby reached calls trigger LOAD_OVER
 */
void extraServer();
/**
 * puts server in standby mode and if max standby is reached calss trigger LOAD_UNDER
 */
void reduceServer(struct hostEntry *H);
/**
 * checks if redirect needs to happen
 * prevents servers from going online when still balancing the servers
 */
void checkNeedRedirect(void *);

/**
 * monitor server
 * \param hostEntryPointer a hostEntry with hostDetailsCalc on details field
 */
void handleServer(void *hostEntryPointer);

/**
 * create new server without starting it
 */
void initNewHost(hostEntry &H, const std::string &N);
/**
 * setup new server for monitoring (with hostDetailsCalc class)
 * \param N gives server name
 * \param H is the host entry being setup
 */
void initHost(hostEntry &H, const std::string &N);
/**
 * Setup foreign host (with hostDetails class)
 * \param N gives server name
 */
void initForeignHost(const std::string &N);
/**
 * remove monitored server or foreign server at \param H
 */
void cleanupHost(hostEntry &H);

/// Fills the given map with the given JSON string of tag adjustments
void fillTagAdjust(std::map<std::string, int32_t> &tags, const std::string &adjust);

/**
 * generate random string using time and process id
 */
std::string generateSalt();

/**
 * \returns the identifiers of the load balancers that need to monitor the server in \param H
 */
std::set<std::string> hostNeedsMonitoring(hostEntry H);
/**
 * changes host to correct monitor state
 */
void checkServerMonitors();

#endif // util_load
