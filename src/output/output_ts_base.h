#pragma once
#include "output.h"
#include "output_http.h"
#include <mist/defines.h>
#include <mist/mp4_generic.h>
#include <mist/ts_packet.h>

#ifndef TS_BASECLASS
#define TS_BASECLASS Output
#endif

namespace Mist{

  template<class T>
  class TSOutputTmpl : public T{
  public:
    TSOutputTmpl(Socket::Connection &conn);
    virtual ~TSOutputTmpl(){};
    virtual void sendNext();
    virtual void sendTS(const char *tsData, size_t len = 188){};
    void fillPacket(char const *data, size_t dataLen, bool &firstPack, bool video, bool keyframe,
                    size_t pkgPid, uint16_t &contPkg);
    virtual void sendHeader(){
      this->sentHeader = true;
      this->packCounter = 0;
    }

  protected:
    virtual bool inlineRestartCapable() const{return true;}
    std::map<size_t, bool> first;
    std::map<size_t, uint16_t> contCounters;
    uint16_t contPAT;
    uint16_t contPMT;
    uint16_t contSDT;
    size_t packCounter; ///\todo update constructors?
    TS::Packet packData;
    uint64_t sendRepeatingHeaders; ///< Amount of ms between PAT/PMT. Zero means do not repeat.
    uint64_t lastHeaderTime;       ///< Timestamp last PAT/PMT were sent.
    uint64_t ts_from;              ///< Starting time to subtract from timestamps
  };

  class TSOutput : public TSOutputTmpl<Output>{
  public:
    TSOutput(Socket::Connection &conn);
  };
  class TSOutputHTTP : public TSOutputTmpl<HTTPOutput>{
  public:
    TSOutputHTTP(Socket::Connection &conn);
  };
}// namespace Mist
