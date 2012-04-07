/// \file json.h Holds all JSON-related headers.

#include <string>
#include <deque>
#include <map>
#include <istream>

/// JSON-related classes and functions
namespace JSON{

  /// Lists all types of JSON::Value.
  enum ValueType{ EMPTY, BOOL, INTEGER, STRING, ARRAY, OBJECT };

  class Value;//forward declaration for below typedef

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
    public:
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
      //array operator for maps and arrays
      Value & operator[](const std::string i);
      Value & operator[](const char * i);
      Value & operator[](unsigned int i);
      //handy functions and others
      std::string toString();
      void append(const Value & rhs);
      void prepend(const Value & rhs);
      void shrink(unsigned int size);
      void removeMember(const std::string & name);
      bool isMember(const std::string & name) const;
      ObjIter ObjBegin();
      ObjIter ObjEnd();
      ArrIter ArrBegin();
      ArrIter ArrEnd();
      unsigned int size();
      void null();
  };

  Value fromString(std::string json);
  
};
