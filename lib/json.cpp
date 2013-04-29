/// \file json.cpp Holds all JSON-related code.

#include "json.h"
#include <sstream>
#include <fstream>
#include <stdlib.h>
#include <stdint.h> //for uint64_t
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl
int JSON::Value::c2hex(int c){
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

std::string JSON::Value::read_string(int separator, std::istream & fromstream){
  std::string out;
  bool escaped = false;
  while (fromstream.good()){
    int c = fromstream.get();
    if (c == '\\'){
      escaped = true;
      continue;
    }
    if (escaped){
      switch (c){
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
          int d1 = fromstream.get();
          int d2 = fromstream.get();
          int d3 = fromstream.get();
          int d4 = fromstream.get();
          c = c2hex(d4) + (c2hex(d3) << 4) + (c2hex(d2) << 8) + (c2hex(d1) << 16);
          break;
        }
        default:
          out += (char)c;
          break;
      }
    }else{
      if (c == separator){
        return out;
      }else{
        out += (char)c;
      }
    }
  }
  return out;
}

std::string JSON::Value::string_escape(std::string val){
  std::string out = "\"";
  for (unsigned int i = 0; i < val.size(); ++i){
    switch (val[i]){
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
        out += val[i];
        break;
    }
  }
  out += "\"";
  return out;
}

/// Skips an std::istream forward until any of the following characters is seen: ,]}
void JSON::Value::skipToEnd(std::istream & fromstream){
  while (fromstream.good()){
    char peek = fromstream.peek();
    if (peek == ','){
      return;
    }
    if (peek == ']'){
      return;
    }
    if (peek == '}'){
      return;
    }
    peek = fromstream.get();
  }
}

/// Sets this JSON::Value to null;
JSON::Value::Value(){
  null();
}

