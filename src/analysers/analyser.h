#pragma once
#include <mist/config.h>
#include <mist/defines.h>
#include <string>
#include <mist/urireader.h>

#define DETAIL_LOW(msg, ...)                                                                       \
  if (detail >= 1 && !validate){printf(msg "\n", ##__VA_ARGS__);}
#define DETAIL_MED(msg, ...)                                                                       \
  if (detail >= 2 && !validate){printf(msg "\n", ##__VA_ARGS__);}
#define DETAIL_HI(msg, ...)                                                                        \
  if (detail >= 3 && !validate){printf(msg "\n", ##__VA_ARGS__);}
#define DETAIL_VHI(msg, ...)                                                                       \
  if (detail >= 4 && !validate){printf(msg "\n", ##__VA_ARGS__);}
#define DETAIL_XTR(msg, ...)                                                                       \
  if (detail >= 5 && !validate){printf(msg "\n", ##__VA_ARGS__);}

class Analyser{
public:
  // These contain standard functionality
  Analyser(Util::Config &conf);
  ~Analyser();
  int run(Util::Config &conf);
  virtual void stop();
  virtual bool open(const std::string &filename);
  virtual bool isOpen();

  // These should be provided by analysers
  static void init(Util::Config &conf);
  virtual bool parsePacket(){return false;}
  void stopReason(const std::string & reason);

protected:
  // These hold the current state and/or config
  bool validate;      ///< True of validation mode is enabled
  uint64_t timeOut;   ///< After how many seconds a timeout should trigger
  int detail;         ///< Detail level of analyser
  uint64_t mediaTime; ///< Timestamp in ms of last media packet received
  uint64_t upTime;    ///< Unix time of analyser start
  uint64_t finTime;   ///< Unix time of last packet received
  uint64_t firstMediaTime;
  uint64_t firstMediaBootTime;
  bool *isActive;     ///< Pointer to is_active bool from config
  HTTP::URIReader uri;
  std::string reasonForStop;
private:
   std::map<uint64_t, uint64_t> mediaTimes;
};

