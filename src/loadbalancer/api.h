#ifndef apifile
#define apifile
#include "communication_defines.h"
#include "server.h"
#include <mist/websocket.h>
#include <mist/config.h>

class LoadBalancer;
struct hostEntry;

const std::string empty = "";
const std::string authDelimiter = ":";
const std::string pathdelimiter = "/";
/**
 * function to select the api function wanted
 */
int handleRequests(Socket::Connection &conn, HTTP::Websocket *webSock, LoadBalancer *LB);
int handleRequest(Socket::Connection &conn); // for starting threads

/**
 * add server to be monitored
 */
void addServer(std::string &ret, const std::string addserver, bool resend);

/**
 * remove server from ?
 */
JSON::Value delServer(const std::string delserver, bool resend);

/**
 * add load balancer to mesh
 */
void addLB(void *p);

/**
 * handle websockets only used for other load balancers
 * \return loadbalancer corrisponding to this socket
 */
LoadBalancer *onWebsocketFrame(HTTP::Websocket *webSock, std::string name, LoadBalancer *LB);

/**
 * set balancing settings
 */
void balance(Util::StringParser path);
void balance(JSON::Value newVals);

/**
 * set and get weights
 */
JSON::Value setWeights(Util::StringParser path, bool resend);
void setWeights(const JSON::Value weights);

/**
 * receive server updates and adds new foreign hosts if needed
 */
void updateHost(const JSON::Value newVals);

/**
 * remove load balancer from mesh
 */
void removeLB(const std::string removeLoadBalancer, bool resend);

/**
 * reconnect to load balancer
 */
void reconnectLB(void *p);

/**
 * returns load balancer list
 */
std::string getLoadBalancerList();

/**
 * return viewer counts of streams
 */
JSON::Value getViewers();

/**
 * return the best source of a stream
 */
void getSource(Socket::Connection conn, HTTP::Parser H, const std::string source,
               const std::string fback, bool repeat);

/**
 * get view count of a stream
 */
uint64_t getStream(const std::string stream);

/**
 * return server list
 */
JSON::Value serverList();

/**
 * return ingest point
 */
void getIngest(Socket::Connection conn, HTTP::Parser H, const std::string ingest,
               const std::string fback, bool repeat);

/**
 * create stream
 */
void stream(Socket::Connection conn, HTTP::Parser H, std::string proto, std::string streamName,
            bool repeat = true);

/**
 * return stream stats
 */
JSON::Value getStreamStats(const std::string streamStats);

/**
 * add viewer to stream on server
 */
void addViewer(const std::string stream, const std::string addViewer);

/**
 * return server data of a server
 */
JSON::Value getHostState(const std::string host);

/**
 * return all server data
 */
JSON::Value getAllHostStates();

/**
 * \returns the config as a string
 */
std::string configToString();
/**
 * save config vars to config file
 * \param resend allows for command to be sent to other load balacners
 */
void saveFile(bool resend = false);
/**
 * timer to check if enough time passed since last config change to save to the config file
 */
void saveTimeCheck(void *);
/**
 * loads the config from a string
 */
void configFromString(std::string s);
/**
 * load config vars from config file
 * \param resend allows for command to be sent sent to other load balancers
 */
void loadFile(bool resend = false);

#endif // apifile