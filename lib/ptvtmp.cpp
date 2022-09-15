#include "ptvtmp.h"
#include <fcntl.h>

namespace Comms{

  PlayerUX::PlayerUX() : Comms(){
    sem.open(SEM_PLAYUX, O_CREAT | O_RDWR, ACCESSPERMS, 1);
  }

  PlayerUX::PlayerUX(const PlayerUX &rhs) : Comms(){
    sem.open(SEM_PLAYUX, O_CREAT | O_RDWR, ACCESSPERMS, 1);
    if (rhs){
      if (*this){
        setStream(rhs.getStream());
        setProto(rhs.getProto());
        setGeohash(rhs.getGeohash());
        setQuality(rhs.getQuality());
        setExperience(rhs.getExperience());
        setBadness(rhs.getBadness());
        setPercWatch(rhs.getPercWatch());
      }
    }
  }

  void PlayerUX::reload(bool _master, bool reIssue){
    Comms::reload(COMMS_PLAYUX, COMMS_PLAYUX_INITSIZE, _master, reIssue);
  }
  
  void PlayerUX::addFields(){
    Comms::addFields();
    dataAccX.addField("stream", RAX_128STRING);
    dataAccX.addField("proto", RAX_128STRING);
    dataAccX.addField("geohash", RAX_STRING, 8);
    dataAccX.addField("quality", RAX_16UINT);
    dataAccX.addField("experience", RAX_16UINT);
    dataAccX.addField("badness", RAX_16UINT);
    dataAccX.addField("percwatch", RAX_16UINT);
  }

  void PlayerUX::nullFields(){
    Comms::nullFields();
    setStream("");
    setProto("");
    setGeohash("");
    setQuality(0);
    setExperience(0);
    setBadness(0);
    setPercWatch(0);
  }

  void PlayerUX::fieldAccess(){
    Comms::fieldAccess();
    stream = dataAccX.getFieldAccX("stream");
    proto = dataAccX.getFieldAccX("proto");
    geohash = dataAccX.getFieldAccX("geohash");
    quality = dataAccX.getFieldAccX("quality");
    experience = dataAccX.getFieldAccX("experience");
    badness = dataAccX.getFieldAccX("badness");
    percwatch = dataAccX.getFieldAccX("percwatch");
  }

#define funcimpl(getname, setname, type, field, accfunc, defret) \
  const type PlayerUX::getname() const{return field.accfunc(index);} \
  const type PlayerUX::getname(size_t idx) const{return (master ? field.accfunc(idx) : defret);} \
  void PlayerUX::setname(const type & _val){field.set(_val, index);} \
  void PlayerUX::setname(const type & _val, size_t idx){ \
    if (!master){return;} \
    field.set(_val, idx); \
  }

  funcimpl(getStream, setStream, std::string, stream, string, "")
  funcimpl(getProto, setProto, std::string, proto, string, "")
  funcimpl(getGeohash, setGeohash, std::string, geohash, string, "")
  funcimpl(getQuality, setQuality, uint8_t, quality, uint, 0)
  funcimpl(getExperience, setExperience, uint8_t, experience, uint, 0)
  funcimpl(getBadness, setBadness, uint32_t, badness, uint, 0)
  funcimpl(getPercWatch, setPercWatch, uint8_t, percwatch, uint, 0)

}// namespace Comms