/// Sets this JSON::Value to read from this position in the std::istream
JSON::Value::Value(std::istream & fromstream){
  null();
  bool reading_object = false;
  bool reading_array = false;
  while (fromstream.good()){
    int c = fromstream.peek();
    switch (c){
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
        if (tmp.myType != EMPTY){
          append(tmp);
        }
        break;
      }
      case '\'':
      case '"':
        c = fromstream.get();
        if ( !reading_object){
          myType = STRING;
          strVal = read_string(c, fromstream);
          return;
        }else{
          std::string tmpstr = read_string(c, fromstream);
          ( *this)[tmpstr] = JSON::Value(fromstream);
        }
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
        if ( !reading_object && !reading_array) return;
        c = fromstream.get();
        if (reading_array){
          append(JSON::Value(fromstream));
        }
        break;
      case '}':
        if (reading_object){
          c = fromstream.get();
        }
        return;
        break;
      case ']':
        if (reading_array){
          c = fromstream.get();
        }
        return;
        break;
      case 't':
      case 'T':
        skipToEnd(fromstream);
        myType = BOOL;
        intVal = 1;
        return;
        break;
      case 'f':
      case 'F':
        skipToEnd(fromstream);
        myType = BOOL;
        intVal = 0;
        return;
        break;
      case 'n':
      case 'N':
        skipToEnd(fromstream);
        myType = EMPTY;
        return;
        break;
      default:
        c = fromstream.get(); //ignore this character
        continue;
        break;
    }
  }
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const std::string & val){
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const char * val){
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(long long int val){
  myType = INTEGER;
  intVal = val;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator==(const JSON::Value & rhs) const{
  if (myType != rhs.myType) return false;
  if (myType == INTEGER || myType == BOOL){
    return intVal == rhs.intVal;
  }
  if (myType == STRING){
    return strVal == rhs.strVal;
  }
  if (myType == EMPTY){
    return true;
  }
  if (myType == OBJECT){
    if (objVal.size() != rhs.objVal.size()) return false;
    for (std::map<std::string, Value>::const_iterator it = objVal.begin(); it != objVal.end(); ++it){
      if ( !rhs.isMember(it->first)){
        return false;
      }
      if (it->second != rhs.objVal.find(it->first)->second){
        return false;
      }
    }
    return true;
  }
  if (myType == ARRAY){
    if (arrVal.size() != rhs.arrVal.size()) return false;
    int i = 0;
    for (std::deque<Value>::const_iterator it = arrVal.begin(); it != arrVal.end(); ++it){
      if ( *it != rhs.arrVal[i]){
        return false;
      }
      i++;
    }
    return true;
  }
  return true;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator!=(const JSON::Value & rhs) const{
  return !(( *this) == rhs);
}

/// Sets this JSON::Value to the given boolean.
JSON::Value & JSON::Value::operator=(const bool &rhs){
  null();
  myType = BOOL;
  if (rhs) intVal = 1;
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value & JSON::Value::operator=(const std::string &rhs){
  null();
  myType = STRING;
  strVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value & JSON::Value::operator=(const char * rhs){
  return (( *this) = (std::string)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const long long int &rhs){
  null();
  myType = INTEGER;
  intVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const int &rhs){
  return (( *this) = (long long int)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const unsigned int &rhs){
  return (( *this) = (long long int)rhs);
}

/// Automatic conversion to long long int - returns 0 if not convertable.
JSON::Value::operator long long int(){
  if (myType == INTEGER){
    return intVal;
  }
  if (myType == STRING){
    return atoll(strVal.c_str());
  }
  return 0;
}

/// Automatic conversion to std::string.
/// Returns the raw string value if available, otherwise calls toString().
JSON::Value::operator std::string(){
  if (myType == STRING){
    return strVal;
  }else{
    if (myType == EMPTY){
      return "";
    }else{
      return toString();
    }
  }
}

/// Automatic conversion to bool.
/// Returns true if there is anything meaningful stored into this value.
JSON::Value::operator bool(){
  if (myType == STRING){
    return strVal != "";
  }
  if (myType == INTEGER){
    return intVal != 0;
  }
  if (myType == BOOL){
    return intVal != 0;
  }
  if (myType == OBJECT){
    return size() > 0;
  }
  if (myType == ARRAY){
    return size() > 0;
  }
  if (myType == EMPTY){
    return false;
  }
  return false; //unimplemented? should never happen...
}

/// Explicit conversion to std::string.
const std::string JSON::Value::asString(){
  return (std::string) *this;
}
/// Explicit conversion to long long int.
const long long int JSON::Value::asInt(){
  return (long long int) *this;
}
/// Explicit conversion to bool.
const bool JSON::Value::asBool(){
  return (bool) *this;
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value & JSON::Value::operator[](const std::string i){
  if (myType != OBJECT){
    null();
    myType = OBJECT;
  }
  return objVal[i];
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value & JSON::Value::operator[](const char * i){
  if (myType != OBJECT){
    null();
    myType = OBJECT;
  }
  return objVal[i];
}

/// Retrieves or sets the JSON::Value at this position in the array.
/// Converts destructively to array if not already an array.
JSON::Value & JSON::Value::operator[](unsigned int i){
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  while (i >= arrVal.size()){
    append(JSON::Value());
  }
  return arrVal[i];
}

/// Packs to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all contents.
/// As a side effect, this function clear the internal buffer of any object-types.
std::string JSON::Value::toPacked(){
  std::string r;
  if (isInt() || isNull() || isBool()){
    r += 0x01;
    uint64_t numval = intVal;
    r += *(((char*) &numval) + 7);
    r += *(((char*) &numval) + 6);
    r += *(((char*) &numval) + 5);
    r += *(((char*) &numval) + 4);
    r += *(((char*) &numval) + 3);
    r += *(((char*) &numval) + 2);
    r += *(((char*) &numval) + 1);
    r += *(((char*) &numval));
  }
  if (isString()){
    r += 0x02;
    r += strVal.size() / (256 * 256 * 256);
    r += strVal.size() / (256 * 256);
    r += strVal.size() / 256;
    r += strVal.size() % 256;
    r += strVal;
  }
  if (isObject()){
    r += 0xE0;
    if (objVal.size() > 0){
      for (JSON::ObjIter it = objVal.begin(); it != objVal.end(); it++){
        r += it->first.size() / 256;
        r += it->first.size() % 256;
        r += it->first;
        r += it->second.toPacked();
      }
    }
    r += (char)0x0;
    r += (char)0x0;
    r += (char)0xEE;
    strVal.clear();
  }
  if (isArray()){
    r += 0x0A;
    for (JSON::ArrIter it = arrVal.begin(); it != arrVal.end(); it++){
      r += it->toPacked();
    }
    r += (char)0x0;
    r += (char)0x0;
    r += (char)0xEE;
  }
  return r;
}
//toPacked

/// Pre-packs any object-type JSON::Value to a std::string for transfer over the network, including proper DTMI header.
/// Non-object-types will print an error to stderr.
/// The internal buffer is guaranteed to be up-to-date after this function is called.
void JSON::Value::netPrepare(){
  if (myType != OBJECT){
    fprintf(stderr, "Error: Only objects may be NetPacked!\n");
    return;
  }
  std::string packed = toPacked();
  //insert proper header for this type of data
  int packID = -1;
  long long unsigned int time = objVal["time"].asInt();
  std::string dataType;
  if (isMember("datatype")){
    dataType = objVal["datatype"].asString();
    if (isMember("trackid")){
      packID = objVal["trackid"].asInt();
    }else{ 
      if (objVal["datatype"].asString() == "video"){
        packID = 1;
      }
      if (objVal["datatype"].asString() == "audio"){
        packID = 2;
      }
      if (objVal["datatype"].asString() == "meta"){
        packID = 3;
      }
      if (packID == -1){
        packID = 0;
      }
    }
    removeMember("time");
    removeMember("datatype");
    removeMember("trackid");
    packed = toPacked();
    objVal["time"] = (long long int)time;
    objVal["datatype"] = dataType;
    objVal["trackid"] = packID;
    strVal.resize(packed.size() + 20);
    memcpy((void*)strVal.c_str(), "DTP2", 4);
  }else{
    packID = -1;
    strVal.resize(packed.size() + 8);
    memcpy((void*)strVal.c_str(), "DTSC", 4);
  }
  //insert the packet length at bytes 4-7
  unsigned int size = htonl(packed.size());
  memcpy((void*)(strVal.c_str() + 4), (void*) &size, 4);
  //copy the rest of the string
  if (packID != -1){
    packID = htonl(packID);
    memcpy((void*)(strVal.c_str() + 8), (void*) &packID, 4);
    int tmpHalf = htonl((int)(time >> 32));
    memcpy((void*)(strVal.c_str() + 12), (void*) &tmpHalf, 4);
    tmpHalf = htonl((int)(time & 0xFFFFFFFF));
    memcpy((void*)(strVal.c_str() + 16), (void*) &tmpHalf, 4);
    memcpy((void*)(strVal.c_str() + 20), packed.c_str(), packed.size());
  }else{
    memcpy((void*)(strVal.c_str() + 8), packed.c_str(), packed.size());
  }
}

/// Packs any object-type JSON::Value to a std::string for transfer over the network, including proper DTMI header.
/// Non-object-types will print an error to stderr and return an empty string.
/// This function returns a reference to an internal buffer where the prepared data is kept.
/// The internal buffer is *not* made stale if any changes occur inside the object - subsequent calls to toPacked() will clear the buffer,
/// calls to netPrepare will guarantee it is up-to-date.
std::string & JSON::Value::toNetPacked(){
  static std::string emptystring;
  //check if this is legal
  if (myType != OBJECT){
    fprintf(stderr, "Error: Only objects may be NetPacked!\n");
    return emptystring;
  }
  //if sneaky storage doesn't contain correct data, re-calculate it
  if (strVal.size() == 0 || strVal[0] != 'D' || strVal[1] != 'T'){
    netPrepare();
  }
  return strVal;
}

/// Converts this JSON::Value to valid JSON notation and returns it.
/// Makes absolutely no attempts to pretty-print anything. :-)
std::string JSON::Value::toString(){
  switch (myType){
    case INTEGER: {
      std::stringstream st;
      st << intVal;
      return st.str();
      break;
    }
    case STRING: {
      return string_escape(strVal);
      break;
    }
    case ARRAY: {
      std::string tmp = "[";
      if (arrVal.size() > 0){
        for (ArrIter it = ArrBegin(); it != ArrEnd(); it++){
          tmp += it->toString();
          if (it + 1 != ArrEnd()){
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
      if (objVal.size() > 0){
        ObjIter it3 = ObjEnd();
        --it3;
        for (ObjIter it2 = ObjBegin(); it2 != ObjEnd(); it2++){
          tmp2 += "\"" + it2->first + "\":";
          tmp2 += it2->second.toString();
          if (it2 != it3){
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
std::string JSON::Value::toPrettyString(int indentation){
  switch (myType){
    case INTEGER: {
      std::stringstream st;
      st << intVal;
      return st.str();
      break;
    }
    case STRING: {
      for (unsigned int i = 0; i < 5 && i < strVal.size(); ++i){
        if (strVal[i] < 32 || strVal[i] > 125){
          return JSON::Value((long long int)strVal.size()).asString() + " bytes of binary data";
        }
      }
      return string_escape(strVal);
      break;
    }
    case ARRAY: {
      if (arrVal.size() > 0){
        std::string tmp = "[\n" + std::string(indentation + 2, ' ');
        for (ArrIter it = ArrBegin(); it != ArrEnd(); it++){
          tmp += it->toPrettyString(indentation + 2);
          if (it + 1 != ArrEnd()){
            tmp += ", ";
          }
        }
        tmp += "\n" + std::string(indentation, ' ') + "]";
        return tmp;
      }else{
        return "[]";
      }
      break;
    }
    case OBJECT: {
      if (objVal.size() > 0){
        bool shortMode = false;
        if (size() <= 3 && isMember("len")){
          shortMode = true;
        }
        std::string tmp2 = "{" + std::string((shortMode ? "" : "\n"));
        ObjIter it3 = ObjEnd();
        --it3;
        for (ObjIter it2 = ObjBegin(); it2 != ObjEnd(); it2++){
          tmp2 += (shortMode ? std::string("") : std::string(indentation + 2, ' ')) + "\"" + it2->first + "\":";
          tmp2 += it2->second.toPrettyString(indentation + 2);
          if (it2 != it3){
            tmp2 += "," + std::string((shortMode ? " " : "\n"));
          }
        }
        tmp2 += (shortMode ? std::string("") : "\n" + std::string(indentation, ' ')) + "}";
        return tmp2;
      }else{
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
void JSON::Value::append(const JSON::Value & rhs){
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  arrVal.push_back(rhs);
}

/// Prepends the given value to the beginning of this JSON::Value array.
/// Turns this value into an array if it is not already one.
void JSON::Value::prepend(const JSON::Value & rhs){
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  arrVal.push_front(rhs);
}

/// For array and object JSON::Value objects, reduces them
/// so they contain at most size elements, throwing away
/// the first elements and keeping the last ones.
/// Does nothing for other JSON::Value types, nor does it
/// do anything if the size is already lower or equal to the
/// given size.
void JSON::Value::shrink(unsigned int size){
  if (myType == ARRAY){
    while (arrVal.size() > size){
      arrVal.pop_front();
    }
    return;
  }
  if (myType == OBJECT){
    while (objVal.size() > size){
      objVal.erase(objVal.begin());
    }
    return;
  }
}

/// For object JSON::Value objects, removes the member with
/// the given name, if it exists. Has no effect otherwise.
void JSON::Value::removeMember(const std::string & name){
  objVal.erase(name);
}

/// For object JSON::Value objects, returns true if the
/// given name is a member. Returns false otherwise.
bool JSON::Value::isMember(const std::string & name) const{
  return objVal.count(name) > 0;
}

/// Returns true if this object is an integer.
bool JSON::Value::isInt() const{
  return (myType == INTEGER);
}

/// Returns true if this object is a string.
bool JSON::Value::isString() const{
  return (myType == STRING);
}

/// Returns true if this object is a bool.
bool JSON::Value::isBool() const{
  return (myType == BOOL);
}

/// Returns true if this object is an object.
bool JSON::Value::isObject() const{
  return (myType == OBJECT);
}

/// Returns true if this object is an array.
bool JSON::Value::isArray() const{
  return (myType == ARRAY);
}

/// Returns true if this object is null.
bool JSON::Value::isNull() const{
  return (myType == EMPTY);
}

/// Returns an iterator to the begin of the object map, if any.
JSON::ObjIter JSON::Value::ObjBegin(){
  return objVal.begin();
}

/// Returns an iterator to the end of the object map, if any.
JSON::ObjIter JSON::Value::ObjEnd(){
  return objVal.end();
}

/// Returns an iterator to the begin of the array, if any.
JSON::ArrIter JSON::Value::ArrBegin(){
  return arrVal.begin();
}

/// Returns an iterator to the end of the array, if any.
JSON::ArrIter JSON::Value::ArrEnd(){
  return arrVal.end();
}

/// Returns the total of the objects and array size combined.
unsigned int JSON::Value::size(){
  return objVal.size() + arrVal.size();
}

/// Completely clears the contents of this value,
/// changing its type to NULL in the process.
void JSON::Value::null(){
  objVal.clear();
  arrVal.clear();
  strVal.clear();
  intVal = 0;
  myType = EMPTY;
}

/// Converts a std::string to a JSON::Value.
JSON::Value JSON::fromString(std::string json){
  std::istringstream is(json);
  return JSON::Value(is);
}

/// Converts a file to a JSON::Value.
JSON::Value JSON::fromFile(std::string filename){
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
JSON::Value JSON::fromDTMI(const unsigned char * data, unsigned int len, unsigned int &i){
#if DEBUG >= 10
  fprintf(stderr, "Note: AMF type %hhx found. %i bytes left\n", data[i], len-i);
#endif
  switch (data[i]){
    case 0x01: { //integer
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
      uint64_t * d = (uint64_t*)tmpdbl;
      return JSON::Value((long long int) *d);
    }
      break;
    case 0x02: { //string
      unsigned int tmpi = data[i + 1] * 256 * 256 * 256 + data[i + 2] * 256 * 256 + data[i + 3] * 256 + data[i + 4]; //set tmpi to UTF-8-long length
      std::string tmpstr = std::string((const char *)data + i + 5, (size_t)tmpi); //set the string data
      i += tmpi + 5; //skip length+size+1 forwards
      return JSON::Value(tmpstr);
    }
      break;
    case 0xFF: //also object
    case 0xE0: { //object
      ++i;
      JSON::Value ret;
      while (data[i] + data[i + 1] != 0){ //while not encountering 0x0000 (we assume 0x0000EE)
        unsigned int tmpi = data[i] * 256 + data[i + 1]; //set tmpi to the UTF-8 length
        std::string tmpstr = std::string((const char *)data + i + 2, (size_t)tmpi); //set the string data
        i += tmpi + 2; //skip length+size forwards
        ret[tmpstr] = fromDTMI(data, len, i); //add content, recursively parsed, updating i, setting indice to tmpstr
      }
      i += 3; //skip 0x0000EE
      return ret;
    }
      break;
    case 0x0A: { //array
      JSON::Value ret;
      ++i;
      while (data[i] + data[i + 1] != 0){ //while not encountering 0x0000 (we assume 0x0000EE)
        ret.append(fromDTMI(data, len, i)); //add content, recursively parsed, updating i
      }
      i += 3; //skip 0x0000EE
      return ret;
    }
      break;
  }
#if DEBUG >= 2
  fprintf(stderr, "Error: Unimplemented DTMI type %hhx - returning.\n", data[i]);
#endif
  return JSON::Value();
} //fromOneDTMI

/// Parses a std::string to a valid JSON::Value.
/// This function will find one DTMI object in the string and return it.
JSON::Value JSON::fromDTMI(std::string data){
  unsigned int i = 0;
  return fromDTMI((const unsigned char*)data.c_str(), data.size(), i);
} //fromDTMI

JSON::Value JSON::fromDTMI2(std::string data){
  JSON::Value tmp = fromDTMI(data.substr(12));
  long long int tmpTrackID = ntohl(((int*)(data.c_str()))[0]);
  long long int tmpTime = ntohl(((int*)(data.c_str() + 4))[0]);
  tmpTime << 32;
  tmpTime += ntohl(((int*)(data.c_str() + 8))[0]);
  tmp["time"] = tmpTime;
  tmp["trackid"] = tmpTrackID;
  return tmp;
}
