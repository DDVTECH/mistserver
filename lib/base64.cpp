#include "base64.h"

/// Needed for base64_encode function
const std::string Base64::chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  /// Helper for base64_decode function
inline bool Base64::is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}

/// Used to base64 encode data. Input is the plaintext as std::string, output is the encoded data as std::string.
/// \param input Plaintext data to encode.
/// \returns Base64 encoded data.
std::string Base64::encode(std::string const input) {
  std::string ret;
  unsigned int in_len = input.size();
  char quad[4], triple[3];
  unsigned int i, x, n = 3;
  for (x = 0; x < in_len; x = x + 3){
    if ((in_len - x) / 3 == 0){n = (in_len - x) % 3;}
    for (i=0; i < 3; i++){triple[i] = '0';}
    for (i=0; i < n; i++){triple[i] = input[x + i];}
    quad[0] = chars[(triple[0] & 0xFC) >> 2]; // FC = 11111100
    quad[1] = chars[((triple[0] & 0x03) << 4) | ((triple[1] & 0xF0) >> 4)]; // 03 = 11
    quad[2] = chars[((triple[1] & 0x0F) << 2) | ((triple[2] & 0xC0) >> 6)]; // 0F = 1111, C0=11110
    quad[3] = chars[triple[2] & 0x3F]; // 3F = 111111
    if (n < 3){quad[3] = '=';}
    if (n < 2){quad[2] = '=';}
    for(i=0; i < 4; i++){ret += quad[i];}
  }
  return ret;
}//base64_encode

/// Used to base64 decode data. Input is the encoded data as std::string, output is the plaintext data as std::string.
/// \param encoded_string Base64 encoded data to decode.
/// \returns Plaintext decoded data.
std::string Base64::decode(std::string const& encoded_string) {
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;
  while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_]; in_++;
    if (i ==4) {
      for (i = 0; i <4; i++){char_array_4[i] = chars.find(char_array_4[i]);}
      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
      for (i = 0; (i < 3); i++){ret += char_array_3[i];}
      i = 0;
    }
  }
  if (i) {
    for (j = i; j <4; j++){char_array_4[j] = 0;}
    for (j = 0; j <4; j++){char_array_4[j] = chars.find(char_array_4[j]);}
    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }
  return ret;
}
