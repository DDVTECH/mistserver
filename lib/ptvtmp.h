#pragma once
#include "comms.h"

#define COMMS_PLAYUX "MstPlayerUX"
#define COMMS_PLAYUX_INITSIZE 4*1024*1024
#define SEM_PLAYUX "/MstPlayerUX"

#define funcdefs(getname, setname, type) \
    const type getname() const; \
    const type getname(size_t idx) const; \
    void setname(const type & _val); \
    void setname(const type & _val, size_t idx);

namespace Comms{

  class PlayerUX : public Comms{
  public:
    PlayerUX();
    PlayerUX(const PlayerUX &rhs);
    void reload(bool _master = false, bool reIssue = false);
    virtual void addFields();
    virtual void nullFields();
    virtual void fieldAccess();

    funcdefs(getStream, setStream, std::string)
    funcdefs(getProto, setProto, std::string)
    funcdefs(getGeohash, setGeohash, std::string)
    funcdefs(getQuality, setQuality, uint8_t)
    funcdefs(getExperience, setExperience, uint8_t)
    funcdefs(getBadness, setBadness, uint32_t)
    funcdefs(getPercWatch, setPercWatch, uint8_t)

  private:
    Util::FieldAccX stream;
    Util::FieldAccX proto;
    Util::FieldAccX geohash;
    Util::FieldAccX quality;
    Util::FieldAccX experience;
    Util::FieldAccX badness;
    Util::FieldAccX percwatch;
  };
}// namespace Comms
