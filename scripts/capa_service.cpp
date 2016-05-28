#include <stdio.h>
#include <string.h>
#include <fstream>
#include <set>
#include <iostream>
#include <mist/json.h>
#include <mist/timing.h>

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
  std::ifstream cpuUsage("/proc/stat");
  if (cpuUsage){
    char line[300];
    cpuUsage.getline(line, 300);
    long long int i, o, p, q;
    if (sscanf(line, "cpu  %lli %lli %lli %lli ", &i, &o, &p, &q) == 4){
      capa["usage"]["user"] = i;
      capa["usage"]["nice"] = o;
      capa["usage"]["system"] = p;
      capa["usage"]["idle"] = q;
    }else{
      std::cerr << "HALP!" << std::endl;
    }
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
        capa["mem"]["total"] = i;
      }
      if (sscanf(line, "MemFree : %lli kB", &i) == 1){
        capa["mem"]["free"] = i;
      }
      if (sscanf(line, "SwapTotal : %lli kB", &i) == 1){
        capa["mem"]["swaptotal"] = i;
      }
      if (sscanf(line, "SwapFree : %lli kB", &i) == 1){
        capa["mem"]["swapfree"] = i;
      }
      if (sscanf(line, "Buffers : %lli kB", &i) == 1){
        bufcache += i;
      }
      if (sscanf(line, "Cached : %lli kB", &i) == 1){
        bufcache += i;
      }
    }
    capa["mem"]["used"] = capa["mem"]["total"].asInt() - capa["mem"]["free"].asInt() - bufcache;
    capa["mem"]["cached"] = bufcache;
    capa["load"]["memory"] = ((capa["mem"]["used"].asInt() + (capa["mem"]["swaptotal"].asInt() - capa["mem"]["swapfree"].asInt())) * 100) / capa["mem"]["total"].asInt();
  }
  std::ifstream loadavg("/proc/loadavg");
  if (loadavg){
    char line[300];
    loadavg.getline(line, 300);
    //parse lines here
    float onemin;
    if (sscanf(line, "%f %*f %*f", &onemin) == 1){
      capa["load"]["one"] = (long long int)(onemin*1000);
    }
  }
  std::ifstream netUsage("/proc/net/dev");
  capa["net"]["sent"] = 0;
  capa["net"]["recv"] = 0;
  while (netUsage){
    char line[300];
    netUsage.getline(line, 300);
    long long unsigned sent = 0;
    long long unsigned recv = 0;
    //std::cout << line;
    if (sscanf(line, "%*s %llu %*u %*u %*u %*u %*u %*u %*u %llu", &recv, &sent) == 2){
      //std::cout << "Net: " << recv << ", " << sent << std::endl;
      capa["net"]["recv"] = (long long int)(capa["net"]["recv"].asInt() + recv);
      capa["net"]["sent"] = (long long int)(capa["net"]["sent"].asInt() + sent);
    }
  }
}

int main(int argc, char** argv){
  JSON::Value stats;
  checkCapable(stats);
  std::ofstream file(argv[1]);
  file  << "Time in seconds,1m load average,Memory use in bytes,CPU percentage,Uploaded bytes,Downloaded bytes" << std::endl;
  long long int totalCpu = 0;
  long long int grandTotal = 0;
  long long int usrCpu = 0;
  long long int niceCpu = 0; 
  long long int systemCpu = 0; 
  long long int prevUsrCpu = stats["usage"]["user"].asInt();
  long long int prevNiceCpu = stats["usage"]["nice"].asInt(); 
  long long int prevSystemCpu = stats["usage"]["system"].asInt(); 
  long long int prevIdleCpu = stats["usage"]["idle"].asInt();
  long long int startUpload = stats["net"]["sent"].asInt();
  long long int startDownload = stats["net"]["recv"].asInt();
  long long int startTime = Util::epoch();
  long long int lastTime = 0;
  while (true){
    Util::sleep(500);//faster than once per second, just in case we go out of sync somewhere
    if (lastTime == Util::epoch()){
      continue;//if a second hasn't passed yet, skip this run
    }
    lastTime = Util::epoch();
    checkCapable(stats);
    file << (lastTime - startTime) << ",";//time since start
    file  << (double)stats["load"]["one"].asInt()/1000.0 << ","; //LoadAvg
    file  << stats["mem"]["used"].asString() << ","; //MemUse
  
    usrCpu = stats["usage"]["user"].asInt() - prevUsrCpu;
    niceCpu = stats["usage"]["nice"].asInt() - prevNiceCpu;
    systemCpu = stats["usage"]["system"].asInt() - prevSystemCpu;
    totalCpu = usrCpu + niceCpu + systemCpu;
    grandTotal = totalCpu + stats["usage"]["idle"].asInt() - prevIdleCpu;
    if (grandTotal != 0){
      file  << 100 * (double)totalCpu / grandTotal << ",";//totalCpu
    }else{
      file  << "," << std::endl;//unknown CPU usage
    }
    file << (stats["net"]["sent"].asInt() - startUpload) << "," << (stats["net"]["recv"].asInt() - startDownload) << std::endl;
    prevUsrCpu = stats["usage"]["user"].asInt();
    prevNiceCpu = stats["usage"]["nice"].asInt(); 
    prevSystemCpu = stats["usage"]["system"].asInt(); 
    prevIdleCpu = stats["usage"]["idle"].asInt();
  }
  return 0;
}
