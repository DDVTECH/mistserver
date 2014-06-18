/// \file amf.h
/// Holds all headers for the AMF namespace.

#pragma once
#include <vector>
#include <iostream>
#include <string>

/// Holds all AMF parsing and creation related functions and classes.
namespace AMF {

  /// Enumerates all possible AMF0 types, adding a special DDVTECH container type for ease of use.
  enum obj0type {
    AMF0_NUMBER = 0x00,
    AMF0_BOOL = 0x01,
    AMF0_STRING = 0x02,
    AMF0_OBJECT = 0x03,
    AMF0_MOVIECLIP = 0x04,
    AMF0_NULL = 0x05,
    AMF0_UNDEFINED = 0x06,
    AMF0_REFERENCE = 0x07,
    AMF0_ECMA_ARRAY = 0x08,
    AMF0_OBJ_END = 0x09,
    AMF0_STRICT_ARRAY = 0x0A,
    AMF0_DATE = 0x0B,
    AMF0_LONGSTRING = 0x0C,
    AMF0_UNSUPPORTED = 0x0D,
    AMF0_RECORDSET = 0x0E,
    AMF0_XMLDOC = 0x0F,
    AMF0_TYPED_OBJ = 0x10,
    AMF0_UPGRADE = 0x11,
    AMF0_DDV_CONTAINER = 0xFF
  };

  /// Enumerates all possible AMF3 types, adding a special DDVTECH container type for ease of use.
  enum obj3type {
    AMF3_UNDEFINED = 0x00,
    AMF3_NULL = 0x01,
    AMF3_FALSE = 0x02,
    AMF3_TRUE = 0x03,
    AMF3_INTEGER = 0x04,
    AMF3_DOUBLE = 0x05,
    AMF3_STRING = 0x06,
    AMF3_XMLDOC = 0x07,
    AMF3_DATE = 0x08,
    AMF3_ARRAY = 0x09,
    AMF3_OBJECT = 0x0A,
    AMF3_XML = 0x0B,
    AMF3_BYTES = 0x0C,
    AMF3_DDV_CONTAINER = 0xFF
  };

  /// Recursive class that holds AMF0 objects.
  /// It supports all AMF0 types (defined in AMF::obj0type), adding support for a special DDVTECH container type.
  class Object {
    public:
      std::string Indice();
      obj0type GetType();
      double NumValue();
      std::string StrValue();
      const char * Str();
      int hasContent();
      void addContent(AMF::Object c);
      Object * getContentP(unsigned int i);
      Object getContent(unsigned int i);
      Object * getContentP(std::string s);
      Object getContent(std::string s);
      Object();
      Object(std::string indice, double val, obj0type setType = AMF0_NUMBER);
      Object(std::string indice, std::string val, obj0type setType = AMF0_STRING);
      Object(std::string indice, obj0type setType = AMF0_OBJECT);
      std::string Print(std::string indent = "");
      std::string Pack();
    protected:
      std::string myIndice; ///< Holds this objects indice, if any.
      obj0type myType; ///< Holds this objects AMF0 type.
      std::string strval; ///< Holds this objects string value, if any.
      double numval; ///< Holds this objects numeric value, if any.
      std::vector<Object> contents; ///< Holds this objects contents, if any (for container types).
  };
  //AMFType

  /// Parses a C-string to a valid AMF::Object.
  Object parse(const unsigned char * data, unsigned int len);
  /// Parses a std::string to a valid AMF::Object.
  Object parse(std::string data);
  /// Parses a single AMF0 type - used recursively by the AMF::parse() functions.
  Object parseOne(const unsigned char *& data, unsigned int & len, unsigned int & i, std::string name);

  /// Recursive class that holds AMF3 objects.
  /// It supports all AMF3 types (defined in AMF::obj3type), adding support for a special DDVTECH container type.
  class Object3 {
    public:
      std::string Indice();
      obj3type GetType();
      double DblValue();
      int IntValue();
      std::string StrValue();
      const char * Str();
      int hasContent();
      void addContent(AMF::Object3 c);
      Object3 * getContentP(int i);
      Object3 getContent(int i);
      Object3 * getContentP(std::string s);
      Object3 getContent(std::string s);
      Object3();
      Object3(std::string indice, int val, obj3type setType = AMF3_INTEGER);
      Object3(std::string indice, double val, obj3type setType = AMF3_DOUBLE);
      Object3(std::string indice, std::string val, obj3type setType = AMF3_STRING);
      Object3(std::string indice, obj3type setType = AMF3_OBJECT);
      std::string Print(std::string indent = "");
      std::string Pack();
    protected:
      std::string myIndice; ///< Holds this objects indice, if any.
      obj3type myType; ///< Holds this objects AMF0 type.
      std::string strval; ///< Holds this objects string value, if any.
      double dblval; ///< Holds this objects double value, if any.
      int intval; ///< Holds this objects int value, if any.
      std::vector<Object3> contents; ///< Holds this objects contents, if any (for container types).
  };
  //AMFType

  /// Parses a C-string to a valid AMF::Object3.
  Object3 parse3(const unsigned char * data, unsigned int len);
  /// Parses a std::string to a valid AMF::Object3.
  Object3 parse3(std::string data);
  /// Parses a single AMF3 type - used recursively by the AMF::parse3() functions.
  Object3 parseOne3(const unsigned char *& data, unsigned int & len, unsigned int & i, std::string name);

} //AMF namespace
