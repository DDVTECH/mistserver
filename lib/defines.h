#pragma once
// Defines to print debug messages.
#ifndef MIST_DEBUG
#define MIST_DEBUG 1
#define DLVL_NONE 0      // All debugging disabled.
#define DLVL_FAIL 1      // Only messages about failed operations.
#define DLVL_ERROR 2     // Only messages about errors and failed operations.
#define DLVL_WARN 3      // Warnings, errors, and fail messages.
#define DLVL_DEVEL 4     // All of the above, plus status messages handy during development.
#define DLVL_INFO 4      // All of the above, plus status messages handy during development.
#define DLVL_MEDIUM 5    // Slightly more than just development-level messages.
#define DLVL_HIGH 6      // Verbose debugging messages.
#define DLVL_VERYHIGH 7  // Very verbose debugging messages.
#define DLVL_EXTREME 8   // Everything is reported in extreme detail.
#define DLVL_INSANE 9    // Everything is reported in insane detail.
#define DLVL_DONTEVEN 10 // All messages enabled, even pointless ones.
#define PRETTY_PRINT_TIME "%ud%uh%um%us"
#define PRETTY_ARG_TIME(t)                                                                         \
  (int)(t) / 86400, ((int)(t) % 86400) / 3600, ((int)(t) % 3600) / 60, (int)(t) % 60
#define PRETTY_PRINT_MSTIME "%ud%.2uh%.2um%.2us.%.3u"
#define PRETTY_ARG_MSTIME(t) PRETTY_ARG_TIME(t / 1000), (int)(t % 1000)
#if DEBUG > -1

#define APPIDENT APPNAME "/" PACKAGE_VERSION
#define __STDC_FORMAT_MACROS 1
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <string>

//Declare as extern so we don't have to include the whole config.h header
namespace Util{
  extern uint32_t printDebugLevel;
  extern __thread char streamName[256];
}

static const char *DBG_LVL_LIST[] ={"NONE", "FAIL",     "ERROR",   "WARN",   "INFO",    "MEDIUM",
                                     "HIGH", "VERYHIGH", "EXTREME", "INSANE", "DONTEVEN"};

#if !defined(PRIu64)
#define PRIu64 "llu"
#endif

#if !defined(PRIu32)
#define PRIu32 "lu"
#endif

#if !defined(__APPLE__) && !defined(__MACH__) && defined(__GNUC__)
#include <errno.h>

