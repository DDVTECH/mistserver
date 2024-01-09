/// \file json.cpp Holds all JSON-related code.

#include "bitfields.h"
#include "defines.h"
#include "json.h"
#include <arpa/inet.h> //for htonl
#include <fstream>
#include <sstream>
#include <stdint.h> //for uint64_t
#include <stdlib.h>
#include <string.h> //for memcpy

/// Construct from a root Value to iterate over.
JSON::Iter::Iter(Value &root){
  myType = root.myType;
  i = 0;
  r = &root;
  if (!root.size()){myType = JSON::EMPTY;}
  if (myType == JSON::ARRAY){aIt = root.arrVal.begin();}
  if (myType == JSON::OBJECT){oIt = root.objVal.begin();}
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
JSON::Value &JSON::Iter::operator*() const{
  if (myType == JSON::ARRAY && aIt != r->arrVal.end()){return **aIt;}
  if (myType == JSON::OBJECT && oIt != r->objVal.end()){return *(oIt->second);}
  static JSON::Value error;
  WARN_MSG("Dereferenced invalid JSON iterator");
  return error;
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
JSON::Value *JSON::Iter::operator->() const{
  return &(operator*());
}

/// True if not done iterating.
JSON::Iter::operator bool() const{
  return ((myType == JSON::ARRAY && aIt != r->arrVal.end()) ||
          (myType == JSON::OBJECT && oIt != r->objVal.end()));
}

/// Go to next iteration.
JSON::Iter &JSON::Iter::operator++(){
  if (*this){
    ++i;
    if (myType == JSON::ARRAY){++aIt;}
    if (myType == JSON::OBJECT){++oIt;}
  }
  return *this;
}

/// Return the name of the current indice.
const std::string &JSON::Iter::key() const{
  if (myType == JSON::OBJECT && *this){return oIt->first;}
  static const std::string empty;
  WARN_MSG("Got key from invalid JSON iterator");
  return empty;
}

/// Return the number of the current indice.
uint32_t JSON::Iter::num() const{
  return i;
}

/// Delete the current indice from the parent JSON::Value.
/// Resets the iterator to restart from the beginning
void JSON::Iter::remove(){
  if (*this){
    i = 0;
    if (myType == JSON::ARRAY){
      r->removeMember(aIt);
      aIt = r->arrVal.begin();
    }
    if (myType == JSON::OBJECT){
      r->removeMember(oIt);
      oIt = r->objVal.begin();
    }
  }
}

/// Construct from a root Value to iterate over.
JSON::ConstIter::ConstIter(const Value &root){
  myType = root.myType;
  i = 0;
  r = &root;
  if (!root.size()){myType = JSON::EMPTY;}
  if (myType == JSON::ARRAY){aIt = root.arrVal.begin();}
  if (myType == JSON::OBJECT){oIt = root.objVal.begin();}
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
const JSON::Value &JSON::ConstIter::operator*() const{
  if (myType == JSON::ARRAY && aIt != r->arrVal.end()){return **aIt;}
  if (myType == JSON::OBJECT && oIt != r->objVal.end()){return *(oIt->second);}
  static JSON::Value error;
  WARN_MSG("Dereferenced invalid JSON iterator");
  return error;
}

/// Dereferences into a Value reference.
/// If invalid iterator, returns an empty reference and prints a warning message.
const JSON::Value *JSON::ConstIter::operator->() const{
  return &(operator*());
}

/// True if not done iterating.
JSON::ConstIter::operator bool() const{
  return ((myType == JSON::ARRAY && aIt != r->arrVal.end()) ||
          (myType == JSON::OBJECT && oIt != r->objVal.end()));
}

/// Go to next iteration.
JSON::ConstIter &JSON::ConstIter::operator++(){
  if (*this){
    ++i;
    if (myType == JSON::ARRAY){++aIt;}
    if (myType == JSON::OBJECT){++oIt;}
  }
  return *this;
}

/// Return the name of the current indice.
const std::string &JSON::ConstIter::key() const{
  if (myType == JSON::OBJECT && *this){return oIt->first;}
  static const std::string empty;
  WARN_MSG("Got key from invalid JSON iterator");
  return empty;
}

/// Return the number of the current indice.
uint32_t JSON::ConstIter::num() const{
  return i;
}

static inline char c2hex(char c){
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return 0;
}

static inline char hex2c(char c){
  if (c < 10){return '0' + c;}
  if (c < 16){return 'A' + (c - 10);}
  return '0';
}

static std::string UTF8(uint32_t c){
  std::string r;
  if (c <= 0x7F){
    r.append(1, c);
    return r;
  }
  if (c <= 0x7FF){
    r.append(1, 0xC0 | (c >> 6));
    r.append(1, 0x80 | (c & 0x3F));
    return r;
  }
  if (c <= 0x7FF){
    r.append(1, 0xC0 | (c >> 12));
    r.append(1, 0x80 | ((c >> 6) & 0x3F));
    r.append(1, 0x80 | (c & 0x3F));
    return r;
  }
  // Convert from two UTF16 chars to unicode codepoint
  c = (((c >> 16) & 0x3ff) << 10) + ((c & 0xFFFF) & 0x3ff) + 0x10000;
  // Encode to 4-byte UTF8 sequence
  r.append(1, 0xF0 | ((c >> 18) & 0x07));
  r.append(1, 0x80 | ((c >> 12) & 0x3F));
  r.append(1, 0x80 | ((c >> 6) & 0x3F));
  r.append(1, 0x80 | (c & 0x3F));
  return r;
}

static std::string read_string(char separator, std::istream &fromstream){
  std::string out;
  bool escaped = false;
  uint32_t fullChar = 0;
  while (fromstream.good()){
    char c;
    fromstream.get(c);
    if (!escaped && c == '\\'){
      escaped = true;
      continue;
    }
    if (escaped){
      if (fullChar && c != 'u'){
        out += UTF8(fullChar >> 16);
        fullChar = 0;
      }
      switch (c){
      case 'b': out += '\b'; break;
      case '\\': out += '\\'; break;
      case 'f': out += '\f'; break;
      case 'n': out += '\n'; break;
      case 'r': out += '\r'; break;
      case 't': out += '\t'; break;
      case 'x':
        char d1, d2;
        fromstream.get(d1);
        fromstream.get(d2);
        out.append(1, (c2hex(d2) + (c2hex(d1) << 4)));
        break;
      case 'u':{
        char d1, d2, d3, d4;
        fromstream.get(d1);
        if (d1 == separator){goto stopParsing;}
        fromstream.get(d2);
        if (d2 == separator){goto stopParsing;}
        fromstream.get(d3);
        if (d3 == separator){goto stopParsing;}
        fromstream.get(d4);
        if (d4 == separator){goto stopParsing;}
        uint32_t tmpChar = (c2hex(d4) + (c2hex(d3) << 4) + (c2hex(d2) << 8) + (c2hex(d1) << 12));
        if (fullChar && (tmpChar < 0xDC00 || tmpChar > 0xDFFF)){
          // not a low surrogate - handle high surrogate separately!
          out += UTF8(fullChar >> 16);
          fullChar = 0;
        }
        fullChar |= tmpChar;
        if (fullChar >= 0xD800 && fullChar <= 0xDBFF){
          // possibly high surrogate! Read next characters before handling...
          fullChar <<= 16; // save as high surrogate
        }else{
          out += UTF8(fullChar);
          fullChar = 0;
        }
        break;
      }
      default: out.append(1, c); break;
      }
      escaped = false;
    }else{
      if (fullChar){
        out += UTF8(fullChar >> 16);
        fullChar = 0;
      }
      if (c == separator){return out;}
      out.append(1, c);
    }
  }
stopParsing:
  if (fullChar){
    out += UTF8(fullChar >> 16);
    fullChar = 0;
  }
  return out;
}

static std::string UTF16(uint32_t c){
  if (c > 0xFFFF){
    c -= 0x010000;
    return UTF16(0xD800 + ((c >> 10) & 0x3FF)) + UTF16(0xDC00 + (c & 0x3FF));
  }
  std::string ret = "\\u";
  ret += hex2c((c >> 12) & 0xf);
  ret += hex2c((c >> 8) & 0xf);
  ret += hex2c((c >> 4) & 0xf);
  ret += hex2c(c & 0xf);
  return ret;
}

std::string JSON::string_escape(const std::string &val){
  std::string out = "\"";
  for (size_t i = 0; i < val.size(); ++i){
    const char &c = val.data()[i];
    switch (c){
    case '"': out += "\\\""; break;
    case '\\': out += "\\\\"; break;
    case '\n': out += "\\n"; break;
    case '\b': out += "\\b"; break;
    case '\f': out += "\\f"; break;
    case '\r': out += "\\r"; break;
    case '\t': out += "\\t"; break;
    default:
      if (c < 32 || c > 126){
        // we assume our data is UTF-8 encoded internally.
        // JavaScript expects UTF-16, so if we recognize a valid UTF-8 sequence, we turn it into
        // UTF-16 for JavaScript. Anything else is escaped as a single character UTF-16 escape.
        if ((c & 0xC0) == 0xC0){
          // possible UTF-8 sequence
          // check for 2-byte sequence
          if (((c & 0xE0) == 0XC0) && (i + 1 < val.size()) && ((val.data()[i + 1] & 0xC0) == 0x80)){
            // valid 2-byte sequence
            out += UTF16(((c & 0x1F) << 6) | (val.data()[i + 1] & 0x3F));
            i += 1;
            break;
          }
          // check for 3-byte sequence
          if (((c & 0xF0) == 0XE0) && (i + 2 < val.size()) &&
              ((val.data()[i + 1] & 0xC0) == 0x80) && ((val.data()[i + 2] & 0xC0) == 0x80)){
            // valid 3-byte sequence
            out += UTF16(((c & 0x1F) << 12) | ((val.data()[i + 1] & 0x3F) << 6) | (val.data()[i + 2] & 0x3F));
            i += 2;
            break;
          }
          // check for 4-byte sequence
          if (((c & 0xF8) == 0XF0) && (i + 3 < val.size()) && ((val.data()[i + 1] & 0xC0) == 0x80) &&
              ((val.data()[i + 2] & 0xC0) == 0x80) && ((val.data()[i + 3] & 0xC0) == 0x80)){
            // valid 4-byte sequence
            out += UTF16(((c & 0x1F) << 18) | ((val.data()[i + 1] & 0x3F) << 12) |
                         ((val.data()[i + 2] & 0x3F) << 6) | (val.data()[i + 3] & 0x3F));
            i += 3;
            break;
          }
        }
        // Anything else, we encode as a single UTF-16 character.
        out += "\\u00";
        out += hex2c((val.data()[i] >> 4) & 0xf);
        out += hex2c(val.data()[i] & 0xf);
      }else{
        out += val.data()[i];
      }
      break;
    }
  }
  out += "\"";
  return out;
}

/// Skips an std::istream forward until any of the following characters is seen: ,]}
static void skipToEnd(std::istream &fromstream){
  while (fromstream.good()){
    char peek = fromstream.peek();
    if (peek == ','){return;}
    if (peek == ']'){return;}
    if (peek == '}'){return;}
    peek = fromstream.get();
  }
}

/// Sets this JSON::Value to null;
JSON::Value::Value(){
  null();
}

/// Sets this JSON::Value to null
JSON::Value::~Value(){
  null();
}

JSON::Value::Value(const Value &rhs){
  null();
  *this = rhs;
}

/// Sets this JSON::Value to read from this position in the std::istream
JSON::Value::Value(std::istream &fromstream){
  null();
  bool reading_object = false;
  bool reading_array = false;
  bool negative = false;
  bool stop = false;
  while (!stop && fromstream.good()){
    int c = fromstream.peek();
    switch (c){
    case '{':
      reading_object = true;
      c = fromstream.get();
      myType = OBJECT;
      break;
    case '[':{
      reading_array = true;
      c = fromstream.get();
      myType = ARRAY;
      Value tmp = JSON::Value(fromstream);
      if (tmp.myType != EMPTY){append(tmp);}
      break;
    }
    case '\'':
    case '"':
      c = fromstream.get();
      if (!reading_object){
        myType = STRING;
        strVal = read_string(c, fromstream);
        stop = true;
      }else{
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
      if (myType != INTEGER && myType != DOUBLE){myType = INTEGER;}
      if (myType == INTEGER){
        intVal *= 10;
        intVal += c - '0';
      }else{
        dblDivider *= 10;
        dblVal += ((double)((c - '0')) / dblDivider);
      }
      break;
    case '.':
      c = fromstream.get();
      myType = DOUBLE;
      if (negative){
        dblVal = -intVal;
        dblDivider = -1;
      }else{
        dblVal = intVal;
        dblDivider = 1;
      }
      break;
    case ',':
      if (!reading_object && !reading_array){
        stop = true;
        break;
      }
      c = fromstream.get();
      if (reading_array){append(JSON::Value(fromstream));}
      break;
    case '}':
      if (reading_object){c = fromstream.get();}
      stop = true;
      break;
    case ']':
      if (reading_array){c = fromstream.get();}
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
      c = fromstream.get(); // ignore this character
      continue;
      break;
    }
  }
  if (negative){intVal *= -1;}
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const std::string &val){
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const char *val){
  myType = STRING;
  strVal = val;
  intVal = 0;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(uint32_t val){
  myType = INTEGER;
  intVal = val;
}
/// Sets this JSON::Value to the given integer.
JSON::Value::Value(uint64_t val){
  myType = INTEGER;
  intVal = val;
}
/// Sets this JSON::Value to the given integer.
JSON::Value::Value(int32_t val){
  myType = INTEGER;
  intVal = val;
}
/// Sets this JSON::Value to the given integer.
JSON::Value::Value(int64_t val){
  myType = INTEGER;
  intVal = val;
}

/// Sets this JSON::Value to the given double.
JSON::Value::Value(double val){
  myType = DOUBLE;
  dblVal = val;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(bool val){
  myType = BOOL;
  intVal = (val ? 1 : 0);
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator==(const JSON::Value &rhs) const{
  if (myType != rhs.myType){return false;}
  if (myType == INTEGER || myType == BOOL){return intVal == rhs.intVal;}
  if (myType == DOUBLE){return dblVal == rhs.dblVal;}
  if (myType == STRING){return strVal == rhs.strVal;}
  if (myType == EMPTY){return true;}
  if (size() != rhs.size()){return false;}
  if (myType == OBJECT){
    jsonForEachConst(*this, it){
      if (!rhs.isMember(it.key()) || *it != rhs[it.key()]){return false;}
    }
    return true;
  }
  if (myType == ARRAY){
    jsonForEachConst(*this, it){
      if (*it != rhs[it.num()]){return false;}
    }
    return true;
  }
  return true;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator!=(const JSON::Value &rhs) const{
  return !((*this) == rhs);
}

bool JSON::Value::compareExcept(const Value &rhs, const std::set<std::string> &skip) const{
  if (myType == OBJECT){
    jsonForEachConst(*this, it){
      if (skip.count(it.key())){continue;}
      if (!rhs.isMember(it.key()) || !(*it).compareExcept(rhs[it.key()], skip)){return false;}
    }
    jsonForEachConst(rhs, it){
      if (skip.count(it.key())){continue;}
      if (!(*this).isMember(it.key())){return false;}
    }
    return true;
  }
  if (myType == ARRAY){
    if (size() != rhs.size()){return false;}
    jsonForEachConst(*this, it){
      if (!(*it).compareExcept(rhs[it.num()], skip)){return false;}
    }
    return true;
  }
  return ((*this) == rhs);
}

bool JSON::Value::compareOnly(const Value &rhs, const std::set<std::string> &check) const{
  if (myType == OBJECT){
    jsonForEachConst(*this, it){
      if (!check.count(it.key())){continue;}
      if (!rhs.isMember(it.key()) || !(*it).compareOnly(rhs[it.key()], check)){return false;}
    }
    jsonForEachConst(rhs, it){
      if (!check.count(it.key())){continue;}
      if (!(*this).isMember(it.key())){return false;}
    }
    return true;
  }
  if (myType == ARRAY){
    if (size() != rhs.size()){return false;}
    jsonForEachConst(*this, it){
      if (!(*it).compareOnly(rhs[it.num()], check)){return false;}
    }
    return true;
  }
  return ((*this) == rhs);
}

/// Completely clears the contents of this value,
/// changing its type to NULL in the process.
void JSON::Value::null(){
  shrink(0);
  strVal.clear();
  intVal = 0;
  dblVal = 0;
  dblDivider = 1;
  myType = EMPTY;
}

/// Assigns this JSON::Value to the given JSON::Value, skipping given member recursively.
JSON::Value &JSON::Value::assignFrom(const Value &rhs, const std::set<std::string> &skip){
  null();
  myType = rhs.myType;
  if (myType == STRING){strVal = rhs.strVal;}
  if (myType == BOOL || myType == INTEGER){intVal = rhs.intVal;}
  if (myType == DOUBLE){dblVal = rhs.dblVal;}
  if (myType == OBJECT){
    jsonForEachConst(rhs, i){
      if (!skip.count(i.key())){
        JSON::Value tmp;
        tmp.assignFrom(*i, skip);
        (*this)[i.key()] = tmp;
      }
    }
  }
  if (myType == ARRAY){
    jsonForEachConst(rhs, i){
      JSON::Value tmp;
      tmp.assignFrom(*i, skip);
      append(tmp);
    }
  }
  return *this;
}

/// Extends this JSON::Value object with the given JSON::Value object, skipping given member(s) recursively.
JSON::Value &JSON::Value::extend(const Value &rhs, const std::set<std::string> &skip){
  // Null values turn into objects automatically for sanity reasons
  if (myType == EMPTY){myType = OBJECT;}
  // Abort if either value is not an object
  if (myType != rhs.myType || myType != OBJECT){return *this;}
  jsonForEachConst(rhs, i){
    if (!skip.count(i.key())){
      if (!objVal.count(i.key()) || !i->isObject()){
        (*this)[i.key()] = *i;
      }else{
        (*this)[i.key()].extend(*i, skip);
      }
    }
  }
  return *this;
}

/// Sets this JSON::Value to be equal to the given JSON::Value.
JSON::Value &JSON::Value::operator=(const JSON::Value &rhs){
  null();
  myType = rhs.myType;
  if (myType == STRING){strVal = rhs.strVal;}
  if (myType == BOOL || myType == INTEGER){intVal = rhs.intVal;}
  if (myType == DOUBLE){dblVal = rhs.dblVal;}
  if (myType == OBJECT){
    jsonForEachConst(rhs, i){(*this)[i.key()] = *i;}
  }
  if (myType == ARRAY){
    jsonForEachConst(rhs, i){append(*i);}
  }
  return *this;
}

/// Sets this JSON::Value to the given boolean.
JSON::Value &JSON::Value::operator=(const bool &rhs){
  null();
  myType = BOOL;
  intVal = (rhs ? 1 : 0);
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value &JSON::Value::operator=(const std::string &rhs){
  null();
  myType = STRING;
  strVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given string.
JSON::Value &JSON::Value::operator=(const char *rhs){
  return ((*this) = (std::string)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value &JSON::Value::operator=(const int64_t &rhs){
  null();
  myType = INTEGER;
  intVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given integer.
JSON::Value &JSON::Value::operator=(const int32_t &rhs){
  return ((*this) = (int64_t)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value &JSON::Value::operator=(const uint64_t &rhs){
  return ((*this) = (int64_t)rhs);
}

/// Sets this JSON::Value to the given double.
JSON::Value &JSON::Value::operator=(const double &rhs){
  null();
  myType = DOUBLE;
  dblVal = rhs;
  return *this;
}

/// Sets this JSON::Value to the given integer.
JSON::Value &JSON::Value::operator=(const uint32_t &rhs){
  return ((*this) = (int64_t)rhs);
}

/// Automatic conversion to long long int - returns 0 if not convertable.
JSON::Value::operator int64_t() const{
  if (myType == INTEGER){return intVal;}
  if (myType == DOUBLE){return (long long int)dblVal;}
  if (myType == STRING){return atoll(strVal.c_str());}
  return 0;
}

/// Automatic conversion to double - returns 0 if not convertable.
JSON::Value::operator double() const{
  if (myType == INTEGER){return (double)intVal;}
  if (myType == DOUBLE){return dblVal;}
  if (myType == STRING){return atof(strVal.c_str());}
  return 0;
}

/// Automatic conversion to std::string.
/// Returns the raw string value if available, otherwise calls toString().
JSON::Value::operator std::string() const{
  if (myType == STRING){return strVal;}
  if (myType == EMPTY){return "";}
  return toString();
}

/// Automatic conversion to bool.
/// Returns true if there is anything meaningful stored into this value.
JSON::Value::operator bool() const{
  if (myType == STRING){return strVal != "";}
  if (myType == DOUBLE){return dblVal != 0;}
  if (myType == INTEGER || myType == BOOL){return intVal != 0;}
  if (myType == OBJECT || myType == ARRAY){return size() > 0;}
  return false; // Empty or 'unimplemented? should never happen...'
}

/// Explicit conversion to std::string.
std::string JSON::Value::asString() const{
  return (std::string) * this;
}
/// Explicit conversion to long long int.
int64_t JSON::Value::asInt() const{
  return (int64_t) * this;
}
/// Explicit conversion to double.
const double JSON::Value::asDouble() const{
  return (double)*this;
}
/// Explicit conversion to bool.
bool JSON::Value::asBool() const{
  return (bool)*this;
}

/// Explicit conversion to std::string reference.
/// Returns a direct reference for string type JSON::Value objects,
/// but a reference to a static empty string otherwise.
/// \warning Only save to use when the JSON::Value is a string type!
const std::string &JSON::Value::asStringRef() const{
  static std::string ugly_buffer;
  if (myType == STRING){return strVal;}
  return ugly_buffer;
}

/// Explicit conversion to c-string.
/// Returns a direct reference for string type JSON::Value objects,
/// a reference to an empty string otherwise.
/// \warning Only save to use when the JSON::Value is a string type!
const char *JSON::Value::c_str() const{
  if (myType == STRING){return strVal.c_str();}
  return "";
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value &JSON::Value::operator[](const std::string &i){
  if (myType != OBJECT){
    null();
    myType = OBJECT;
  }
  Value *pntr = objVal[i];
  if (!pntr){
    objVal[i] = new JSON::Value();
    pntr = objVal[i];
  }
  return *pntr;
}

/// Retrieves or sets the JSON::Value at this position in the object.
/// Converts destructively to object if not already an object.
JSON::Value &JSON::Value::operator[](const char *i){
  if (myType != OBJECT){
    null();
    myType = OBJECT;
  }
  Value *pntr = objVal[i];
  if (!pntr){
    objVal[i] = new JSON::Value();
    pntr = objVal[i];
  }
  return *pntr;
}

/// Retrieves or sets the JSON::Value at this position in the array.
/// Converts destructively to array if not already an array.
JSON::Value &JSON::Value::operator[](uint32_t i){
  static JSON::Value empty;
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  while (i >= arrVal.size()){append(empty);}
  return *arrVal[i];
}

/// Retrieves the JSON::Value at this position in the object.
/// Fails horribly if that values does not exist.
const JSON::Value &JSON::Value::operator[](const std::string &i) const{
  return *objVal.find(i)->second;
}

/// Retrieves the JSON::Value at this position in the object.
/// Fails horribly if that values does not exist.
const JSON::Value &JSON::Value::operator[](const char *i) const{
  return *objVal.find(i)->second;
}

/// Retrieves the JSON::Value at this position in the array.
/// Fails horribly if that values does not exist.
const JSON::Value &JSON::Value::operator[](uint32_t i) const{
  return *arrVal[i];
}

/// Packs to a std::string for transfer over the network.
/// If the object is a container type, this function will call itself recursively and contain all
/// contents. As a side effect, this function clear the internal buffer of any object-types.
std::string JSON::Value::toPacked() const{
  std::string r;
  if (isInt() || isNull() || isBool()){
    r += 0x01;
    uint64_t numval = intVal;
    r += *(((char *)&numval) + 7);
    r += *(((char *)&numval) + 6);
    r += *(((char *)&numval) + 5);
    r += *(((char *)&numval) + 4);
    r += *(((char *)&numval) + 3);
    r += *(((char *)&numval) + 2);
    r += *(((char *)&numval) + 1);
    r += *(((char *)&numval));
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
      jsonForEachConst(*this, i){
        if (i.key().size() > 0){
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
  if (isArray()){
    r += 0x0A;
    jsonForEachConst(*this, i){r += i->toPacked();}
    r += (char)0x0;
    r += (char)0x0;
    r += (char)0xEE;
  }
  // Note: Will output integers for doubles.
  // This is intentional, as DTSC packets cannot contain doubles.
  if (isDouble()){
    r += 0x01;
    uint64_t numval = intVal;
    r += *(((char *)&numval) + 7);
    r += *(((char *)&numval) + 6);
    r += *(((char *)&numval) + 5);
    r += *(((char *)&numval) + 4);
    r += *(((char *)&numval) + 3);
    r += *(((char *)&numval) + 2);
    r += *(((char *)&numval) + 1);
    r += *(((char *)&numval));
  }
  return r;
}
// toPacked

/// Packs and transfers over the network.
/// If the object is a container type, this function will call itself recursively for all contents.
void JSON::Value::sendTo(Socket::Connection &socket) const{
  if (isInt() || isNull() || isBool()){
    socket.SendNow("\001", 1);
    int tmpHalf = htonl((int)(intVal >> 32));
    socket.SendNow((char *)&tmpHalf, 4);
    tmpHalf = htonl((int)(intVal & 0xFFFFFFFF));
    socket.SendNow((char *)&tmpHalf, 4);
    return;
  }
  if (isString()){
    socket.SendNow("\002", 1);
    int tmpVal = htonl((int)strVal.size());
    socket.SendNow((char *)&tmpVal, 4);
    socket.SendNow(strVal);
    return;
  }
  if (isObject()){
    if (isMember("trackid") && isMember("time")){
      unsigned int trackid = objVal.find("trackid")->second->asInt();
      long long time = objVal.find("time")->second->asInt();
      unsigned int size = 16;
      if (objVal.size() > 0){
        jsonForEachConst(*this, i){
          if (i.key().size() > 0 && i.key() != "trackid" && i.key() != "time" && i.key() != "datatype"){
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
      if (objVal.size() > 0){
        jsonForEachConst(*this, i){
          if (i.key().size() > 0 && i.key() != "trackid" && i.key() != "time" && i.key() != "datatype"){
            char sizebuffer[2] ={0, 0};
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
    if (isMember("tracks")){
      socket.SendNow("DTSC", 4);
      unsigned int size = htonl(packedSize());
      socket.SendNow((char *)&size, 4);
    }
    socket.SendNow("\340", 1);
    if (objVal.size() > 0){
      jsonForEachConst(*this, i){
        if (i.key().size() > 0){
          char sizebuffer[2] ={0, 0};
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
  if (isArray()){
    socket.SendNow("\012", 1);
    jsonForEachConst(*this, i){i->sendTo(socket);}
    socket.SendNow("\000\000\356", 3);
    return;
  }
}// sendTo

/// Returns the packed size of this Value.
uint64_t JSON::Value::packedSize() const{
  if (isInt() || isNull() || isBool()){return 9;}
  if (isString()){return 5 + strVal.size();}
  if (isObject()){
    uint64_t ret = 4;
    if (objVal.size() > 0){
      jsonForEachConst(*this, i){
        if (i.key().size() > 0){ret += 2 + i.key().size() + i->packedSize();}
      }
    }
    return ret;
  }
  if (isArray()){
    uint64_t ret = 4;
    jsonForEachConst(*this, i){ret += i->packedSize();}
    return ret;
  }
  return 0;
}// packedSize

/// Pre-packs any object-type JSON::Value to a std::string for transfer over the network, including
/// proper DTMI header. Non-object-types will print an error. The internal buffer is guaranteed to
/// be up-to-date after this function is called.
void JSON::Value::netPrepare(){
  if (myType != OBJECT){
    ERROR_MSG("Only objects may be netpacked!");
    return;
  }
  std::string packed = toPacked();
  // insert proper header for this type of data
  int32_t packID = -1;
  int64_t time = (*this)["time"].asInt();
  std::string dataType;
  if (isMember("datatype") || isMember("trackid")){
    dataType = (*this)["datatype"].asString();
    if (isMember("trackid")){
      packID = (*this)["trackid"].asInt();
    }else{
      if ((*this)["datatype"].asString() == "video"){packID = 1;}
      if ((*this)["datatype"].asString() == "audio"){packID = 2;}
      if ((*this)["datatype"].asString() == "meta"){packID = 3;}
      // endmark and the likes...
      if (packID == -1){packID = 0;}
    }
    removeMember("time");
    if (packID != 0){removeMember("datatype");}
    removeMember("trackid");
    packed = toPacked();
    (*this)["time"] = time;
    (*this)["datatype"] = dataType;
    (*this)["trackid"] = packID;
    strVal.resize(packed.size() + 20);
    memcpy((void *)strVal.c_str(), "DTP2", 4);
  }else{
    packID = -1;
    strVal.resize(packed.size() + 8);
    memcpy((void *)strVal.c_str(), "DTSC", 4);
  }
  // insert the packet length at bytes 4-7
  size_t size = packed.size();
  if (packID != -1){size += 12;}
  size = htonl(size);
  memcpy((void *)(strVal.c_str() + 4), (void *)&size, 4);
  // copy the rest of the string
  if (packID == -1){
    memcpy((void *)(strVal.c_str() + 8), packed.c_str(), packed.size());
    return;
  }
  packID = htonl(packID);
  memcpy((void *)(strVal.c_str() + 8), (void *)&packID, 4);
  int tmpHalf = htonl((int)(time >> 32));
  memcpy((void *)(strVal.c_str() + 12), (void *)&tmpHalf, 4);
  tmpHalf = htonl((int)(time & 0xFFFFFFFF));
  memcpy((void *)(strVal.c_str() + 16), (void *)&tmpHalf, 4);
  memcpy((void *)(strVal.c_str() + 20), packed.c_str(), packed.size());
}

/// Packs any object-type JSON::Value to a std::string for transfer over the network, including
/// proper DTMI header. Non-object-types will print an error and return an empty string. This
/// function returns a reference to an internal buffer where the prepared data is kept. The internal
/// buffer is *not* made stale if any changes occur inside the object - subsequent calls to
/// toPacked() will clear the buffer, calls to netPrepare will guarantee it is up-to-date.
std::string &JSON::Value::toNetPacked(){
  static std::string emptystring;
  // check if this is legal
  if (myType != OBJECT){
    INFO_MSG("Ignored attempt to netpack a non-object.");
    return emptystring;
  }
  // if sneaky storage doesn't contain correct data, re-calculate it
  if (strVal.size() == 0 || strVal[0] != 'D' || strVal[1] != 'T'){netPrepare();}
  return strVal;
}

/// Converts this JSON::Value to valid JSON notation and returns it.
/// Makes absolutely no attempts to pretty-print anything. :-)
std::string JSON::Value::toString() const{
  switch (myType){
  case INTEGER:{
    std::stringstream st;
    st << intVal;
    return st.str();
    break;
  }
  case DOUBLE:{
    std::stringstream st;
    st.precision(10);
    st << std::fixed << dblVal;
    return st.str();
    break;
  }
  case BOOL:{
    if (intVal != 0){
      return "true";
    }else{
      return "false";
    }
    break;
  }
  case STRING:{
    return JSON::string_escape(strVal);
    break;
  }
  case ARRAY:{
    std::string tmp = "[";
    if (arrVal.size() > 0){
      jsonForEachConst(*this, i){
        tmp += i->toString();
        if (i.num() + 1 != arrVal.size()){tmp += ",";}
      }
    }
    tmp += "]";
    return tmp;
    break;
  }
  case OBJECT:{
    std::string tmp2 = "{";
    if (objVal.size() > 0){
      jsonForEachConst(*this, i){
        tmp2 += JSON::string_escape(i.key()) + ":";
        tmp2 += i->toString();
        if (i.num() + 1 != objVal.size()){tmp2 += ",";}
      }
    }
    tmp2 += "}";
    return tmp2;
    break;
  }
  case EMPTY:
  default: return "null";
  }
  return "null"; // should never get here...
}

/// Converts this JSON::Value to valid JSON notation and returns it.
/// Makes an attempt at pretty-printing.
std::string JSON::Value::toPrettyString(size_t indentation) const{
  switch (myType){
  case INTEGER:{
    std::stringstream st;
    st << intVal;
    return st.str();
    break;
  }
  case DOUBLE:{
    std::stringstream st;
    st.precision(10);
    st << std::fixed << dblVal;
    return st.str();
    break;
  }
  case BOOL:{
    if (intVal != 0){
      return "true";
    }else{
      return "false";
    }
    break;
  }
  case STRING:{
    for (uint8_t i = 0; i < 201 && i < strVal.size(); ++i){
      if (strVal[i] < 32 || strVal[i] > 126 || strVal.size() > 200){
        return "\"" + JSON::Value((int64_t)strVal.size()).asString() + " bytes of data\"";
      }
    }
    return JSON::string_escape(strVal);
    break;
  }
  case ARRAY:{
    if (arrVal.size() > 0){
      std::string tmp = "[\n" + std::string(indentation + 2, ' ');
      jsonForEachConst(*this, i){
        tmp += i->toPrettyString(indentation + 2);
        if (i.num() + 1 != arrVal.size()){tmp += ", ";}
      }
      tmp += "\n" + std::string(indentation, ' ') + "]";
      return tmp;
    }else{
      return "[]";
    }
    break;
  }
  case OBJECT:{
    if (objVal.size() > 0){
      bool shortMode = false;
      if (size() <= 3 && isMember("len")){shortMode = true;}
      std::string tmp2 = "{" + std::string((shortMode ? "" : "\n"));
      jsonForEachConst(*this, i){
        tmp2 += (shortMode ? std::string("") : std::string(indentation + 2, ' ')) +
                JSON::string_escape(i.key()) + ":";
        tmp2 += i->toPrettyString(indentation + 2);
        if (i.num() + 1 != objVal.size()){tmp2 += "," + std::string((shortMode ? " " : "\n"));}
      }
      tmp2 += (shortMode ? std::string("") : "\n" + std::string(indentation, ' ')) + "}";
      return tmp2;
    }else{
      return "{}";
    }
    break;
  }
  case EMPTY:
  default: return "null";
  }
  return "null"; // should never get here...
}

/// Appends the given value to the end of this JSON::Value array.
/// Turns this value into an array if it is not already one.
void JSON::Value::append(const JSON::Value &rhs){
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  arrVal.push_back(new JSON::Value(rhs));
}

/// Appends a null value to the end of this JSON::Value array.
/// Turns this value into an array if it is not already one.
/// Returns a reference to the appended element.
JSON::Value & JSON::Value::append(){
  if (myType != ARRAY){
    null();
    myType = ARRAY;
  }
  arrVal.push_back(new JSON::Value());
  return **arrVal.rbegin();
}

/// Prepends the given value to the beginning of this JSON::Value array.
/// Turns this value into an array if it is not already one.
void JSON::Value::prepend(const JSON::Value &rhs){
  if (myType != ARRAY){
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
void JSON::Value::shrink(unsigned int size){
  while (arrVal.size() > size){
    delete arrVal.front();
    arrVal.pop_front();
  }
  while (objVal.size() > size){
    delete objVal.begin()->second;
    objVal.erase(objVal.begin());
  }
}

/// For object JSON::Value objects, removes the member with
/// the given name, if it exists. Has no effect otherwise.
void JSON::Value::removeMember(const std::string &name){
  if (objVal.count(name)){
    delete objVal[name];
    objVal.erase(name);
  }
}

void JSON::Value::removeMember(const std::deque<Value *>::iterator &it){
  delete (*it);
  arrVal.erase(it);
}

void JSON::Value::removeMember(const std::map<std::string, Value *>::iterator &it){
  delete it->second;
  objVal.erase(it);
}

void JSON::Value::removeNullMembers(){
  bool again = true;
  while (again){
    again = false;
    jsonForEach(*this, m){
      if (m.key().size() && m->isNull()){
        removeMember(m.key());
        again = true;
        break;
      }
    }
  }
}

/// For object JSON::Value objects, returns true if the
/// given name is a member. Returns false otherwise.
bool JSON::Value::isMember(const std::string &name) const{
  return objVal.count(name) > 0;
}

/// Returns true if this object is an integer.
bool JSON::Value::isInt() const{
  return (myType == INTEGER);
}

/// Returns true if this object is a double.
bool JSON::Value::isDouble() const{
  return (myType == DOUBLE);
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

/// Returns the total of the objects and array size combined.
unsigned int JSON::Value::size() const{
  return objVal.size() + arrVal.size();
}

/// Converts a std::string to a JSON::Value.
JSON::Value JSON::fromString(const char *data, uint32_t data_len){
  std::string str(data, data_len);
  return JSON::fromString(str);
}

/// Converts a std::string to a JSON::Value.
JSON::Value JSON::fromString(const std::string &json){
  std::istringstream is(json);
  return JSON::Value(is);
}

/// Converts a file to a JSON::Value.
JSON::Value JSON::fromFile(const std::string &filename){
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
JSON::Value JSON::fromDTMI(const char *data, uint64_t len, uint32_t &i){
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
void JSON::fromDTMI(const char *data, uint64_t len, uint32_t &i, JSON::Value &ret){
  ret.null();
  if (i >= len){return;}
  switch (data[i]){
  case 0x01:{// integer
    if (i + 8 >= len){return;}
    unsigned char tmpdbl[8];
    tmpdbl[7] = data[i + 1];
    tmpdbl[6] = data[i + 2];
    tmpdbl[5] = data[i + 3];
    tmpdbl[4] = data[i + 4];
    tmpdbl[3] = data[i + 5];
    tmpdbl[2] = data[i + 6];
    tmpdbl[1] = data[i + 7];
    tmpdbl[0] = data[i + 8];
    i += 9; // skip 8(an uint64_t)+1 forwards
    ret = *(int64_t *)tmpdbl;
    return;
    break;
  }
  case 0x02:{// string
    if (i + 4 >= len){return;}
    uint32_t tmpi = Bit::btohl(data + i + 1);             // set tmpi to UTF-8-long length
    std::string tmpstr = std::string(data + i + 5, tmpi); // set the string data
    if (i + 4 + tmpi >= len){return;}
    i += tmpi + 5; // skip length+size+1 forwards
    ret = tmpstr;
    return;
    break;
  }
  case 0xFF:   // also object
  case 0xE0:{// object
    ++i;
    while (data[i] + data[i + 1] != 0 && i < len){// while not encountering 0x0000 (we assume 0x0000EE)
      if (i + 2 >= len){return;}
      uint16_t tmpi = Bit::btohs(data + i);                 // set tmpi to the UTF-8 length
      std::string tmpstr = std::string(data + i + 2, tmpi); // set the string data
      i += tmpi + 2;                                        // skip length+size forwards
      ret[tmpstr].null();
      fromDTMI(data, len, i,
               ret[tmpstr]); // add content, recursively parsed, updating i, setting indice to tmpstr
    }
    i += 3; // skip 0x0000EE
    return;
    break;
  }
  case 0x0A:{// array
    ++i;
    while (data[i] + data[i + 1] != 0 && i < len){// while not encountering 0x0000 (we assume 0x0000EE)
      JSON::Value tval;
      fromDTMI(data, len, i, tval); // add content, recursively parsed, updating i
      ret.append(tval);
    }
    i += 3; // skip 0x0000EE
    return;
    break;
  }
  }
  FAIL_MSG("Unimplemented DTMI type %hhx, @ %i / %" PRIu64 " - returning.", data[i], i, len);
  i += 1;
  return;
}// fromOneDTMI

/// Parses a std::string to a valid JSON::Value.
/// This function will find one DTMI object in the string and return it.
void JSON::fromDTMI(const std::string &data, JSON::Value &ret){
  uint32_t i = 0;
  return fromDTMI(data.c_str(), data.size(), i, ret);
}// fromDTMI

/// Parses a std::string to a valid JSON::Value.
/// This function will find one DTMI object in the string and return it.
JSON::Value JSON::fromDTMI(const std::string &data){
  uint32_t i = 0;
  return fromDTMI(data.c_str(), data.size(), i);
}// fromDTMI

void JSON::fromDTMI2(const std::string &data, JSON::Value &ret){
  uint32_t i = 0;
  fromDTMI2(data.c_str(), data.size(), i, ret);
  return;
}

JSON::Value JSON::fromDTMI2(const std::string &data){
  JSON::Value ret;
  uint32_t i = 0;
  fromDTMI2(data.c_str(), data.size(), i, ret);
  return ret;
}

void JSON::fromDTMI2(const char *data, uint64_t len, uint32_t &i, JSON::Value &ret){
  if (len < 13){return;}
  uint32_t tid = Bit::btohl(data + i);
  uint64_t time = Bit::btohll(data + 4);
  i += 12;
  fromDTMI(data, len, i, ret);
  ret["time"] = time;
  ret["trackid"] = tid;
  return;
}

JSON::Value JSON::fromDTMI2(const char *data, uint64_t len, uint32_t &i){
  JSON::Value ret;
  fromDTMI2(data, len, i, ret);
  return ret;
}
