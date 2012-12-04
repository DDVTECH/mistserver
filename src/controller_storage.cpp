#include <iostream>
#include <mist/timing.h>
#include "controller_storage.h"

namespace Controller{
  
  JSON::Value Storage; ///< Global storage of data.

  /// Store and print a log message.
  void Log(std::string kind, std::string message){
    //if last log message equals this one, do not log.
    if (Storage["log"].size() > 0){
      JSON::ArrIter it = Storage["log"].ArrEnd() - 1;
      if ((*it)[2] == message){return;}
    }
    JSON::Value m;
    m.append(Util::epoch());
    m.append(kind);
    m.append(message);
    Storage["log"].append(m);
    Storage["log"].shrink(100);//limit to 100 log messages
    std::cout << "[" << kind << "] " << message << std::endl;
  }

}
