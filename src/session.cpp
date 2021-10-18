#include <mist/defines.h>
#include <mist/stream.h>
#include <mist/util.h>
#include <mist/config.h>
#include <mist/auth.h>
#include <mist/comms.h>
#include <mist/triggers.h>
#include <signal.h>
#include <stdio.h>
// Stats of connections which have closed are added to these global counters
uint64_t globalNow = 0;
uint64_t globalTime = 0;
uint64_t globalDown = 0;
uint64_t globalUp = 0;
uint64_t globalPktcount = 0;
uint64_t globalPktloss = 0;
uint64_t globalPktretrans = 0;
// Counts the duration a connector has been active
std::map<std::string, uint64_t> connectorCount;
std::map<std::string, uint64_t> connectorLastActive;
// Set to True when a session gets invalidated, so that we know to run a new USER_NEW trigger
bool forceTrigger = false;
void handleSignal(int signum){
  if (signum == SIGUSR1){
    forceTrigger = true;
  }
}

void userOnActive(uint64_t &connections){
  ++connections;
}

std::string getEnvWithDefault(const std::string variableName, const std::string defaultValue){
    const char* value = getenv(variableName.c_str());
    if (value){
      unsetenv(variableName.c_str());
      return value;
    }else{
      return defaultValue;
    }
}

/// \brief Adds stats of closed connections to global counters
void userOnDisconnect(Comms::Connections & connections, size_t idx){
  std::string thisConnector = connections.getConnector(idx);
  if (thisConnector != ""){
    connectorCount[thisConnector] += connections.getTime(idx);
  }
  globalTime += connections.getTime(idx);
  globalDown += connections.getDown(idx);
  globalUp += connections.getUp(idx);
  globalPktcount += connections.getPacketCount(idx);
  globalPktloss += connections.getPacketLostCount(idx);
  globalPktretrans += connections.getPacketRetransmitCount(idx);
}

