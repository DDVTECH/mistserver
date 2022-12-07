#pragma once
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mist/auth.h>
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/encryption.h>
#include <mist/timing.h>
#include <mist/tinythread.h>
#include <mist/triggers.h>
#include <mist/util.h>
#include <mist/websocket.h>
#include <set>
#include <stdint.h>
#include <string>
#include <unistd.h>

// transmision json names
#define confStreamKey "conf_streams"
#define tagKey "tags"
#define streamsKey "streams"
#define outputsKey "outputs"
#define fillstateout "fillStateOut"
#define fillStreamOut "fillStreamsOut"
#define scoreSourceKey "scoreSource"
#define scoreRateKey "scoreRate"
#define cpuKey "cpu"
#define servLatiKey "servLati"
#define servLongiKey "servLongi"
#define hostnameKey "hostName"
#define ramKey "ram"
#define bwKey "bw"
#define geoKey "geo"
#define bonusKey "bonus"
#define saveKey "save"
#define loadKey "load"
#define identifierKey "LB"
#define toAddKey "toadd"
#define currBandwidthKey "currBandwidth"
#define availBandwidthKey "availBandwidth"
#define currRAMKey "currram"
#define ramMaxKey "ramMax"
#define binhostKey "binhost"
#define balanceKey "balance"
#define addUserKey "auser"
#define addPassKey "apass"
#define addSaltKey "asalt"
#define addWhitelistKey "awhitelist"
#define rUserKey "ruser"
#define rPassKey "rpass"
#define rSaltKey "rsalt"
#define rWhitelistKey "rwhitelist"

// balancing transmision json names
#define minstandbyKEY "minstandby"
#define maxstandbyKEY "maxstandby"
#define cappacityTriggerCPUDecKEY "triggerdecrementcpu" // percentage om cpu te verminderen
#define cappacitytriggerBWDecKEY                                                                   \
  "triggerdecrementbandwidth"                           // percentage om bandwidth te verminderen
#define cappacityTriggerRAMDECKEY "triggerdecrementram" // percentage om ram te verminderen
#define cappacityTriggercpuKey "triggercpu"             // max capacity trigger for balancing cpu
#define cappacityTriggerbwKey "triggerbandwidth" // max capacity trigger for balancing bandwidth
#define cappacityTriggerRAMKEY "triggerram"      // max capacity trigger for balancing ram
#define highCappacityTriggercpuKey                                                                 \
  "balancingtriggercpu" // capacity at which considerd almost full. should be less than cappacityTriggerCPU
#define highCappacityTriggerbwKey                                                                  \
  "balancingtriggerbandwidth" // capacity at which considerd almost full. should be less than cappacityTriggerBW
#define highCappacityTriggerRAMKEY                                                                 \
  "balancingtriggerram" // capacity at which considerd almost full. should be less than cappacityTriggerRAM
#define lowCappacityTriggercpuKey                                                                  \
  "balancingminimumtriggercpu" // capacity at which considerd almost full. should be less than cappacityTriggerCPU
#define lowCappacityTriggerbwKey                                                                   \
  "balancingminimumtriggerbandwidth" // capacity at which considerd almost full. should be less than cappacityTriggerBW
#define LOWcappacityTriggerRAMKEY                                                                  \
  "balancingminimumtriggerram" // capacity at which considerd almost full. should be less than cappacityTriggerRAM
#define balancingIntervalKEY "balancingInterval"
#define serverMonitorLimitKey "servermonitorlimit"
#define standbyKey "standby"
#define removeStandbyKey "removestandby"
#define lockKey "lock"

// const websocket api names set multiple times
#define addLoadbalancerKey "addloadbalancer"
#define removeLoadbalancerKey "removeloadbalancer"
#define resendKey "resend"
#define updateHostKey "updatehost"
#define weightsKey "weights"
#define addViewerKey "addviewer"
#define hostKey "host"
#define addServerKey "addserver"
#define removeServerKey "removeServer"
#define sendConfigKey "configExchange"

// config file names
#define configFallback "fallback"
#define configC "weight_cpu"
#define configR "weight_ram"
#define configBW "weight_bw"
#define configWG "weight_geo"
#define configWB "weight_bonus"
#define configPass "passHash"
#define configSPass "passphrase"
#define configPort "port"
#define configInterface "interface"
#define configWhitelist "whitelist"
#define configBearer "bearer_tokens"
#define configUsers "user_auth"
#define configServers "server_list"
#define configLoadbalancer "loadbalancers"

// balancing config file names
#define configMinStandby "minstandby"
#define configMaxStandby "maxstandby"
#define configCappacityTriggerCPUDec "triggerdecrementcpu" // percentage om cpu te verminderen
#define configCappacityTriggerBWDec                                                                \
  "triggerdecrementbandwidth"                              // percentage om bandwidth te verminderen
#define configCappacityTriggerRAMDEC "triggerdecrementram" // percentage om ram te verminderen
#define configCappacityTriggerCPU "triggercpu"             // max capacity trigger for balancing cpu
#define configCappacityTriggerBW "triggerbandwidth" // max capacity trigger for balancing bandwidth
#define configCappacityTriggerRAM "triggerram"      // max capacity trigger for balancing ram
#define configHighCappacityTriggerCPU                                                              \
  "balancingtriggercpu" // capacity at which considerd almost full. should be less than cappacityTriggerCPU
#define configHighCappacityTriggerBW                                                               \
  "balancingtriggerbandwidth" // capacity at which considerd almost full. should be less than cappacityTriggerBW
#define configHighCappacityTriggerRAM                                                              \
  "balancingtriggerram" // capacity at which considerd almost full. should be less than cappacityTriggerRAM
#define configLowCappacityTriggerCPU                                                               \
  "balancingminimumtriggercpu" // capacity at which considerd almost full. should be less than cappacityTriggerCPU
#define configLowCappacityTriggerBW                                                                \
  "balancingminimumtriggerbandwidth" // capacity at which considerd almost full. should be less than cappacityTriggerBW
#define configLowcappacityTriggerRAM                                                               \
  "balancingminimumtriggerram" // capacity at which considerd almost full. should be less than cappacityTriggerRAM
#define configBalancingInterval "balancingInterval"

// streamdetails names
#define streamDetailsTotal "total"
#define streamDetailsInputs "inputs"
#define streamDetailsBandwidth "bandwidth"
#define streamDetailsPrevTotal "prevTotal"
#define streamDetailsBytesUp "bytesUp"
#define streamDetailsBytesDown "bytesDown"

// outurl names
#define outUrlPre "pre"
#define outUrlPost "post"