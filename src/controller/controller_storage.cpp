#include <iostream>
#include <fstream>
#include <mist/timing.h>
#include "controller_storage.h"

///\brief Holds everything unique to the controller.
namespace Controller {

  JSON::Value Storage; ///< Global storage of data.

  ///\brief Store and print a log message.
  ///\param kind The type of message.
  ///\param message The message to be logged.
  void Log(std::string kind, std::string message){
    //if last log message equals this one, do not log.
    if (Storage["log"].size() > 0){
      JSON::ArrIter it = Storage["log"].ArrEnd();
      int repeats = Storage["log"].size();
      if (repeats > 10){repeats = 10;}
      do{
        it--;
        if (( *it)[2] == message && ( *it)[1] == kind){
          return;
        }
        repeats--;
      }while (repeats > 0);
    }
    JSON::Value m;
    m.append(Util::epoch());
    m.append(kind);
    m.append(message);
    Storage["log"].append(m);
    Storage["log"].shrink(100); //limit to 100 log messages
    time_t rawtime;
    struct tm * timeinfo;
    char buffer [100];
    time (&rawtime);
    timeinfo = localtime (&rawtime);
    strftime (buffer,100,"%b %d %Y -- %H:%M",timeinfo);
    std::cout << "(" << buffer << ") [" << kind << "] " << message << std::endl;
  }

  ///\brief Write contents to Filename
  ///\param Filename The full path of the file to write to.
  ///\param contents The data to be written to the file.
  bool WriteFile(std::string Filename, std::string contents){
    std::ofstream File;
    File.open(Filename.c_str());
    File << contents << std::endl;
    File.close();
    return File.good();
  }

}