int main(int argc, char **argv){
  Comms::Connections connections;
  Comms::Sessions sessions;
  uint64_t lastSeen = Util::bootSecs();
  uint64_t currentConnections = 0;
  Util::redirectLogsIfNeeded();
  signal(SIGUSR1, handleSignal);
  // Init config and parse arguments
  Util::Config config = Util::Config("MistSession");
  JSON::Value option;

  option.null();
  option["arg_num"] = 1;
  option["arg"] = "string";
  option["help"] = "Session identifier of the entire session";
  option["default"] = "";
  config.addOption("sessionid", option);

  option.null();
  option["long"] = "sessionmode";
  option["short"] = "m";
  option["arg"] = "integer";
  option["default"] = 0;
  config.addOption("sessionmode", option);

  option.null();
  option["long"] = "streamname";
  option["short"] = "n";
  option["arg"] = "string";
  option["default"] = "";
  config.addOption("streamname", option);

  option.null();
  option["long"] = "ip";
  option["short"] = "i";
  option["arg"] = "string";
  option["default"] = "";
  config.addOption("ip", option);

  option.null();
  option["long"] = "sid";
  option["short"] = "s";
  option["arg"] = "string";
  option["default"] = "";
  config.addOption("sid", option);

  option.null();
  option["long"] = "protocol";
  option["short"] = "p";
  option["arg"] = "string";
  option["default"] = "";
  config.addOption("protocol", option);

  option.null();
  option["long"] = "requrl";
  option["short"] = "r";
  option["arg"] = "string";
  option["default"] = "";
  config.addOption("requrl", option);

  config.activate();
  if (!(config.parseArgs(argc, argv))){
    FAIL_MSG("Cannot start a new session due to invalid arguments");
    return 1;
  }

  const uint64_t bootTime = Util::getMicros();
  // Get session ID, session mode and other variables used as payload for the USER_NEW and USER_END triggers
  const std::string thisStreamName = config.getString("streamname");
  const std::string thisHost = config.getString("ip");
  const std::string thisSid = config.getString("sid");
  const std::string thisProtocol = config.getString("protocol");
  const std::string thisReqUrl = config.getString("requrl");
  const std::string thisSessionId = config.getString("sessionid");
  const uint64_t sessionMode = config.getInteger("sessionmode");

  if (thisSessionId == "" || thisProtocol == "" || thisStreamName == ""){
    FAIL_MSG("Given the following incomplete arguments: SessionId: '%s', protocol: '%s', stream name: '%s'. Aborting opening a new session",
    thisSessionId.c_str(), thisProtocol.c_str(), thisStreamName.c_str());
    return 1;
  }

  MEDIUM_MSG("Starting a new session for sessionId '%s'", thisSessionId.c_str());
  if (sessionMode < 1 || sessionMode > 15) {
    FAIL_MSG("Invalid session mode of value %lu. Should be larger than 0 and smaller than 16", sessionMode);
    return 1;
  }

  // Try to lock to ensure we are the only process initialising this session
  IPC::semaphore sessionLock;
  char semName[NAME_BUFFER_SIZE];
  snprintf(semName, NAME_BUFFER_SIZE, SEM_SESSION, thisSessionId.c_str());
  sessionLock.open(semName, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  // If the lock fails, the previous Session process must've failed in spectacular fashion
  // It's the Controller's task to clean everything up. When the lock fails, this cleanup hasn't happened yet
  if (!sessionLock.tryWaitOneSecond()){
    FAIL_MSG("Session '%s' already locked", thisSessionId.c_str());
    return 1;
  }

  // Check if a page already exists for this session ID. If so, quit
  {
    IPC::sharedPage dataPage;
    char userPageName[NAME_BUFFER_SIZE];
    snprintf(userPageName, NAME_BUFFER_SIZE, COMMS_SESSIONS, thisSessionId.c_str());
    dataPage.init(userPageName, 0, false, false);
    if (dataPage){
      INFO_MSG("Session '%s' already has a running process", thisSessionId.c_str());
      sessionLock.post();
      return 0;
    }
  }
  
  // Claim a spot in shared memory for this session on the global statistics page
  sessions.reload();
  if (!sessions){
    FAIL_MSG("Unable to register entry for session '%s' on the stats page", thisSessionId.c_str());
    sessionLock.post();
    return 1;
  }
  // Open the shared memory page containing statistics for each individual connection in this session
  connections.reload(thisStreamName, thisHost, thisSid, thisProtocol, thisReqUrl, sessionMode, true, false);
  // Initialise global session data
  sessions.setHost(thisHost);
  sessions.setSessId(thisSessionId);
  sessions.setStream(thisStreamName);
  sessionLock.post();

  // Determine session type, since triggers only get run for viewer type sessions
  uint64_t thisType = 0;
  if (thisSessionId[0] == 'I'){
    INFO_MSG("Started new input session %s in %lu microseconds", thisSessionId.c_str(), Util::getMicros(bootTime));
    thisType = 1;
  }
  else if (thisSessionId[0] == 'O'){
    INFO_MSG("Started new output session %s in %lu microseconds", thisSessionId.c_str(), Util::getMicros(bootTime));
    thisType = 2;
  }
  else{
    INFO_MSG("Started new viewer session %s in %lu microseconds", thisSessionId.c_str(), Util::getMicros(bootTime));
  }

  // Do a USER_NEW trigger if it is defined for this stream
  if (!thisType && Triggers::shouldTrigger("USER_NEW", thisStreamName)){
    std::string payload = thisStreamName + "\n" + thisHost + "\n" +
                          thisSid + "\n" + thisProtocol +
                          "\n" + thisReqUrl + "\n" + thisSessionId;
    if (!Triggers::doTrigger("USER_NEW", payload, thisStreamName)){
      // Mark all connections of this session as finished, since this viewer is not allowed to view this stream
      connections.setExit();
      connections.finishAll();
    }
  }

  uint64_t lastSecond = 0;
  uint64_t now = 0;
  uint64_t time = 0;
  uint64_t down = 0;
  uint64_t up = 0;
  uint64_t pktcount = 0;
  uint64_t pktloss = 0;
  uint64_t pktretrans = 0;
  std::string connector = "";
  // Stay active until Mist exits or we no longer have an active connection
  while (config.is_active && (currentConnections || Util::bootSecs() - lastSeen <= 10)){
    time = 0;
    connector = "";
    down = 0;
    up = 0;
    pktcount = 0;
    pktloss = 0;
    pktretrans = 0;
    currentConnections = 0;

    // Count active connections
    COMM_LOOP(connections, userOnActive(currentConnections), userOnDisconnect(connections, id));
    // Loop through all connection entries to get a summary of statistics
    for (uint64_t idx = 0; idx < connections.recordCount(); idx++){
      if (connections.getStatus(idx) == COMM_STATUS_INVALID || connections.getStatus(idx) & COMM_STATUS_DISCONNECT){continue;}
      uint64_t thisLastSecond = connections.getLastSecond(idx);
      std::string thisConnector = connections.getConnector(idx);
      // Save info on the latest active connection separately
      if (thisLastSecond > lastSecond){
        lastSecond = thisLastSecond;
        now = connections.getNow(idx);
      }
      connectorLastActive[thisConnector] = thisLastSecond;
      // Sum all other variables
      time += connections.getTime(idx);
      down += connections.getDown(idx);
      up += connections.getUp(idx);
      pktcount += connections.getPacketCount(idx);
      pktloss += connections.getPacketLostCount(idx);
      pktretrans += connections.getPacketRetransmitCount(idx);
    }

    // Convert connector duration to string
    std::stringstream connectorSummary;
    bool addDelimiter = false;
    connectorSummary << "{";
    for (std::map<std::string, uint64_t>::iterator it = connectorLastActive.begin();
          it != connectorLastActive.end(); ++it){
      if (lastSecond - it->second < 10000){
        connectorSummary << (addDelimiter ? "," : "") << it->first;
        addDelimiter = true;
      }
    }
    connectorSummary << "}";

    // Write summary to global statistics
    sessions.setTime(time + globalTime);
    sessions.setDown(down + globalDown);
    sessions.setUp(up + globalUp);
    sessions.setPacketCount(pktcount + globalPktcount);
    sessions.setPacketLostCount(pktloss + globalPktloss);
    sessions.setPacketRetransmitCount(pktretrans + globalPktretrans);
    sessions.setLastSecond(lastSecond);
    sessions.setConnector(connectorSummary.str());
    sessions.setNow(now);

    // Retrigger USER_NEW if a re-sync was requested
    if (!thisType && forceTrigger){
      forceTrigger = false;
      if (Triggers::shouldTrigger("USER_NEW", thisStreamName)){
        INFO_MSG("Triggering USER_NEW for stream %s", thisStreamName.c_str());
        std::string payload = thisStreamName + "\n" + thisHost + "\n" +
                              thisSid + "\n" + thisProtocol +
                              "\n" + thisReqUrl + "\n" + thisSessionId;
        if (!Triggers::doTrigger("USER_NEW", payload, thisStreamName)){
          INFO_MSG("USER_NEW rejected stream %s", thisStreamName.c_str());
          connections.setExit();
          connections.finishAll();
        }else{
          INFO_MSG("USER_NEW accepted stream %s", thisStreamName.c_str());
        }
      }
    }

    // Invalidate connections if the session is marked as invalid
    if(connections.getExit()){
      connections.finishAll();
      break;
    }
    // Remember latest activity so we know when this session ends
    if (currentConnections){
      lastSeen = Util::bootSecs();
    }
    Util::sleep(1000);
  }

  // Trigger USER_END
  if (!thisType && Triggers::shouldTrigger("USER_END", thisStreamName)){
    lastSecond = 0;
    time = 0;
    down = 0;
    up = 0;

    // Get a final summary of this session
    for (uint64_t idx = 0; idx < connections.recordCount(); idx++){
      if (connections.getStatus(idx) == COMM_STATUS_INVALID || connections.getStatus(idx) & COMM_STATUS_DISCONNECT){continue;}
      uint64_t thisLastSecond = connections.getLastSecond(idx);
      // Set last second to the latest entry
      if (thisLastSecond > lastSecond){
        lastSecond = thisLastSecond;
      }
      // Count protocol durations across the entire session
      std::string thisConnector = connections.getConnector(idx);
      if (thisConnector != ""){
        connectorCount[thisConnector] += connections.getTime(idx);
      }
      // Sum all other variables
      time += connections.getTime(idx);
      down += connections.getDown(idx);
      up += connections.getUp(idx);
    }

    // Convert connector duration to string
    std::stringstream connectorSummary;
    bool addDelimiter = false;
    connectorSummary << "{";
    for (std::map<std::string, uint64_t>::iterator it = connectorCount.begin();
          it != connectorCount.end(); ++it){
      connectorSummary << (addDelimiter ? "," : "") << it->first << ":" << it->second;
      addDelimiter = true;
    }
    connectorSummary << "}";

    const uint64_t duration = lastSecond - (bootTime / 1000);
    std::stringstream summary;
    summary << thisSessionId << "\n"
          << thisStreamName << "\n"
          << connectorSummary.str() << "\n"
          << thisHost << "\n"
          << duration << "\n"
          << up << "\n"
          << down << "\n"
          << sessions.getTags();
    Triggers::doTrigger("USER_END", summary.str(), thisStreamName);
  }

  if (!thisType && connections.getExit()){
    WARN_MSG("Session %s has been invalidated since it is not allowed to view stream %s", thisSessionId.c_str(), thisStreamName.c_str());
    uint64_t sleepStart = Util::bootSecs();
    // Keep session invalidated for 10 minutes, or until the session stops
    while (config.is_active && sleepStart - Util::bootSecs() < 600){
      Util::sleep(1000);
    }
  }
  INFO_MSG("Shutting down session %s", thisSessionId.c_str());
  return 0;
}
