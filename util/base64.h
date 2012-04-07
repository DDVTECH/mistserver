#include <string>

/// Holds base64 decoding and encoding functions.
class Base64{
  private:
    static const std::string chars;
    static inline bool is_base64(unsigned char c);
  public:
    static std::string encode(std::string const input);
    static std::string decode(std::string const& encoded_string);
};
