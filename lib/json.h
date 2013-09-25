/// \file json.h Holds all JSON-related headers.

#pragma once
#include <string>
#include <deque>
#include <map>
#include <istream>
#include <vector>

//empty definition of DTSC::Stream so it can be a friend.
namespace DTSC {
  class Stream;
}

/// JSON-related classes and functions
namespace JSON {

  /// Lists all types of JSON::Value.
  enum ValueType{
    EMPTY, BOOL, INTEGER, STRING, ARRAY, OBJECT
  };

  class Value;
  //forward declaration for below typedef

  typedef std::map<std::string, Value>::iterator ObjIter;
  typedef std::deque<Value>::iterator ArrIter;
  typedef std::map<std::string, Value>::const_iterator ObjConstIter;
  typedef std::deque<Value>::const_iterator ArrConstIter;
  
  /// A JSON::Value is either a string or an integer, but may also be an object, array or null.
  class Value{
    private:
      ValueType myType;
      long long int intVal;
      std::string strVal;
      std::deque<Value> arrVal;
      std::map<std::string, Value> objVal;
    public:
      //friends
      friend class DTSC::Stream; //for access to strVal
      //constructors
      Value();
      Value(std::istream & fromstream);
      Value(const std::string & val);
      Value(const char * val);
      Value(long long int val);
      Value(bool val);
      //comparison operators
      bool operator==(const Value &rhs) const;
      bool operator!=(const Value &rhs) const;
      //assignment operators
      Value & operator=(const std::string &rhs);
      Value & operator=(const char * rhs);
      Value & operator=(const long long int &rhs);
      Value & operator=(const int &rhs);
      Value & operator=(const unsigned int &rhs);
      Value & operator=(const bool &rhs);
      //converts to basic types
      operator long long int() const;
      operator std::string() const;
      operator bool() const;
      const std::string asString() const;
      const long long int asInt() const;
      const bool asBool() const;
      const std::string & asStringRef() const;
      const char * c_str() const;
      //array operator for maps and arrays
      Value & operator[](const std::string i);
      Value & operator[](const char * i);
      Value & operator[](unsigned int i);
      const Value & operator[](const std::string i) const;
      const Value & operator[](const char * i) const;
      const Value & operator[](unsigned int i) const;
      //handy functions and others
      std::string toPacked();
      void netPrepare();
      std::string & toNetPacked();
      std::string toString() const;
      std::string toPrettyString(int indentation = 0) const;
      void append(const Value & rhs);
      void prepend(const Value & rhs);
      void shrink(unsigned int size);
      void removeMember(const std::string & name);
      bool isMember(const std::string & name) const;
      bool isInt() const;
      bool isString() const;
      bool isBool() const;
      bool isObject() const;
      bool isArray() const;
      bool isNull() const;
      ObjIter ObjBegin();
      ObjIter ObjEnd();
      ArrIter ArrBegin();
      ArrIter ArrEnd();
      ObjConstIter ObjBegin() const;
      ObjConstIter ObjEnd() const;
      ArrConstIter ArrBegin() const;
      ArrConstIter ArrEnd() const;
      unsigned int size() const;
      void null();
  };

  Value fromDTMI2(std::string data);
  Value fromDTMI2(const unsigned char * data, unsigned int len, unsigned int &i);
  Value fromDTMI(std::string data);
  Value fromDTMI(const unsigned char * data, unsigned int len, unsigned int &i);
  Value fromString(std::string json);
  Value fromFile(std::string filename);

  std::string encodeVector(std::vector<long long int> & toEncode);

  template <typename T>
  std::string encodeVector(T begin, T end){
    std::string result;
    for( T it = begin; it != end; it++){
      long long int tmp = (*it);
      while(tmp >= 0xFFFF){
        result += (char)0xFF;
        result += (char)0xFF;
        tmp -= 0xFFFF;
      }
      result += (char)(tmp / 256);
      result += (char)(tmp % 256);
    }
    return result;
  }

  template <typename T>
  void decodeVector( std::string input, T & result ){
    result.clear();
    unsigned int tmp = 0;
    for( int i = 0; i < input.size(); i += 2){
      unsigned int curLen = (input[i] << 8) + input[i + 1];
      tmp += curLen;
      if (curLen != 0xFFFF){
        result.push_back(tmp);
        tmp = 0;
      }
    }
  }
}
