// Defines to print debug messages.
#define DLVL_NONE      0 // All debugging disabled.
#define DLVL_FAIL      1 // Only messages about failed operations.
#define DLVL_ERROR     2 // Only messages about errors and failed operations.
#define DLVL_WARN      3 // Warnings, errors, and fail messages.
#define DLVL_DEVEL     4 // All of the above, plus status messages handy during development.
#define DLVL_MEDIUM    5 // Slightly more than just development-level messages.
#define DLVL_HIGH      6 // Verbose debugging messages.
#define DLVL_VERYHIGH  7 // Very verbose debugging messages.
#define DLVL_EXTREME   8 // Everything is reported in extreme detail.
#define DLVL_INSANE    9 // Everything is reported in insane detail.
#define DLVL_DONTEVEN 10 // All messages enabled, even pointless ones.
#if DEBUG > 0
#include <stdio.h>
#define DEBUG_MSG(lvl, msg, ...) if (DEBUG >= lvl){fprintf(stderr, "[%s:%d] " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);}
#else
#define DEBUG_MSG(lvl, msg, ...) // Debugging disabled.
#endif
