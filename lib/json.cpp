/// \file json.cpp Holds all JSON-related code.

#include "json.h"
#include "defines.h"
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <stdint.h> //for uint64_t
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl

/// Construct from a root Value to iterate over.
JSON::Iter::Iter(Value & root){
  myType = root.myType;
  i = 0;
  r = &root;
  if (!root.size()){myType = JSON::EMPTY;}
  if (myType == JSON::ARRAY){
    aIt = root.arrVal.begin();
  }
  if (myType == JSON::OBJECT){
    oIt = root.objVal.begin();
  }
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
JSON::Value & JSON::Iter::operator*() const{
  if (myType == JSON::ARRAY && aIt != r->arrVal.end()){
    return **aIt;
  }
  if (myType == JSON::OBJECT && oIt != r->objVal.end()){
    return *(oIt->second);
  }
  static JSON::Value error;
  WARN_MSG("Dereferenced invalid JSON iterator");
  return error;
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
JSON::Value * JSON::Iter::operator->() const{
  return &(operator*());
}

/// True if not done iterating.
JSON::Iter::operator bool() const{
  return ((myType == JSON::ARRAY && aIt != r->arrVal.end()) || (myType == JSON::OBJECT && oIt != r->objVal.end()));
}

/// Go to next iteration.
JSON::Iter & JSON::Iter::operator++(){
  if (*this){
    ++i;
    if (myType == JSON::ARRAY){
      ++aIt;
    }
    if (myType == JSON::OBJECT){
      ++oIt;
    }
  }
}

/// Return the name of the current indice.
const std::string & JSON::Iter::key() const{
  if (myType == JSON::OBJECT && *this){
    return oIt->first;
  }
  static const std::string empty;
  WARN_MSG("Got key from invalid JSON iterator");
  return empty;
}

/// Return the number of the current indice.
unsigned int JSON::Iter::num() const{
  return i;
}

/// Construct from a root Value to iterate over.
JSON::ConstIter::ConstIter(const Value & root){
  myType = root.myType;
  i = 0;
  r = &root;
  if (!root.size()){myType = JSON::EMPTY;}
  if (myType == JSON::ARRAY){
    aIt = root.arrVal.begin();
  }
  if (myType == JSON::OBJECT){
    oIt = root.objVal.begin();
  }
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
const JSON::Value & JSON::ConstIter::operator*() const{
  if (myType == JSON::ARRAY && aIt != r->arrVal.end()){
    return **aIt;
  }
  if (myType == JSON::OBJECT && oIt != r->objVal.end()){
    return *(oIt->second);
  }
  static JSON::Value error;
  WARN_MSG("Dereferenced invalid JSON iterator");
  return error;
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
const JSON::Value * JSON::ConstIter::operator->() const{
  return &(operator*());
}

/// True if not done iterating.
JSON::ConstIter::operator bool() const{
  return ((myType == JSON::ARRAY && aIt != r->arrVal.end()) || (myType == JSON::OBJECT && oIt != r->objVal.end()));
}

/// Go to next iteration.
JSON::ConstIter & JSON::ConstIter::operator++(){
  if (*this){
    ++i;
    if (myType == JSON::ARRAY){
      ++aIt;
    }
    if (myType == JSON::OBJECT){
      ++oIt;
    }
  }
  return *this;
}

/// Return the name of the current indice.
const std::string & JSON::ConstIter::key() const{
  if (myType == JSON::OBJECT && *this){
    return oIt->first;
  }
  static const std::string empty;
  WARN_MSG("Got key from invalid JSON iterator");
  return empty;
}

/// Return the number of the current indice.
unsigned int JSON::ConstIter::num() const{
  return i;
}

static inline char c2hex(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static inline char hex2c(char c) {
  if (c < 10) {
    return '0' + c;
  }
  if (c < 16) {
    return 'A' + (c - 10);
  }
  return '0';
}

static std::string read_string(int separator, std::istream & fromstream) {
  std::string out;
  bool escaped = false;
  while (fromstream.good()) {
    char c;
    fromstream.get(c);
    if (c == '\\') {
      escaped = true;
      continue;
    }
    if (escaped) {
      switch (c) {
        case 'b':
          out += '\b';
          break;
        case 'f':
          out += '\f';
          break;
        case 'n':
          out += '\n';
          break;
        case 'r':
          out += '\r';
          break;
        case 't':
          out += '\t';
          break;
        case 'u': {
            char d1, d2, d3, d4;
            fromstream.get(d1);
            fromstream.get(d2);
            fromstream.get(d3);
            fromstream.get(d4);
            out.append(1, (c2hex(d4) + (c2hex(d3) << 4)));
            //We ignore the upper two characters.
            // + (c2hex(d2) << 8) + (c2hex(d1) << 16)
            break;
          }
        default:
          out.append(1, c);
          break;
      }
      escaped = false;
    } else {
      if (c == separator) {
        return out;
      } else {
        out.append(1, c);
      }
    }
  }
  return out;
}

static std::string string_escape(const std::string val) {
  std::string out = "\"";
  for (unsigned int i = 0; i < val.size(); ++i) {
    switch (val.data()[i]) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (val.data()[i] < 32 || val.data()[i] > 126) {
          out += "\\u00";
          out += hex2c((val.data()[i] >> 4) & 0xf);
          out += hex2c(val.data()[i] & 0xf);
        } else {
          out += val.data()[i];
        }
        break;
    }
  }
  out += "\"";
  return out;
}

/// Skips an std::istream forward until any of the following characters is seen: ,]}
static void skipToEnd(std::istream & fromstream) {
  while (fromstream.good()) {
    char peek = fromstream.peek();
    if (peek == ',') {
      return;
    }
    if (peek == ']') {
      return;
    }
    if (peek == '}') {
      return;
    }
    peek = fromstream.get();
  }
}

/// Sets this JSON::Value to null;
JSON::Value::Value() {
  null();
}

/// Sets this JSON::Value to null
JSON::Value::~Value() {
  null();
}

JSON::Value::Value(const Value & rhs) {
  null();
  *this = rhs;
}

/// Sets this JSON::Value to read from this position in the std::istream
JSON::Value::Value(std::istream & fromstream) {
  null();
  bool reading_object = false;
  bool reading_array = false;
  bool negative = false;
  bool stop = false;
  while (!stop && fromstream.good()) {
    int c = fromstream.peek();
    switch (c) {
      case '{':
        reading_object = true;
        c = fromstream.get();
        myType = OBJECT;
        break;
      case '[': {
          reading_array = true;
          c = fromstream.get();
          myType = ARRAY;
          Value tmp = JSON::Value(fromstream);
          if (tmp.myType != EMPTY) {
            append(tmp);
          }
          break;
        }
      case '\'':
      case '"':
        c = fromstream.get();
        if (!reading_object) {
          myType = STRING;
          strVal = read_string(c, fromstream);
          stop = true;
        } else {
          std::string tmpstr = read_string(c, fromstream);
          (*this)[tmpstr] = JSON::Value(fromstream);
        }
        break;
      case '-':
        c = fromstream.get();
        negative = true;
        break;
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        c = fromstream.get();
        myType = INTEGER;
        intVal *= 10;
        intVal += c - '0';
        break;
      case ',':
        if (!reading_object && !reading_array) {
          stop = true;
          break;
        }
        c = fromstream.get();
        if (reading_array) {
          append(JSON::Value(fromstream));
        }
        break;
      case '}':
        if (reading_object) {
          c = fromstream.get();
        }
        stop = true;
        break;
      case ']':
        if (reading_array) {
          c = fromstream.get();
        }
        stop = true;
        break;
      case 't':
      case 'T':
        skipToEnd(fromstream);
        myType = BOOL;
        intVal = 1;
        stop = true;
        break;
      case 'f':
      case 'F':
        skipToEnd(fromstream);
        myType = BOOL;
        intVal = 0;
        stop = true;
        break;
      case 'n':
      case 'N':
        skipToEnd(fromstream);
        myType = EMPTY;
        stop = true;
        break;
      default:
        c = fromstream.get(); //ignore this character
        continue;
        break;
    }
  }
  if (negative) {
    intVal *= -1;
  }
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const std::string & val) {
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const char * val) {
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(long long int val) {
  myType = INTEGER;
  intVal = val;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(bool val) {
  myType = BOOL;
  if (val){
    intVal = 1;
  }else{
    intVal = 0;
  }
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator==(const JSON::Value & rhs) const {
  if (myType != rhs.myType){
    return false;
  }
  if (myType == INTEGER || myType == BOOL) {
    return intVal == rhs.intVal;
  }
  if (myType == STRING) {
    return strVal == rhs.strVal;
  }
  if (myType == EMPTY) {
    return true;
  }
  if (size() != rhs.size()){
    return false;
  }
  if (myType == OBJECT) {
    jsonForEachConst(*this, it){
      if (!rhs.isMember(it.key()) || *it != rhs[it.key()]) {
        return false;
      }
    }
    return true;
  }
  if (myType == ARRAY) {
    jsonForEachConst(*this, it){
      if (*it != rhs[it.num()]) {
        return false;
      }
    }
    return true;
  }
  return true;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator!=(const JSON::Value & rhs) const {
  return !((*this) == rhs);
}

/// Completely clears the contents of this value,
/// changing its type to NULL in the process.
void JSON::Value::null() {
  shrink(0);
  strVal.clear();
  intVal = 0;
  myType = EMPTY;
}

/// Sets this JSON::Value to be equal to the given JSON::Value.
JSON::Value & JSON::Value::operator=(const JSON::Value & rhs) {
  null();
  myType = rhs.myType;
  if (myType == STRING){
    strVal = rhs.strVal;
  }
  if (myType == BOOL || myType == INTEGER){
    intVal = rhs.intVal;
  }
  if (myType == OBJECT){
    jsonForEachConst(rhs, i){
      (*this)[i.key()] = *i;
    }
  }
  if (myType == ARRAY){
    jsonForEachConst(rhs, i){
      append(*i);
    }
  }
  return *this;
}

/// Sets this JSON::Value to the given boolean.
JSON::Value & JSON::Value::operator=(const bool & rhs) {
  null();
  myType = BOOL;
  if (rhs) intVal = 1;
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value & JSON::Value::operator=(const std::string & rhs) {
  null();
  myType = STRING;
  strVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value & JSON::Value::operator=(const char * rhs) {
  return ((*this) = (std::string)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const long long int & rhs) {
  null();
  myType = INTEGER;
  intVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const int & rhs) {
  return ((*this) = (long long int)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const unsigned int & rhs) {
  return ((*this) = (long long int)rhs);
}

/// Automatic conversion to long long int - returns 0 if not convertable.
JSON::Value::operator long long int() const {
  if (myType == INTEGER) {
    return intVal;
  }
  if (myType == STRING) {
    return atoll(strVal.c_str());
  }
  return 0;
}

/// Automatic conversion to std::string.
/// Returns the raw string value if available, otherwise calls toString().
JSON::Value::operator std::string() const {
  if (myType == STRING) {
    return strVal;
  } else {
    if (myType == EMPTY) {
      return "";
    } else {
      return toString();
    }
  }
}

/// Automatic conversion to bool.
/// Returns true if there is anything meaningful stored into this value.
JSON::Value::operator bool() const {
  if (myType == STRING) {
    return strVal != "";
  }
  if (myType == INTEGER) {
    return intVal != 0;
  }
  if (myType == BOOL) {
    return intVal != 0;
  }
  if (myType == OBJECT) {
    return size() > 0;
  }
  if (myType == ARRAY) {
    return size() > 0;
  }
  if (myType == EMPTY) {
    return false;
  }
  return false; //unimplemented? should never happen...
}

/// Explicit conversion to std::string.
const std::string JSON::Value::asString() const {
  return (std::string) * this;
}
/// Explicit conversion to long long int.
const long long int JSON::Value::asInt() const {
  return (long long int) * this;
}
/// Explicit conversion to bool.
const bool JSON::Value::asBool() const {
  return (bool) * this;
}

/// Explicit conversion to std::string reference.
/// Returns a direct reference for string type JSON::Value objects,
/// but a reference to a static empty string otherwise.
/// \warning Only save to use when the JSON::Value is a string type!
const std::string & JSON::Value::asStringRef() const {
  static std::string ugly_buffer;
  if (myType == STRING) {
    return strVal;
  }
  return ugly_buffer;
}

/// Explicit conversion to c-string.
/// Returns a direct reference for string type JSON::Value objects,
/// a reference to an empty string otherwise.
/// \warning Only save to use when the JSON::Value is a string type!
const char * JSON::Value::c_str() const {
  if (myType == STRING) {
    return strVal.c_str();
  }
  return "";
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value & JSON::Value::operator[](const std::string i) {
  if (myType != OBJECT) {
    null();
    myType = OBJECT;
  }
  Value * pntr = objVal[i];
  if (!pntr){
    objVal[i] = new JSON::Value();
    pntr = objVal[i];
  }
  return *pntr;
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value & JSON::Value::operator[](const char * i) {
  if (myType != OBJECT) {
    null();
    myType = OBJECT;
  }
  Value * pntr = objVal[i];
  if (!pntr){
    objVal[i] = new JSON::Value();
    pntr = objVal[i];
  }
  return *pntr;
}

/// Retrieves or sets the JSON::Value at this position in the array.
/// Converts destructively to array if not already an array.
JSON::Value & JSON::Value::operator[](unsigned int i) {
  if (myType != ARRAY) {
    null();
    myType = ARRAY;
  }
  while (i >= arrVal.size()) {
    append(new JSON::Value());
  }
  return *arrVal[i];
}

/// Retrieves the JSON::Value at this position in the object.
/// Fails horribly if that values does not exist.
const JSON::Value & JSON::Value::operator[](const std::string i) const {
  return *objVal.find(i)->second;
}

/// Retrieves the JSON::Value at this position in the object.
/// Fails horribly if that values does not exist.
const JSON::Value & JSON::Value::operator[](const char * i) const {
  return *objVal.find(i)->second;
}

/// Retrieves the JSON::Value at this position in the array.
/// Fails horribly if that values does not exist.
const JSON::Value & JSON::Value::operator[](unsigned int i) const {
  return *arrVal[i];
}

/// Packs to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all contents.
/// As a side effect, this function clear the internal buffer of any object-types.
std::string JSON::Value::toPacked() const {
  std::string r;
  if (isInt() || isNull() || isBool()) {
    r += 0x01;
    uint64_t numval = intVal;
    r += *(((char *) &numval) + 7);
    r += *(((char *) &numval) + 6);
    r += *(((char *) &numval) + 5);
    r += *(((char *) &numval) + 4);
    r += *(((char *) &numval) + 3);
    r += *(((char *) &numval) + 2);
    r += *(((char *) &numval) + 1);
    r += *(((char *) &numval));
  }
  if (isString()) {
    r += 0x02;
    r += strVal.size() / (256 * 256 * 256);
    r += strVal.size() / (256 * 256);
    r += strVal.size() / 256;
    r += strVal.size() % 256;
    r += strVal;
  }
  if (isObject()) {
    r += 0xE0;
    if (objVal.size() > 0) {
      jsonForEachConst(*this, i){
        if (i.key().size() > 0) {
          r += i.key().size() / 256;
          r += i.key().size() % 256;
          r += i.key();
          r += i->toPacked();
        }
      }
    }
    r += (char)0x0;
    r += (char)0x0;
    r += (char)0xEE;
  }
  if (isArray()) {
    r += 0x0A;
    jsonForEachConst(*this, i){
      r += i->toPacked();
    }
    r += (char)0x0;
    r += (char)0x0;
    r += (char)0xEE;
  }
  return r;
}
//toPacked

/// Packs and transfers over the network.
/// If the object is a container type, this function will call itself recursively for all contents.
void JSON::Value::sendTo(Socket::Connection & socket) const {
  if (isInt() || isNull() || isBool()) {
    socket.SendNow("\001", 1);
    int tmpHalf = htonl((int)(intVal >> 32));
    socket.SendNow((char *)&tmpHalf, 4);
    tmpHalf = htonl((int)(intVal & 0xFFFFFFFF));
    socket.SendNow((char *)&tmpHalf, 4);
    return;
  }
  if (isString()) {
    socket.SendNow("\002", 1);
    int tmpVal = htonl((int)strVal.size());
    socket.SendNow((char *)&tmpVal, 4);
    socket.SendNow(strVal);
    return;
  }
  if (isObject()) {
    if (isMember("trackid") && isMember("time")) {
      unsigned int trackid = objVal.find("trackid")->second->asInt();
      long long time = objVal.find("time")->second->asInt();
      unsigned int size = 16;
      if (objVal.size() > 0) {
        jsonForEachConst(*this, i){
          if (i.key().size() > 0 && i.key() != "trackid" && i.key() != "time" && i.key() != "datatype") {
            size += 2 + i.key().size() + i->packedSize();
          }
        }
      }
      socket.SendNow("DTP2", 4);
      size = htonl(size);
      socket.SendNow((char *)&size, 4);
      trackid = htonl(trackid);
      socket.SendNow((char *)&trackid, 4);
      int tmpHalf = htonl((int)(time >> 32));
      socket.SendNow((char *)&tmpHalf, 4);
      tmpHalf = htonl((int)(time & 0xFFFFFFFF));
      socket.SendNow((char *)&tmpHalf, 4);
      socket.SendNow("\340", 1);
      if (objVal.size() > 0) {
        jsonForEachConst(*this, i){
          if (i.key().size() > 0 && i.key() != "trackid" && i.key() != "time" && i.key() != "datatype") {
            char sizebuffer[2] = {0, 0};
            sizebuffer[0] = (i.key().size() >> 8) & 0xFF;
            sizebuffer[1] = i.key().size() & 0xFF;
            socket.SendNow(sizebuffer, 2);
            socket.SendNow(i.key());
            i->sendTo(socket);
          }
        }
      }
      socket.SendNow("\000\000\356", 3);
      return;
    }
    if (isMember("tracks")) {
      socket.SendNow("DTSC", 4);
      unsigned int size = htonl(packedSize());
      socket.SendNow((char *)&size, 4);
    }
    socket.SendNow("\340", 1);
    if (objVal.size() > 0) {
      jsonForEachConst(*this, i){
        if (i.key().size() > 0) {
          char sizebuffer[2] = {0, 0};
          sizebuffer[0] = (i.key().size() >> 8) & 0xFF;
          sizebuffer[1] = i.key().size() & 0xFF;
          socket.SendNow(sizebuffer, 2);
          socket.SendNow(i.key());
          i->sendTo(socket);
        }
      }
    }
    socket.SendNow("\000\000\356", 3);
    return;
  }
  if (isArray()) {
    socket.SendNow("\012", 1);
    jsonForEachConst(*this, i){
      i->sendTo(socket);
    }
    socket.SendNow("\000\000\356", 3);
    return;
  }
}//sendTo

/// Returns the packed size of this Value.
unsigned int JSON::Value::packedSize() const {
  if (isInt() || isNull() || isBool()) {
    return 9;
  }
  if (isString()) {
    return 5 + strVal.size();
  }
  if (isObject()) {
    unsigned int ret = 4;
    if (objVal.size() > 0) {
      jsonForEachConst(*this, i){
        if (i.key().size() > 0) {
          ret += 2 + i.key().size() + i->packedSize();
        }
      }
    }
    return ret;
  }
  if (isArray()) {
    unsigned int ret = 4;
    jsonForEachConst(*this, i){
      ret += i->packedSize();
    }
    return ret;
  }
  return 0;
}//packedSize

/// Pre-packs any object-type JSON::Value to a std::string for transfer over the network, including proper DTMI header.
/// Non-object-types will print an error.
/// The internal buffer is guaranteed to be up-to-date after this function is called.
void JSON::Value::netPrepare() {
  if (myType != OBJECT) {
    DEBUG_MSG(DLVL_ERROR, "Only objects may be netpacked!");
    return;
  }
  std::string packed = toPacked();
  //insert proper header for this type of data
  int packID = -1;
  long long unsigned int time = (*this)["time"].asInt();
  std::string dataType;
  if (isMember("datatype") || isMember("trackid")) {
    dataType = (*this)["datatype"].asString();
    if (isMember("trackid")) {
      packID = (*this)["trackid"].asInt();
    } else {
      if ((*this)["datatype"].asString() == "video") {
        packID = 1;
      }
      if ((*this)["datatype"].asString() == "audio") {
        packID = 2;
      }
      if ((*this)["datatype"].asString() == "meta") {
        packID = 3;
      }
      //endmark and the likes...
      if (packID == -1) {
        packID = 0;
      }
    }
    removeMember("time");
    if (packID != 0) {
      removeMember("datatype");
    }
    removeMember("trackid");
    packed = toPacked();
    (*this)["time"] = (long long int)time;
    (*this)["datatype"] = dataType;
    (*this)["trackid"] = packID;
    strVal.resize(packed.size() + 20);
    memcpy((void *)strVal.c_str(), "DTP2", 4);
  } else {
    packID = -1;
    strVal.resize(packed.size() + 8);
    memcpy((void *)strVal.c_str(), "DTSC", 4);
  }
  //insert the packet length at bytes 4-7
  unsigned int size = packed.size();
  if (packID != -1) {
    size += 12;
  }
  size = htonl(size);
  memcpy((void *)(strVal.c_str() + 4), (void *) &size, 4);
  //copy the rest of the string
  if (packID == -1) {
    memcpy((void *)(strVal.c_str() + 8), packed.c_str(), packed.size());
    return;
  }
  packID = htonl(packID);
  memcpy((void *)(strVal.c_str() + 8), (void *) &packID, 4);
  int tmpHalf = htonl((int)(time >> 32));
  memcpy((void *)(strVal.c_str() + 12), (void *) &tmpHalf, 4);
  tmpHalf = htonl((int)(time & 0xFFFFFFFF));
  memcpy((void *)(strVal.c_str() + 16), (void *) &tmpHalf, 4);
  memcpy((void *)(strVal.c_str() + 20), packed.c_str(), packed.size());
}

/// Packs any object-type JSON::Value to a std::string for transfer over the network, including proper DTMI header.
/// Non-object-types will print an error and return an empty string.
/// This function returns a reference to an internal buffer where the prepared data is kept.
/// The internal buffer is *not* made stale if any changes occur inside the object - subsequent calls to toPacked() will clear the buffer,
/// calls to netPrepare will guarantee it is up-to-date.
std::string & JSON::Value::toNetPacked() {
  static std::string emptystring;
  //check if this is legal
  if (myType != OBJECT) {
    INFO_MSG("Ignored attempt to netpack a non-object.");
    return emptystring;
  }
  //if sneaky storage doesn't contain correct data, re-calculate it
  if (strVal.size() == 0 || strVal[0] != 'D' || strVal[1] != 'T') {
    netPrepare();
  }
  return strVal;
}

/// Converts this JSON::Value to valid JSON notation and returns it.
/// Makes absolutely no attempts to pretty-print anything. :-)
std::string JSON::Value::toString() const {
  switch (myType) {
    case INTEGER: {
        std::stringstream st;
        st << intVal;
        return st.str();
        break;
      }
    case BOOL: {
        if (intVal != 0){
          return "true";
        }else{
          return "false";
        }
        break;
      }
    case STRING: {
        return string_escape(strVal);
        break;
      }
    case ARRAY: {
        std::string tmp = "[";
        if (arrVal.size() > 0) {
          jsonForEachConst(*this, i){
            tmp += i->toString();
            if (i.num()+1 != arrVal.size()) {
              tmp += ",";
            }
          }
        }
        tmp += "]";
        return tmp;
        break;
      }
    case OBJECT: {
        std::string tmp2 = "{";
        if (objVal.size() > 0) {
          jsonForEachConst(*this, i){
            tmp2 += string_escape(i.key()) + ":";
            tmp2 += i->toString();
            if (i.num()+1 != objVal.size()) {
              tmp2 += ",";
            }
          }
        }
        tmp2 += "}";
        return tmp2;
        break;
      }
    case EMPTY:
    default:
      return "null";
  }
  return "null"; //should never get here...
}

/// Converts this JSON::Value to valid JSON notation and returns it.
/// Makes an attempt at pretty-printing.
std::string JSON::Value::toPrettyString(int indentation) const {
  switch (myType) {
    case INTEGER: {
        std::stringstream st;
        st << intVal;
        return st.str();
        break;
      }
    case STRING: {
        for (unsigned int i = 0; i < 201 && i < strVal.size(); ++i) {
          if (strVal[i] < 32 || strVal[i] > 126 || strVal.size() > 200) {
            return "\"" + JSON::Value((long long int)strVal.size()).asString() + " bytes of data\"";
          }
        }
        return string_escape(strVal);
        break;
      }
    case ARRAY: {
        if (arrVal.size() > 0) {
          std::string tmp = "[\n" + std::string(indentation + 2, ' ');
          jsonForEachConst(*this, i){
            tmp += i->toPrettyString(indentation + 2);
            if (i.num() + 1 != arrVal.size()) {
              tmp += ", ";
            }
          }
          tmp += "\n" + std::string(indentation, ' ') + "]";
          return tmp;
        } else {
          return "[]";
        }
        break;
      }
    case OBJECT: {
        if (objVal.size() > 0) {
          bool shortMode = false;
          if (size() <= 3 && isMember("len")) {
            shortMode = true;
          }
          std::string tmp2 = "{" + std::string((shortMode ? "" : "\n"));
          jsonForEachConst(*this, i){
            tmp2 += (shortMode ? std::string("") : std::string(indentation + 2, ' ')) + string_escape(i.key()) + ":";
            tmp2 += i->toPrettyString(indentation + 2);
            if (i.num() + 1 != objVal.size()) {
              tmp2 += "," + std::string((shortMode ? " " : "\n"));
            }
          }
          tmp2 += (shortMode ? std::string("") : "\n" + std::string(indentation, ' ')) + "}";
          return tmp2;
        } else {
          return "{}";
        }
        break;
      }
    case EMPTY:
    default:
      return "null";
  }
  return "null"; //should never get here...
}

/// Appends the given value to the end of this JSON::Value array.
/// Turns this value into an array if it is not already one.
void JSON::Value::append(const JSON::Value & rhs) {
  if (myType != ARRAY) {
    null();
    myType = ARRAY;
  }
  arrVal.push_back(new JSON::Value(rhs));
}

/// Prepends the given value to the beginning of this JSON::Value array.
/// Turns this value into an array if it is not already one.
void JSON::Value::prepend(const JSON::Value & rhs) {
  if (myType != ARRAY) {
    null();
    myType = ARRAY;
  }
  arrVal.push_front(new JSON::Value(rhs));
}

/// For array and object JSON::Value objects, reduces them
/// so they contain at most size elements, throwing away
/// the first elements and keeping the last ones.
/// Does nothing for other JSON::Value types, nor does it
/// do anything if the size is already lower or equal to the
/// given size.
void JSON::Value::shrink(unsigned int size) {
  while (arrVal.size() > size) {
    delete arrVal.front();
    arrVal.pop_front();
  }
  while (objVal.size() > size) {
    delete objVal.begin()->second;
    objVal.erase(objVal.begin());
  }
}

/// For object JSON::Value objects, removes the member with
/// the given name, if it exists. Has no effect otherwise.
void JSON::Value::removeMember(const std::string & name) {
  if (objVal.count(name)){
    delete objVal[name];
    objVal.erase(name);
  }
}

/// For object JSON::Value objects, returns true if the
/// given name is a member. Returns false otherwise.
bool JSON::Value::isMember(const std::string & name) const {
  return objVal.count(name) > 0;
}

/// Returns true if this object is an integer.
bool JSON::Value::isInt() const {
  return (myType == INTEGER);
}

/// Returns true if this object is a string.
bool JSON::Value::isString() const {
  return (myType == STRING);
}

/// Returns true if this object is a bool.
bool JSON::Value::isBool() const {
  return (myType == BOOL);
}

/// Returns true if this object is an object.
bool JSON::Value::isObject() const {
  return (myType == OBJECT);
}

/// Returns true if this object is an array.
bool JSON::Value::isArray() const {
  return (myType == ARRAY);
}

/// Returns true if this object is null.
bool JSON::Value::isNull() const {
  return (myType == EMPTY);
}

/// Returns the total of the objects and array size combined.
unsigned int JSON::Value::size() const {
  return objVal.size() + arrVal.size();
}

/// Converts a std::string to a JSON::Value.
JSON::Value JSON::fromString(std::string json) {
  std::istringstream is(json);
  return JSON::Value(is);
}

/// Converts a file to a JSON::Value.
JSON::Value JSON::fromFile(std::string filename) {
  std::ifstream File;
  File.open(filename.c_str());
  JSON::Value ret(File);
  File.close();
  return ret;
}

/// Parses a single DTMI type - used recursively by the JSON::fromDTMI functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data (defaults to 0).
/// \returns A single JSON::Value, parsed from the raw data.
JSON::Value JSON::fromDTMI(const unsigned char * data, unsigned int len, unsigned int & i) {
  JSON::Value ret;
  fromDTMI(data, len, i, ret);
  return ret;
}

/// Parses a single DTMI type - used recursively by the JSON::fromDTMI functions.
/// This function updates i every call with the new position in the data.
/// \param data The raw data to parse.
/// \param len The size of the raw data.
/// \param i Current parsing position in the raw data (defaults to 0).
/// \param ret Will be set to JSON::Value, parsed from the raw data.
void JSON::fromDTMI(const unsigned char * data, unsigned int len, unsigned int & i, JSON::Value & ret) {
  ret.null();
  if (i >= len) {
    return;
  }
  switch (data[i]) {
    case 0x01: { //integer
        if (i + 8 >= len) {
          return;
        }
        unsigned char tmpdbl[8];
        tmpdbl[7] = data[i + 1];
        tmpdbl[6] = data[i + 2];
        tmpdbl[5] = data[i + 3];
        tmpdbl[4] = data[i + 4];
        tmpdbl[3] = data[i + 5];
        tmpdbl[2] = data[i + 6];
        tmpdbl[1] = data[i + 7];
        tmpdbl[0] = data[i + 8];
        i += 9; //skip 8(an uint64_t)+1 forwards
        uint64_t * d = (uint64_t *)tmpdbl;
        ret = (long long int) * d;
        return;
        break;
      }
    case 0x02: { //string
        if (i + 4 >= len) {
          return;
        }
        unsigned int tmpi = data[i + 1] * 256 * 256 * 256 + data[i + 2] * 256 * 256 + data[i + 3] * 256 + data[i + 4]; //set tmpi to UTF-8-long length
        std::string tmpstr = std::string((const char *)data + i + 5, (size_t)tmpi); //set the string data
        if (i + 4 + tmpi >= len) {
          return;
        }
        i += tmpi + 5; //skip length+size+1 forwards
        ret = tmpstr;
        return;
        break;
      }
    case 0xFF: //also object
    case 0xE0: { //object
        ++i;
        while (data[i] + data[i + 1] != 0 && i < len) { //while not encountering 0x0000 (we assume 0x0000EE)
          if (i + 2 >= len) {
            return;
          }
          unsigned int tmpi = data[i] * 256 + data[i + 1]; //set tmpi to the UTF-8 length
          std::string tmpstr = std::string((const char *)data + i + 2, (size_t)tmpi); //set the string data
          i += tmpi + 2; //skip length+size forwards
          ret[tmpstr].null();
          fromDTMI(data, len, i, ret[tmpstr]); //add content, recursively parsed, updating i, setting indice to tmpstr
        }
        i += 3; //skip 0x0000EE
        return;
        break;
      }
    case 0x0A: { //array
        ++i;
        while (data[i] + data[i + 1] != 0 && i < len) { //while not encountering 0x0000 (we assume 0x0000EE)
          JSON::Value tval;
          fromDTMI(data, len, i, tval); //add content, recursively parsed, updating i
          ret.append(tval);
        }
        i += 3; //skip 0x0000EE
        return;
        break;
      }
  }
  DEBUG_MSG(DLVL_FAIL, "Unimplemented DTMI type %hhx, @ %i / %i - returning.", data[i], i, len);
  i += 1;
  return;
} //fromOneDTMI

/// Parses a std::string to a valid JSON::Value.
/// This function will find one DTMI object in the string and return it.
void JSON::fromDTMI(std::string & data, JSON::Value & ret) {
  unsigned int i = 0;
  return fromDTMI((const unsigned char *)data.c_str(), data.size(), i, ret);
} //fromDTMI

/// Parses a std::string to a valid JSON::Value.
/// This function will find one DTMI object in the string and return it.
JSON::Value JSON::fromDTMI(std::string & data) {
  unsigned int i = 0;
  return fromDTMI((const unsigned char *)data.c_str(), data.size(), i);
} //fromDTMI

void JSON::fromDTMI2(std::string & data, JSON::Value & ret) {
  unsigned int i = 0;
  fromDTMI2((const unsigned char *)data.c_str(), data.size(), i, ret);
  return;
}

JSON::Value JSON::fromDTMI2(std::string & data) {
  JSON::Value ret;
  unsigned int i = 0;
  fromDTMI2((const unsigned char *)data.c_str(), data.size(), i, ret);
  return ret;
}

void JSON::fromDTMI2(const unsigned char * data, unsigned int len, unsigned int & i, JSON::Value & ret) {
  if (len < 13) {
    return;
  }
  long long int tmpTrackID = ntohl(((int *)(data + i))[0]);
  long long int tmpTime = ntohl(((int *)(data + i))[1]);
  tmpTime <<= 32;
  tmpTime += ntohl(((int *)(data + i))[2]);
  i += 12;
  fromDTMI(data, len, i, ret);
  ret["time"] = tmpTime;
  ret["trackid"] = tmpTrackID;
  return;
}

JSON::Value JSON::fromDTMI2(const unsigned char * data, unsigned int len, unsigned int & i) {
  JSON::Value ret;
  fromDTMI2(data, len, i, ret);
  return ret;
}
