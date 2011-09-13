/// \file dtmi.h
/// Holds all headers for DDVTECH MediaInfo parsing/generation.

#pragma once
#include <vector>
#include <iostream>
//#include <string.h>
#include <string>

/// Holds all DDVTECH Stream Container classes and parsers.
namespace DTSC{

  /// Enumerates all possible DTMI types.
  enum DTMItype {
    DTMI_INT = 0x01, ///< Unsigned 64-bit integer.
    DTMI_STRING = 0x02, ///< String, equivalent to the AMF longstring type.
    DTMI_OBJECT = 0xE0, ///< Object, equivalent to the AMF object type.
    DTMI_OBJ_END = 0xEE, ///< End of object marker.
    DTMI_ROOT = 0xFF ///< Root node for all DTMI data.
  };

  /// Recursive class that holds DDVTECH MediaInfo.
  class DTMI {
    public:
      std::string Indice();
      DTMItype GetType();
      uint64_t NumValue();
      std::string StrValue();
      const char * Str();
      int hasContent();
      void addContent(DTMI c);
      DTMI* getContentP(int i);
      DTMI getContent(int i);
      DTMI* getContentP(std::string s);
      DTMI getContent(std::string s);
      DTMI();
      DTMI(std::string indice, double val, DTMItype setType = DTMI_INT);
      DTMI(std::string indice, std::string val, DTMItype setType = DTMI_STRING);
      DTMI(std::string indice, DTMItype setType = DTMI_OBJECT);
      void Print(std::string indent = "");
      std::string Pack();
    protected:
      std::string myIndice; ///< Holds this objects indice, if any.
      DTMItype myType; ///< Holds this objects AMF0 type.
      std::string strval; ///< Holds this objects string value, if any.
      uint64_t numval; ///< Holds this objects numeric value, if any.
      std::vector<DTMI> contents; ///< Holds this objects contents, if any (for container types).
  };//AMFType

  /// Parses a C-string to a valid DTSC::DTMI.
  DTMI parseDTMI(const unsigned char * data, unsigned int len);
  /// Parses a std::string to a valid DTSC::DTMI.
  DTMI parseDTMI(std::string data);
  /// Parses a single DTMI type - used recursively by the DTSC::parseDTMI() functions.
  DTMI parseOneDTMI(const unsigned char *& data, unsigned int &len, unsigned int &i, std::string name);

};//DTSC namespace

