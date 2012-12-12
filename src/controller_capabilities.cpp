#include <stdio.h>
#include <string.h>
#include <fstream>
#include <set>
#include "controller_capabilities.h"

namespace Controller {

  class cpudata{
    public:
      std::string model;
      int cores;
      int threads;
      int mhz;
      int id;
      cpudata(){
        model = "Unknown";
        cores = 1;
        threads = 1;
        mhz = 0;
        id = 0;
      }
      ;
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
  }

}

