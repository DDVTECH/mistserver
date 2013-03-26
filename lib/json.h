/// \file json.h Holds all JSON-related headers.

#pragma once
#include <string>
#include <deque>
#include <map>
#include <istream>

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

  /// A JSON::Value is either a string or an integer, but may also be an object, array or null.
  class Value{
    private:
      ValueType myType;
      long long int intVal;
      std::string strVal;
      std::deque<Value> arrVal;
      std::map<std::string, Value> objVal;
      std::string read_string(int separator, std::istream & fromstream);
      std::string string_escape(std::string val);
      int c2hex(int c);
      static void skipToEnd(std::istream & fromstream);
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
      operator long long int();
      operator std::string();
      operator bool();
      const std::string asString();
      const long long int asInt();
      const bool asBool();
      //array operator for maps and arrays
      Value & operator[](const std::string i);
      Value & operator[](const char * i);
      Value & operator[](unsigned int i);
      //handy functions and others
      std::string toPacked();
      void netPrepare();
      std::string & toNetPacked();
      std::string toString();
      std::string toPrettyString(int indentation = 0);
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
      unsigned int size();
      void null();
  };

  Value fromDTMI(std::string data);
  Value fromDTMI(const unsigned char * data, unsigned int len, unsigned int &i);
  Value fromString(std::string json);
  Value fromFile(std::string filename);

}
