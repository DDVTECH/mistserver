#include <stdio.h>
#include <string.h>
#include <fstream>
#include <set>
#include "controller_capabilities.h"

///\brief Holds everything unique to the controller.
namespace Controller {
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
  void checkCapable(JSON::Value & capa){
    capa.null();
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
        if (sscanf(line, "MemTotal : %Li kB", &i) == 1){
          capa["mem"]["total"] = i / 1024;
        }
        if (sscanf(line, "MemFree : %Li kB", &i) == 1){
          capa["mem"]["free"] = i / 1024;
        }
        if (sscanf(line, "SwapTotal : %Li kB", &i) == 1){
          capa["mem"]["swaptotal"] = i / 1024;
        }
        if (sscanf(line, "SwapFree : %Li kB", &i) == 1){
          capa["mem"]["swapfree"] = i / 1024;
        }
        if (sscanf(line, "Buffers : %Li kB", &i) == 1){
          bufcache += i / 1024;
        }
        if (sscanf(line, "Cached : %Li kB", &i) == 1){
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
      int bufcache = 0;
      loadavg.getline(line, 300);
      //parse lines here
      float onemin, fivemin, fifteenmin;
      if (sscanf(line, "%f %f %f", &onemin, &fivemin, &fifteenmin) == 3){
        capa["load"]["one"] = (long long int)(onemin * 100);
        capa["load"]["five"] = (long long int)(onemin * 100);
        capa["load"]["fifteen"] = (long long int)(onemin * 100);
      }
    }

    //list available protocols and report about them
    capa["connectors"]["RTMP"]["desc"] = "Enables the RTMP protocol which is used by Adobe Flash Player.";
    capa["connectors"]["RTMP"]["deps"] = "";
    capa["connectors"]["RTMP"]["optional"]["port"]["name"] = "TCP port";
    capa["connectors"]["RTMP"]["optional"]["port"]["help"] = "TCP port to listen on - default if unprovided is 1935";
    capa["connectors"]["RTMP"]["optional"]["port"]["type"] = "uint";
    capa["connectors"]["RTMP"]["optional"]["interface"]["name"] = "Interface";
    capa["connectors"]["RTMP"]["optional"]["interface"]["help"] = "Address of the interface to listen on - default if unprovided is all interfaces";
    capa["connectors"]["RTMP"]["optional"]["interface"]["type"] = "str";
    capa["connectors"]["RTMP"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["RTMP"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["RTMP"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["TS"]["desc"] = "Enables the raw MPEG Transport Stream protocol over TCP.";
    capa["connectors"]["TS"]["deps"] = "";
    capa["connectors"]["TS"]["required"]["port"]["name"] = "TCP port";
    capa["connectors"]["TS"]["required"]["port"]["help"] = "TCP port to listen on";
    capa["connectors"]["TS"]["required"]["port"]["type"] = "uint";
    capa["connectors"]["TS"]["required"]["args"]["name"] = "Stream";
    capa["connectors"]["TS"]["required"]["args"]["help"] = "What streamname to serve - for multiple streams, add this protocol multiple times.";
    capa["connectors"]["TS"]["required"]["args"]["type"] = "str";
    capa["connectors"]["TS"]["optional"]["interface"]["name"] = "Interface";
    capa["connectors"]["TS"]["optional"]["interface"]["help"] = "Address of the interface to listen on - default if unprovided is all interfaces";
    capa["connectors"]["TS"]["optional"]["interface"]["type"] = "str";
    capa["connectors"]["TS"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["TS"]["optional"]["username"]["help"] = "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["TS"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["HTTP"]["desc"] =
        "Enables the generic HTTP listener, required by all other HTTP protocols. Needs other HTTP protocols enabled to do much of anything.";
    capa["connectors"]["HTTP"]["deps"] = "";
    capa["connectors"]["HTTP"]["optional"]["port"]["name"] = "TCP port";
    capa["connectors"]["HTTP"]["optional"]["port"]["help"] = "TCP port to listen on - default if unprovided is 8080";
    capa["connectors"]["HTTP"]["optional"]["port"]["type"] = "uint";
    capa["connectors"]["HTTP"]["optional"]["interface"]["name"] = "Interface";
    capa["connectors"]["HTTP"]["optional"]["interface"]["help"] = "Address of the interface to listen on - default if unprovided is all interfaces";
    capa["connectors"]["HTTP"]["optional"]["interface"]["type"] = "str";
    capa["connectors"]["HTTP"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["HTTP"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["HTTP"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["HTTPProgressive"]["desc"] = "Enables HTTP protocol progressive streaming.";
    capa["connectors"]["HTTPProgressive"]["deps"] = "HTTP";
    capa["connectors"]["HTTPProgressive"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["HTTPProgressive"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["HTTPProgressive"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["HTTPDynamic"]["desc"] = "Enables HTTP protocol Adobe-specific dynamic streaming (aka HDS).";
    capa["connectors"]["HTTPDynamic"]["deps"] = "HTTP";
    capa["connectors"]["HTTPDynamic"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["HTTPDynamic"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["HTTPDynamic"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["HTTPSmooth"]["desc"] = "Enables HTTP protocol MicroSoft-specific smooth streaming through silverlight.";
    capa["connectors"]["HTTPSmooth"]["deps"] = "HTTP";
    capa["connectors"]["HTTPSmooth"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["HTTPSmooth"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["HTTPSmooth"]["optional"]["username"]["type"] = "str";
    capa["connectors"]["HTTPLive"]["desc"] = "Enables HTTP protocol Apple-style live streaming.";
    capa["connectors"]["HTTPLive"]["deps"] = "HTTP";
    capa["connectors"]["HTTPLive"]["optional"]["username"]["name"] = "Username";
    capa["connectors"]["HTTPLive"]["optional"]["username"]["help"] =
        "Username to drop privileges to - default if unprovided means do not drop privileges";
    capa["connectors"]["HTTPLive"]["optional"]["username"]["type"] = "str";
  }

}

