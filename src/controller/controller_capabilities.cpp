#include <stdio.h>
#include <string.h>
#include <fstream>
#include <set>
#include <mist/config.h>
#include <mist/procs.h>
#include "controller_capabilities.h"

///\brief Holds everything unique to the controller.
namespace Controller {
  
  JSON::Value capabilities;
  //Converter::Converter * myConverter = 0;
  
  
  ///Aquire list of available protocols, storing in global 'capabilities' JSON::Value.
  void checkAvailProtocols(){
    std::deque<std::string> execs;
    Util::getMyExec(execs);
    std::string arg_one;
    char const * conn_args[] = {0, "-j", 0};
    for (std::deque<std::string>::iterator it = execs.begin(); it != execs.end(); it++){
      if ((*it).substr(0, 8) == "MistConn"){
        //skip if an MistOut already existed - MistOut takes precedence!
        if (capabilities["connectors"].isMember((*it).substr(8))){
          continue;
        }
        arg_one = Util::getMyPath() + (*it);
        conn_args[0] = arg_one.c_str();
        capabilities["connectors"][(*it).substr(8)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
        if (capabilities["connectors"][(*it).substr(8)].size() < 1){
          capabilities["connectors"].removeMember((*it).substr(8));
        }
      }
      if ((*it).substr(0, 7) == "MistOut"){
        arg_one = Util::getMyPath() + (*it);
        conn_args[0] = arg_one.c_str();
        capabilities["connectors"][(*it).substr(7)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
        if (capabilities["connectors"][(*it).substr(7)].size() < 1){
          capabilities["connectors"].removeMember((*it).substr(7));
        }
      }
      if ((*it).substr(0, 6) == "MistIn" && (*it) != "MistInfo"){
        arg_one = Util::getMyPath() + (*it);
        conn_args[0] = arg_one.c_str();
        capabilities["inputs"][(*it).substr(6)] = JSON::fromString(Util::Procs::getOutputOf((char**)conn_args));
        if (capabilities["inputs"][(*it).substr(6)].size() < 1){
          capabilities["inputs"].removeMember((*it).substr(6));
        }
      }
    }
  }
  
  ///\brief A class storing information about the cpu the server is running on.
  class cpudata{
    public:
      std::string model;///<A string describing the model of the cpu.
      int cores;///<The amount of cores in the cpu.
      int threads;///<The amount of threads this cpu can run.
      int mhz;///<The speed of the cpu in mhz.
      int id;///<The id of the cpu in the system.
      
      ///\brief The default constructor
      cpudata(){
        model = "Unknown";
        cores = 1;
        threads = 1;
        mhz = 0;
        id = 0;
      }
      ;
      
      ///\brief Fills the structure by parsing a given description.
      ///\param data A description of the cpu.
      void fill(char * data){
        int i;
        i = 0;
        if (sscanf(data, "model name : %n", &i) != EOF && i > 0){
          model = (data + i);
        }
        if (sscanf(data, "cpu cores : %d", &i) == 1){
          cores = i;
        }
        if (sscanf(data, "siblings : %d", &i) == 1){
          threads = i;
        }
        if (sscanf(data, "physical id : %d", &i) == 1){
          id = i;
        }
        if (sscanf(data, "cpu MHz : %d", &i) == 1){
          mhz = i;
        }
      }
      ;
  };

  ///\brief Checks the capabilities of the system.
  ///\param capa The location to store the capabilities.
  ///
  /// \api
  /// `"capabilities"` requests are always empty:
  /// ~~~~~~~~~~~~~~~{.js}
  /// {}
  /// ~~~~~~~~~~~~~~~
  /// and are responded to as:
  /// ~~~~~~~~~~~~~~~{.js}
  /// {
  ///   "connectors": { // a list of installed connectors
  ///     "FLV": { //name of the connector. This is based on the executable filename, with the "MistIn" / "MistConn" prefix stripped.
  ///       "codecs": [ //supported combinations of codecs.
  ///         [["H264","H263","VP6"],["AAC","MP3"]] //one such combination, listing simultaneously available channels and the codec options for those channels
  ///       ],
  ///       "deps": "HTTP", //dependencies on other connectors, if any.
  ///       "desc": "Enables HTTP protocol progressive streaming.", //human-friendly description of this connector
  ///       "methods": [ //list of supported request methods
  ///         {
  ///           "handler": "http", //what handler to use for this request method. The "http://" part of a URL, without the "://".
  ///           "priority": 5, // priority of this request method, higher is better.
  ///           "type": "flash/7" //type of request method - usually name of plugin followed by the minimal plugin version, or 'HTML5' for pluginless.
  ///         }
  ///       ],
  ///       "name": "HTTP_Progressive_FLV", //Full name of this connector.
  ///       "optional": { //optional parameters
  ///         "username": { //name of the parameter
  ///           "help": "Username to drop privileges to - default if unprovided means do not drop privileges", //human-readable help text
  ///           "name": "Username", //human-readable name of parameter
  ///           "option": "--username", //command-line option to use
  ///           "type": "str" //type of option - "str" or "num"
  ///         }
  ///         //above structure repeated for all (optional) parameters
  ///       },
  ///       //above structure repeated, as "required" for required parameters, if any.
  ///       "socket": "http_progressive_flv", //unix socket this connector listens on, if any
  ///       "url_match": "/$.flv", //URL pattern to match, if any. The $ substitutes the stream name and may not be the first or last character.
  ///       "url_prefix": "/progressive/$/", //URL prefix to match, if any. The $ substitutes the stream name and may not be the first or last character.
  ///       "url_rel": "/$.flv" //relative URL where to access a stream through this connector.
  ///     }
  ///     //... above structure repeated for all installed connectors.
  ///   },
  ///   "cpu": [ //a list of installed CPUs
  ///     {
  ///       "cores": 4, //amount of cores for this CPU
  ///       "mhz": 1645, //speed in MHz for this CPU
  ///       "model": "Intel(R) Core(TM) i7-2630QM CPU @ 2.00GHz", //model identifier, for humans
  ///       "threads": 8 //amount of simultaneously executing threads that are supported on this CPU
  ///     }
  ///     //above structure repeated for all installed CPUs
  ///   ],
  ///   "load": {
  ///     "fifteen": 72,
  ///     "five": 81,
  ///     "memory": 42,
  ///     "one": 124
  ///   },
  ///   "mem": {
  ///     "cached": 1989, //current memory usage of system caches, in MiB
  ///     "free": 2539, //free memory, in MiB
  ///     "swapfree": 0, //free swap space, in MiB
  ///     "swaptotal": 0, //total swap space, in MiB
  ///     "total": 7898, //total memory, in MiB
  ///     "used": 3370 //used memory, in MiB (excluding system caches, listed separately)
  ///   },
  ///   "speed": 6580, //total speed in MHz of all CPUs cores summed together
  ///   "threads": 8 //total count of all threads of all CPUs summed together
  ///   "cpu_use": 105 //Tenths of percent CPU usage - i.e. 105 = 10.5%
  /// }
  /// ~~~~~~~~~~~~~~~
  void checkCapable(JSON::Value & capa){
    //capa.null();
    capa.removeMember("cpu");
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo){
      std::map<int, cpudata> cpus;
      char line[300];
      int proccount = -1;
      while (cpuinfo.good()){
        cpuinfo.getline(line, 300);
        if (cpuinfo.fail()){
          //empty lines? ignore them, clear flags, continue
          if ( !cpuinfo.eof()){
            cpuinfo.ignore();
            cpuinfo.clear();
          }
          continue;
        }
        if (memcmp(line, "processor", 9) == 0){
          proccount++;
        }
        cpus[proccount].fill(line);
      }
      //fix wrong core counts
      std::map<int, int> corecounts;
      for (int i = 0; i <= proccount; ++i){
        corecounts[cpus[i].id]++;
      }
      //remove double physical IDs - we only want real CPUs.
      std::set<int> used_physids;
      int total_speed = 0;
      int total_threads = 0;
      for (int i = 0; i <= proccount; ++i){
        if ( !used_physids.count(cpus[i].id)){
          used_physids.insert(cpus[i].id);
          JSON::Value thiscpu;
          thiscpu["model"] = cpus[i].model;
          thiscpu["cores"] = cpus[i].cores;
          if (cpus[i].cores < 2 && corecounts[cpus[i].id] > cpus[i].cores){
            thiscpu["cores"] = corecounts[cpus[i].id];
          }
          thiscpu["threads"] = cpus[i].threads;
          if (thiscpu["cores"].asInt() > thiscpu["threads"].asInt()){
            thiscpu["threads"] = thiscpu["cores"];
          }
          thiscpu["mhz"] = cpus[i].mhz;
          capa["cpu"].append(thiscpu);
          total_speed += cpus[i].cores * cpus[i].mhz;
          total_threads += cpus[i].threads;
        }
      }
      capa["speed"] = total_speed;
      capa["threads"] = total_threads;
    }
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo){
      char line[300];
      int bufcache = 0;
      while (meminfo.good()){
        meminfo.getline(line, 300);
        if (meminfo.fail()){
          //empty lines? ignore them, clear flags, continue
          if ( !meminfo.eof()){
            meminfo.ignore();
            meminfo.clear();
          }
          continue;
        }
        long long int i;
        if (sscanf(line, "MemTotal : %lli kB", &i) == 1){
          capa["mem"]["total"] = i / 1024;
        }
        if (sscanf(line, "MemFree : %lli kB", &i) == 1){
          capa["mem"]["free"] = i / 1024;
        }
        if (sscanf(line, "SwapTotal : %lli kB", &i) == 1){
          capa["mem"]["swaptotal"] = i / 1024;
        }
        if (sscanf(line, "SwapFree : %lli kB", &i) == 1){
          capa["mem"]["swapfree"] = i / 1024;
        }
        if (sscanf(line, "Buffers : %lli kB", &i) == 1){
          bufcache += i / 1024;
        }
        if (sscanf(line, "Cached : %lli kB", &i) == 1){
          bufcache += i / 1024;
        }
      }
      capa["mem"]["used"] = capa["mem"]["total"].asInt() - capa["mem"]["free"].asInt() - bufcache;
      capa["mem"]["cached"] = bufcache;
      capa["load"]["memory"] = ((capa["mem"]["used"].asInt() + (capa["mem"]["swaptotal"].asInt() - capa["mem"]["swapfree"].asInt())) * 100)
          / capa["mem"]["total"].asInt();
    }
    std::ifstream loadavg("/proc/loadavg");
    if (loadavg){
      char line[300];
      loadavg.getline(line, 300);
      //parse lines here
      float onemin, fivemin, fifteenmin;
      if (sscanf(line, "%f %f %f", &onemin, &fivemin, &fifteenmin) == 3){
        capa["load"]["one"] = (long long int)(onemin * 100);
        capa["load"]["five"] = (long long int)(fivemin * 100);
        capa["load"]["fifteen"] = (long long int)(fifteenmin * 100);
      }
    }
    std::ifstream cpustat("/proc/stat");
    if (cpustat){
      char line[300];
      while (cpustat.getline(line, 300)){
        static unsigned long long cl_total = 0, cl_idle = 0;
        unsigned long long c_user, c_nice, c_syst, c_idle, c_total;
        if (sscanf(line, "cpu %Lu %Lu %Lu %Lu", &c_user, &c_nice, &c_syst, &c_idle) == 4){
          c_total = c_user + c_nice + c_syst + c_idle;
          if (c_total - cl_total > 0){
            capa["cpu_use"] = (long long int)(1000 - ((c_idle - cl_idle) * 1000) / (c_total - cl_total));
          }else{
            capa["cpu_use"] = 0ll;
          }
          cl_total = c_total;
          cl_idle = c_idle;
          break;
        }
      }
    }

  }

}

