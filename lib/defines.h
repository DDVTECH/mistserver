#pragma once
// Defines to print debug messages.
#ifndef MIST_DEBUG
#define MIST_DEBUG 1
#define DLVL_NONE      0 // All debugging disabled.
#define DLVL_FAIL      1 // Only messages about failed operations.
#define DLVL_ERROR     2 // Only messages about errors and failed operations.
#define DLVL_WARN      3 // Warnings, errors, and fail messages.
#define DLVL_DEVEL     4 // All of the above, plus status messages handy during development.
#define DLVL_INFO      4 // All of the above, plus status messages handy during development.
#define DLVL_MEDIUM    5 // Slightly more than just development-level messages.
#define DLVL_HIGH      6 // Verbose debugging messages.
#define DLVL_VERYHIGH  7 // Very verbose debugging messages.
#define DLVL_EXTREME   8 // Everything is reported in extreme detail.
#define DLVL_INSANE    9 // Everything is reported in insane detail.
#define DLVL_DONTEVEN 10 // All messages enabled, even pointless ones.
#if DEBUG > -1

#include <stdio.h>
#include <unistd.h>
#include "config.h"
static const char * DBG_LVL_LIST[] = {"NONE", "FAIL", "ERROR", "WARN", "INFO", "MEDIUM", "HIGH", "VERYHIGH", "EXTREME", "INSANE", "DONTEVEN"};

#if !defined(PRIu64)
#define PRIu64 "llu"
#endif

#if !defined(__APPLE__) && !defined(__MACH__) && defined(__GNUC__)
#include <errno.h>

#if DEBUG >= DLVL_DEVEL
#define DEBUG_MSG(lvl, msg, ...) if (Util::Config::printDebugLevel >= lvl){fprintf(stderr, "%s|%s|%d|%s:%d|" msg "\n", DBG_LVL_LIST[lvl], program_invocation_short_name, getpid(), __FILE__, __LINE__, ##__VA_ARGS__);}
#else
#define DEBUG_MSG(lvl, msg, ...) if (Util::Config::printDebugLevel >= lvl){fprintf(stderr, "%s|%s|%d||" msg "\n", DBG_LVL_LIST[lvl], program_invocation_short_name, getpid(), ##__VA_ARGS__);}
#endif
#else
#if DEBUG >= DLVL_DEVEL
#define DEBUG_MSG(lvl, msg, ...) if (Util::Config::printDebugLevel >= lvl){fprintf(stderr, "%s||%d|%s:%d|" msg "\n", DBG_LVL_LIST[lvl], getpid(), __FILE__, __LINE__, ##__VA_ARGS__);}
#else
#define DEBUG_MSG(lvl, msg, ...) if (Util::Config::printDebugLevel >= lvl){fprintf(stderr, "%s||%d||" msg "\n", DBG_LVL_LIST[lvl], getpid(), ##__VA_ARGS__);}
#endif
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
static inline void show_stackframe(){}
#else
#include <execinfo.h>
static inline void show_stackframe() {
  void *trace[16];
  char **messages = 0;
  int i, trace_size = 0;
  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  for (i=1; i<trace_size; ++i){
    size_t p = 0;
    while(messages[i][p] != '(' && messages[i][p] != ' ' && messages[i][p] != 0){
      ++p;
    }
    DEBUG_MSG(0, "Backtrace[%d]: %s", i, messages[i]+p);
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


#ifndef SHM_DATASIZE
#define SHM_DATASIZE 20
#endif


#ifndef STATS_DELAY
#define STATS_DELAY 15
#endif
#define STATS_INPUT_DELAY 2

#ifndef INPUT_TIMEOUT
#define INPUT_TIMEOUT STATS_DELAY
#endif

/// The size used for stream headers for live streams
#define DEFAULT_STRM_PAGE_SIZE 16 * 1024 * 1024

/// The size used for stream data pages under Windows, where they cannot be size-detected.
#define DEFAULT_DATA_PAGE_SIZE SHM_DATASIZE * 1024 * 1024

/// The size used for server configuration pages.
#define DEFAULT_CONF_PAGE_SIZE 4 * 1024 * 1024

/// The data size or duration from where on stream data pages are switched over to the next page.
/// The flip happens whenever either of these is matched.
#define FLIP_DATA_PAGE_SIZE 8 * 1024 * 1024
#define FLIP_TARGET_DURATION 60000
/// The minimum duration for switching to next page. The flip will never happen before this.
/// Does not affect live streams.
#define FLIP_MIN_DURATION 20000

#define SHM_STREAM_INDEX "MstSTRM%s" //%s stream name
#define SHM_STREAM_STATE "MstSTATE%s" //%s stream name
#define STRMSTAT_OFF 0
#define STRMSTAT_INIT 1
#define STRMSTAT_BOOT 2
#define STRMSTAT_WAIT 3
#define STRMSTAT_READY 4
#define STRMSTAT_SHUTDOWN 5
#define STRMSTAT_INVALID 255
#define SHM_TRACK_META "MstTRAK%s@%lu" //%s stream name, %lu track ID
#define SHM_TRACK_INDEX "MstTRID%s@%lu" //%s stream name, %lu track ID
#define SHM_TRACK_INDEX_SIZE 8192
#define SHM_TRACK_DATA "MstDATA%s@%lu_%lu" //%s stream name, %lu track ID, %lu page #
#define SHM_STATISTICS "MstSTAT"
#define SHM_USERS "MstUSER%s" //%s stream name
#define SHM_TRIGGER "MstTRGR%s" //%s trigger name
#define SEM_LIVE "/MstLIVE%s" //%s stream name
#define SEM_INPUT "/MstInpt%s" //%s stream name
#define SEM_CONF "/MstConfLock"
#define SEM_SESSCACHE "/MstSessCacheLock"
#define SHM_CONF "MstConf"
#define NAME_BUFFER_SIZE 200    //char buffer size for snprintf'ing shm filenames
#define SHM_SESSIONS "/MstSess"
#define SHM_SESSIONS_ITEM 165 //4 byte crc, 100b streamname, 20b connector, 40b host, 1b sync
#define SHM_SESSIONS_SIZE 5248000 //5MiB = almost 32k sessions

#define SHM_STREAM_ENCRYPT "MstCRYP%s" //%s stream name

#define SIMUL_TRACKS 20

