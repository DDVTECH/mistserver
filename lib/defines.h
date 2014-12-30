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

#else

#define DEBUG_MSG(lvl, msg, ...) // Debugging disabled.

#endif

#define FAIL_MSG(msg, ...) DEBUG_MSG(DLVL_FAIL, msg, ##__VA_ARGS__)
#define ERROR_MSG(msg, ...) DEBUG_MSG(DLVL_ERROR, msg, ##__VA_ARGS__)
#define WARN_MSG(msg, ...) DEBUG_MSG(DLVL_WARN, msg, ##__VA_ARGS__)
#define DEVEL_MSG(msg, ...) DEBUG_MSG(DLVL_DEVEL, msg, ##__VA_ARGS__)
#define INFO_MSG(msg, ...) DEBUG_MSG(DLVL_DEVEL, msg, ##__VA_ARGS__)

#endif
