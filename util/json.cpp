/// \file json.cpp Holds all JSON-related code.

#include "json.h"
#include <sstream>
#include <fstream>

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
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u':{
           int d1 = fromstream.get();
           int d2 = fromstream.get();
           int d3 = fromstream.get();
           int d4 = fromstream.get();
           c = c2hex(d4) + (c2hex(d3) << 4) + (c2hex(d2) << 8) + (c2hex(d1) << 16);
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
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += val[i];
    }
  }
  out += "\"";
  return out;
}


/// Sets this JSON::Value to null;
JSON::Value::Value(){
  null();
}

/// Sets this JSON::Value to read from this position in the std::istream
JSON::Value::Value(std::istream & fromstream){
  null();
  bool reading_object = false;
  bool reading_obj_name = false;
  bool reading_array = false;
  while (fromstream.good()){
    int c = fromstream.peek();
    switch (c){
      case '{':
        reading_object = true;
        reading_obj_name = true;
        c = fromstream.get();
        myType = OBJECT;
        break;
      case '[':
        reading_array = true;
        c = fromstream.get();
        myType = ARRAY;
        append(JSON::Value(fromstream));
        break;
      case '\'':
      case '"':
        c = fromstream.get();
        if (!reading_object || !reading_obj_name){
          myType = STRING;
          strVal = read_string(c, fromstream);
          return;
        }else{
          std::string tmpstr = read_string(c, fromstream);
          (*this)[tmpstr] = JSON::Value(fromstream);
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
        if (!reading_object && !reading_array) return;
        c = fromstream.get();
        if (reading_object){
          reading_obj_name = true;
        }else{
          append(JSON::Value(fromstream));
        }
        break;
      case '}':
        if (reading_object){c = fromstream.get();}
        return;
        break;
      case ']':
        if (reading_array){c = fromstream.get();}
        return;
        break;
      case 't':
      case 'T':
        myType = BOOL;
        intVal = 1;
        return;
        break;
      case 'f':
      case 'F':
        myType = BOOL;
        intVal = 0;
        return;
        break;
      case 'n':
      case 'N':
        myType = EMPTY;
        return;
        break;
      default:
        c = fromstream.get();//ignore this character
        continue;
        break;
    }
  }
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const std::string & val){
  myType = STRING;
  strVal = val;
}

/// Sets this JSON::Value to the given string.
JSON::Value::Value(const char * val){
  myType = STRING;
  strVal = val;
}

/// Sets this JSON::Value to the given integer.
JSON::Value::Value(long long int val){
  myType = INTEGER;
  intVal = val;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator==(const JSON::Value & rhs) const{
  if (myType != rhs.myType) return false;
  if (myType == INTEGER || myType == BOOL){return intVal == rhs.intVal;}
  if (myType == STRING){return strVal == rhs.strVal;}
  if (myType == EMPTY){return true;}
  if (myType == OBJECT){
    if (objVal.size() != rhs.objVal.size()) return false;
    for (std::map<std::string, Value>::const_iterator it = objVal.begin(); it != objVal.end(); ++it){
      if (!rhs.isMember(it->first)){return false;}
      if (it->second != rhs.objVal.find(it->first)->second){return false;}
    }
    return true;
  }
  return true;
}

/// Compares a JSON::Value to another for equality.
bool JSON::Value::operator!=(const JSON::Value & rhs) const{
  return !((*this) == rhs);
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
  return ((*this) = (std::string)rhs);
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
  return ((*this) = (long long int)rhs);
}

/// Sets this JSON::Value to the given integer.
JSON::Value & JSON::Value::operator=(const unsigned int &rhs){
  return ((*this) = (long long int)rhs);
}

/// Automatic conversion to long long int - returns 0 if not an integer type.
JSON::Value::operator long long int(){
  return intVal;
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
      for (ArrIter it = ArrBegin(); it != ArrEnd(); it++){
        tmp += it->toString();
        if (it + 1 != ArrEnd()){tmp += ",";}
      }
      tmp += "]";
      return tmp;
      break;
    }
    case OBJECT: {
      std::string tmp2 = "{";
      ObjIter it3 = ObjEnd();
      --it3;
      for (ObjIter it2 = ObjBegin(); it2 != ObjEnd(); it2++){
        tmp2 += "\"" + it2->first + "\":";
        tmp2 += it2->second.toString();
        if (it2 != it3){tmp2 += ",";}
      }
      tmp2 += "}";
      return tmp2;
      break;
    }
    case EMPTY:
    default:
      return "null";
  }
  return "null";//should never get here...
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
    while (arrVal.size() > size){arrVal.pop_front();}
    return;
  }
  if (myType == OBJECT){
    while (objVal.size() > size){objVal.erase(objVal.begin());}
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
  std::string Result;
  std::ifstream File;
  File.open(filename.c_str());
  while (File.good()){Result += File.get();}
  File.close( );
  return fromString(Result);
}
