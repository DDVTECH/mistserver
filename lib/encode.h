#pragma once
#include <string>

/// Namespace for character encoding functions and classes
namespace Encodings {

  /// Holds base64 decoding and encoding functions.
  class Base64 {
    private:
      static const std::string chars;
      static inline bool is_base64(unsigned char c);
    public:
      static std::string encode(std::string const input);
      static std::string decode(std::string const & encoded_string);
  };

  /// urlencoding and urldecoding functions
  class URL {
    public:
      /// urldecodes std::string data, parsing out both %-encoded characters and +-encoded spaces.
      static std::string decode(const std::string & in);
      /// urlencodes std::string data, leaving only the characters A-Za-z0-9~!&()' alone.
      static std::string encode(const std::string & c, const std::string &ign = "");

  };

  /// Hexadecimal-related functions
  class Hex {
    public:
    /// Decodes a single hexadecimal character to integer, case-insensitive.
    static inline int ord(char c){
      return ((c&15) + (((c&64)>>6) | ((c&64)>>3)));
    }
    /// Encodes a single character as two hex digits in string form.
    static std::string chr(char dec);

    /// Decodes a hex-encoded std::string to a raw binary std::string.
    static std::string decode(const std::string & in);
  };

}

