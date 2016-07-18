#pragma once
#include <mist/config.h>
#include <string>

class analysers
{
  protected:
    bool fileinput;
    std::string filename;
    bool analyse;
    bool validate;
    bool mayExecute = true;
    int detail;
     
    long long int endTime;
    long long int upTime;
    long long int finTime;
    
    bool useBuffer;
    
  public:
    Util::Config conf;
    analysers(Util::Config &config);
    ~analysers();
    
    void Prepare();
    int Run();
    virtual int doAnalyse();
    virtual void doValidate();
    virtual bool hasInput();

    static void defaultConfig(Util::Config & conf);
    //int Analyse();
    
    virtual bool packetReady();
};
