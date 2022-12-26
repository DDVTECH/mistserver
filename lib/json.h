/// \file json.h Holds all JSON-related headers.
#pragma once
#include "socket.h"
#include <deque>
#include <istream>
#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

static const std::set<std::string> emptyset;

/// JSON-related classes and functions
namespace JSON{

  /// Lists all types of JSON::Value.
  enum ValueType{EMPTY, BOOL, INTEGER, DOUBLE, STRING, ARRAY, OBJECT};

  /// JSON-string-escapes a value
  std::string string_escape(const std::string &val);

  /// A JSON::Value is either a string or an integer, but may also be an object, array or null.
  class Value{
    friend class Iter;
    friend class ConstIter;

  private:
    ValueType myType;
    long long int intVal;
    std::string strVal;
    double dblVal;
    double dblDivider;
    std::deque<Value *> arrVal;
    std::map<std::string, Value *> objVal;

  public:
    // constructors/destructors
    Value();
    ~Value();
    Value(const Value &rhs);
    Value(std::istream &fromstream);
    Value(const std::string &val);
    Value(const char *val);
    Value(int32_t val);
    Value(int64_t val);
    Value(uint32_t val);
    Value(uint64_t val);
#if defined(__APPLE__)
    Value(unsigned long val);
#endif
    Value(double val);
    Value(bool val);
    // comparison operators
    bool operator==(const Value &rhs) const;
    bool operator!=(const Value &rhs) const;
    bool compareExcept(const Value &rhs, const std::set<std::string> &skip = emptyset) const;
    bool compareOnly(const Value &rhs, const std::set<std::string> &check = emptyset) const;
    // assignment operators
    Value &extend(const Value &rhs, const std::set<std::string> &skip = emptyset);
    Value &assignFrom(const Value &rhs, const std::set<std::string> &skip = emptyset);
    Value &operator=(const Value &rhs);
    Value &operator=(const std::string &rhs);
    Value &operator=(const char *rhs);
    Value &operator=(const int64_t &rhs);
    Value &operator=(const int32_t &rhs);
    Value &operator=(const uint64_t &rhs);
#if defined(__APPLE__)
    Value &operator=(const unsigned long &rhs);
#endif
    Value &operator=(const uint32_t &rhs);
    Value &operator=(const double &rhs);
    Value &operator=(const bool &rhs);
    // converts to basic types
    operator int64_t() const;
    operator std::string() const;
    operator bool() const;
    operator double() const;
    std::string asString() const;
    int64_t asInt() const;
    bool asBool() const;
    const double asDouble() const;
    const std::string &asStringRef() const;
    const char *c_str() const;
    // array operator for maps and arrays
    Value &operator[](const std::string &i);
    Value &operator[](const char *i);
    Value &operator[](uint32_t i);
    const Value &operator[](const std::string &i) const;
    const Value &operator[](const char *i) const;
    const Value &operator[](uint32_t i) const;
    // handy functions and others
    std::string toPacked() const;
    void sendTo(Socket::Connection &socket) const;
    uint64_t packedSize() const;
    void netPrepare();
    std::string &toNetPacked();
    std::string toString() const;
    std::string toPrettyString(size_t indent = 0) const;
    void append(const Value &rhs);
    Value & append();
    void prepend(const Value &rhs);
    void shrink(uint32_t size);
    void removeMember(const std::string &name);
    void removeMember(const std::deque<Value *>::iterator &it);
    void removeMember(const std::map<std::string, Value *>::iterator &it);
    void removeNullMembers();
    bool isMember(const std::string &name) const;
    bool isInt() const;
    bool isDouble() const;
    bool isString() const;
    bool isBool() const;
    bool isObject() const;
    bool isArray() const;
    bool isNull() const;
    uint32_t size() const;
    void null();
  };

  Value fromDTMI2(const std::string &data);
  Value fromDTMI2(const char *data, uint64_t len, uint32_t &i);
  Value fromDTMI(const std::string &data);
  Value fromDTMI(const char *data, uint64_t len, uint32_t &i);
  Value fromString(const std::string &json);
  Value fromString(const char *data, uint32_t data_len);
  Value fromFile(const std::string &filename);
  void fromDTMI2(const std::string &data, Value &ret);
  void fromDTMI2(const char *data, uint64_t len, uint32_t &i, Value &ret);
  void fromDTMI(const std::string &data, Value &ret);
  void fromDTMI(const char *data, uint64_t len, uint32_t &i, Value &ret);

  class Iter{
  public:
    Iter(Value &root);              ///< Construct from a root Value to iterate over.
    Value &operator*() const;       ///< Dereferences into a Value reference.
    Value *operator->() const;      ///< Dereferences into a Value reference.
    operator bool() const;          ///< True if not done iterating.
    Iter &operator++();             ///< Go to next iteration.
    const std::string &key() const; ///< Return the name of the current indice.
    uint32_t num() const;           ///< Return the number of the current indice.
    void remove();                  ///< Delete the current indice from the parent JSON::Value.
  private:
    ValueType myType;
    Value *r;
    uint32_t i;
    std::deque<Value *>::iterator aIt;
    std::map<std::string, Value *>::iterator oIt;
  };
  class ConstIter{
  public:
    ConstIter(const Value &root);    ///< Construct from a root Value to iterate over.
    const Value &operator*() const;  ///< Dereferences into a Value reference.
    const Value *operator->() const; ///< Dereferences into a Value reference.
    operator bool() const;           ///< True if not done iterating.
    ConstIter &operator++();         ///< Go to next iteration.
    const std::string &key() const;  ///< Return the name of the current indice.
    uint32_t num() const;            ///< Return the number of the current indice.
  private:
    ValueType myType;
    const Value *r;
    uint32_t i;
    std::deque<Value *>::const_iterator aIt;
    std::map<std::string, Value *>::const_iterator oIt;
  };
#define jsonForEach(val, i) for (JSON::Iter i(val); i; ++i)
#define jsonForEachConst(val, i) for (JSON::ConstIter i(val); i; ++i)
}// namespace JSON