#if DEBUG >= DLVL_DEVEL
#define DEBUG_MSG(lvl, msg, ...)                                                                     \
  if (Util::printDebugLevel >= lvl){\
    fprintf(stderr, "%s|%s|%d|%s:%d|%s|" msg "\n", DBG_LVL_LIST[lvl], program_invocation_short_name, \
            getpid(), __FILE__, __LINE__, Util::streamName, ##__VA_ARGS__);          \
  }
#else
#define DEBUG_MSG(lvl, msg, ...)                                                                   \
  if (Util::printDebugLevel >= lvl){\
    fprintf(stderr, "%s|%s|%d||%s|" msg "\n", DBG_LVL_LIST[lvl], program_invocation_short_name,    \
            getpid(), Util::streamName, ##__VA_ARGS__);                            \
  }
#endif
#else
#if DEBUG >= DLVL_DEVEL
#define DEBUG_MSG(lvl, msg, ...)                                                                   \
  if (Util::printDebugLevel >= lvl){\
    fprintf(stderr, "%s|%s|%d|%s:%d|%s|" msg "\n", DBG_LVL_LIST[lvl], getprogname(),  getpid(), __FILE__, \
            __LINE__, Util::streamName, ##__VA_ARGS__);                            \
  }
#else
#define DEBUG_MSG(lvl, msg, ...)                                                                   \
  if (Util::printDebugLevel >= lvl){\
    fprintf(stderr, "%s|MistProcess|%d||%s|" msg "\n", DBG_LVL_LIST[lvl], getpid(),                \
            Util::streamName, ##__VA_ARGS__);                                      \
  }
#endif
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
static inline void show_stackframe(){}
#else
#include <execinfo.h>
static inline void show_stackframe(){
  void *trace[16];
  char **messages = 0;
  int i, trace_size = 0;
  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  for (i = 1; i < trace_size; ++i){
    size_t p = 0;
    while (messages[i][p] != '(' && messages[i][p] != ' ' && messages[i][p] != 0){++p;}
    DEBUG_MSG(0, "Backtrace[%d]: %s", i, messages[i] + p);
  }
}
#endif

#else

#define DEBUG_MSG(lvl, msg, ...) // Debugging disabled.
static inline void show_stackframe(){}

#endif

#define BACKTRACE show_stackframe();
#define FAIL_MSG(msg, ...) DEBUG_MSG(DLVL_FAIL, msg, ##__VA_ARGS__)
#define ERROR_MSG(msg, ...) DEBUG_MSG(DLVL_ERROR, msg, ##__VA_ARGS__)
#define WARN_MSG(msg, ...) DEBUG_MSG(DLVL_WARN, msg, ##__VA_ARGS__)
#define DEVEL_MSG(msg, ...) DEBUG_MSG(DLVL_DEVEL, msg, ##__VA_ARGS__)
#define INFO_MSG(msg, ...) DEBUG_MSG(DLVL_DEVEL, msg, ##__VA_ARGS__)
#define MEDIUM_MSG(msg, ...) DEBUG_MSG(DLVL_MEDIUM, msg, ##__VA_ARGS__)
#define HIGH_MSG(msg, ...) DEBUG_MSG(DLVL_HIGH, msg, ##__VA_ARGS__)
#define VERYHIGH_MSG(msg, ...) DEBUG_MSG(DLVL_VERYHIGH, msg, ##__VA_ARGS__)
#define EXTREME_MSG(msg, ...) DEBUG_MSG(DLVL_EXTREME, msg, ##__VA_ARGS__)
#define INSANE_MSG(msg, ...) DEBUG_MSG(DLVL_INSANE, msg, ##__VA_ARGS__)
#define DONTEVEN_MSG(msg, ...) DEBUG_MSG(DLVL_DONTEVEN, msg, ##__VA_ARGS__)

#endif

#define DTSH_FRAGMENT_SIZE 13
#define DTSH_KEY_SIZE 25
#define DTSH_PART_SIZE 9

#ifndef SHM_DATASIZE
#define SHM_DATASIZE 40
#endif

#ifndef STATS_DELAY
#define STATS_DELAY 15
#endif
#define STATS_INPUT_DELAY 2

#ifndef INPUT_TIMEOUT
#define INPUT_TIMEOUT STATS_DELAY * 2
#endif

/// The size used for stream headers for live streams
#define DEFAULT_STRM_PAGE_SIZE 16 * 1024 * 1024

/// The size used for stream data pages under Windows, where they cannot be size-detected.
#define DEFAULT_DATA_PAGE_SIZE SHM_DATASIZE * 1024 * 1024

/// The size used for server configuration pages.
#define DEFAULT_CONF_PAGE_SIZE 4 * 1024 * 1024

/// The data size or duration from where on stream data pages are switched over to the next page.
/// The flip happens whenever either of these is matched.
#define FLIP_DATA_PAGE_SIZE 4 * 1024 * 1024
#define FLIP_TARGET_DURATION 30000
/// The minimum duration for switching to next page. The flip will never happen before this.
/// Does not affect live streams.
#define FLIP_MIN_DURATION 10000

// New meta
#define SHM_STREAM_META "MstMeta%s" //%s stream name
#define SHM_STREAM_META_LEN 8 * 1024 * 1024
#define SHM_STREAM_META_ITEM 2 * 1024

#define SHM_STREAM_TM "MstTrak%s@%" PRIu32 "-%zu" //%s stream name
#define SHM_STREAM_TRACK_ITEM 16 * 1024 * 1024
#define SHM_STREAM_TRACK_LEN 4 * SHM_STREAM_TRACK_ITEM

// Default values, these will scale up and down when needed, and are mainly used as starting values.
#define DEFAULT_TRACK_COUNT 100
#define DEFAULT_FRAGMENT_COUNT 60
#define DEFAULT_KEY_COUNT 60
#define DEFAULT_PART_COUNT 30 * DEFAULT_KEY_COUNT
#define DEFAULT_PAGE_COUNT 10

#define DEFAULT_FRAGMENT_DURATION 1900

// Pages get marked for deletion after X seconds of no one watching
#define DEFAULT_PAGE_TIMEOUT 15

/// \TODO These values are hardcoded for now, but the dtsc_sizing_test binary can calculate them accurately.
#define META_META_OFFSET 138
#define META_META_RECORDSIZE 548

#define META_TRACK_OFFSET 148
#define META_TRACK_RECORDSIZE 1893

#define TRACK_TRACK_OFFSET 193
#define TRACK_TRACK_RECORDSIZE 1049060

#define TRACK_FRAGMENT_OFFSET 68
#define TRACK_FRAGMENT_RECORDSIZE 14

#define TRACK_KEY_OFFSET 90
#define TRACK_KEY_RECORDSIZE 40

#define TRACK_PART_OFFSET 60
#define TRACK_PART_RECORDSIZE 8

#define TRACK_PAGE_OFFSET 100
#define TRACK_PAGE_RECORDSIZE 36

#define COMMS_STATISTICS "MstStat"
#define COMMS_STATISTICS_INITSIZE 16 * 1024 * 1024

#define COMMS_USERS "MstUser%s" //%s stream name
#define COMMS_USERS_INITSIZE 512 * 1024

#define COMMS_SESSIONS "MstSession%s"
#define COMMS_SESSIONS_INITSIZE 8 * 1024 * 1024

#define SEM_STATISTICS "/MstStat"
#define SEM_USERS "/MstUser%s" //%s stream name

#define SHM_TRACK_DATA "MstData%s@%zu_%" PRIu32 //%s stream name, %zu track ID, %PRIu32 page #
// End new meta

#define INPUT_USER_INTERVAL 1000

#define SHM_STREAM_STATE "MstSTATE%s" //%s stream name
#define SHM_STREAM_CONF "MstSCnf%s"   //%s stream name
#define SHM_STREAM_IPID "MstIPID%s"   //%s stream name
#define SHM_STREAM_PPID "MstPPID%s"   //%s stream name
#define SHM_GLOBAL_CONF "MstGlobalConfig"
#define STRMSTAT_OFF 0
#define STRMSTAT_INIT 1
#define STRMSTAT_BOOT 2
#define STRMSTAT_WAIT 3
#define STRMSTAT_READY 4
#define STRMSTAT_SHUTDOWN 5
#define STRMSTAT_INVALID 255

#define SHM_TRIGGER "MstTRGR%s" //%s trigger name
#define SEM_LIVE "/MstLIVE%s"   //%s stream name
#define SEM_INPUT "/MstInpt%s"  //%s stream name
#define SEM_TRACKLIST "/MstTRKS%s"  //%s stream name
#define SEM_SESSION "/MstSess%s"
#define SEM_SESSCACHE "/MstSessCacheLock"
#define SESS_TIMEOUT 600 // Session timeout in seconds
#define SHM_CAPA "MstCapa"
#define SHM_PROTO "MstProt"
#define SHM_PROXY "MstProx"
#define SHM_STATE_LOGS "MstStateLogs"
#define SHM_STATE_ACCS "MstStateAccs"
#define SHM_STATE_STREAMS "MstStateStreams"
#define NAME_BUFFER_SIZE 200 // char buffer size for snprintf'ing shm filenames
#define SHM_SESSIONS "/MstSess"
#define SHM_SESSIONS_ITEM 165     // 4 byte crc, 100b streamname, 20b connector, 40b host, 1b sync
#define SHM_SESSIONS_SIZE 5248000 // 5MiB = almost 32k sessions

#if defined(__APPLE__)
#define IPC_MAX_LEN 30 // macos allows a maximum of 31, including terminating null
#else
#define IPC_MAX_LEN 250 // most other implementation a maximum of 251, including terminating null
#endif

#define SHM_STREAM_ENCRYPT "MstCRYP%s" //%s stream name

#define SIMUL_TRACKS 40

#ifndef UDP_API_HOST
#define UDP_API_HOST "localhost"
#endif

#ifndef UDP_API_PORT
#define UDP_API_PORT 4242
#endif


// The amount of milliseconds a simulated live stream is allowed to be "behind".
// Setting this value to lower than 2 seconds **WILL** cause stuttering in playback due to buffer negotiation.
#define SIMULATED_LIVE_BUFFER 7000

/// The time between virtual audio "keyframes"
#define AUDIO_KEY_INTERVAL 2047

#define STAT_EX_SIZE 177
#define PLAY_EX_SIZE 2 + 6 * SIMUL_TRACKS

#define INVALID_TRACK_ID     0xFFFFFFFFu
#define INVALID_KEY_NUM      0xFFFFFFFFu
#define INVALID_RECORD_INDEX 0xFFFFFFFFFFFFFFFFull

#define NEW_TRACK_ID 0x80000000
#define QUICK_NEGOTIATE 0xC0000000

#define ER_UNKNOWN "UNKNOWN"
#define ER_CLEAN_LIVE_BUFFER_REQ "CLEAN_LIVE_BUFFER_REQ"
#define ER_CLEAN_CONTROLLER_REQ "CLEAN_CONTROLLER_REQ"
#define ER_CLEAN_INTENDED_STOP "CLEAN_INTENDED_STOP"
#define ER_CLEAN_REMOTE_CLOSE "CLEAN_REMOTE_CLOSE"
#define ER_CLEAN_INACTIVE "CLEAN_INACTIVE"
#define ER_CLEAN_SIGNAL "CLEAN_SIGNAL"
#define ER_CLEAN_EOF "CLEAN_EOF"
#define ER_READ_START_FAILURE "READ_START_FAILURE"
#define ER_PROCESS_SPECIFIC "PROCESS_SPECIFIC"
#define ER_FORMAT_SPECIFIC "FORMAT_SPECIFIC"
#define ER_INTERNAL_ERROR "INTERNAL_ERROR"
#define ER_WRITE_FAILURE "WRITE_FAILURE"
#define ER_EXEC_FAILURE "EXEC_FAILURE"
#define ER_SHM_LOST "SHM_LOST"
#define ER_TRIGGER "TRIGGER"