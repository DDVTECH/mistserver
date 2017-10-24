#include "encode.h"

namespace Encodings{

  /// Needed for base64_encode function
  const std::string Base64::chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  /// Helper for base64_decode function
  inline bool Base64::is_base64(unsigned char c){
    return (isalnum(c) || (c == '+') || (c == '/'));
  }

  /// Used to base64 encode data. Input is the plaintext as std::string, output is the encoded data
  /// as std::string. \param input Plaintext data to encode. \returns Base64 encoded data.
  std::string Base64::encode(std::string const input){
    std::string ret;
    unsigned int in_len = input.size();
    char quad[4], triple[3];
    unsigned int i, x, n = 3;
    for (x = 0; x < in_len; x = x + 3){
      if ((in_len - x) / 3 == 0){n = (in_len - x) % 3;}
      for (i = 0; i < 3; i++){triple[i] = '0';}
      for (i = 0; i < n; i++){triple[i] = input[x + i];}
      quad[0] = chars[(triple[0] & 0xFC) >> 2];                               // FC = 11111100
      quad[1] = chars[((triple[0] & 0x03) << 4) | ((triple[1] & 0xF0) >> 4)]; // 03 = 11
      quad[2] = chars[((triple[1] & 0x0F) << 2) | ((triple[2] & 0xC0) >> 6)]; // 0F = 1111, C0=11110
      quad[3] = chars[triple[2] & 0x3F];                                      // 3F = 111111
      if (n < 3){quad[3] = '=';}
      if (n < 2){quad[2] = '=';}
      for (i = 0; i < 4; i++){ret += quad[i];}
    }
    return ret;
  }// base64_encode

  /// Used to base64 decode data. Input is the encoded data as std::string, output is the plaintext
  /// data as std::string. \param encoded_string Base64 encoded data to decode. \returns Plaintext
  /// decoded data.
  std::string Base64::decode(std::string const &encoded_string){
    int in_len = encoded_string.size();
    int i = 0;
    int j = 0;
    int in_ = 0;
    unsigned char char_array_4[4], char_array_3[3];
    std::string ret;
    while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])){
      char_array_4[i++] = encoded_string[in_];
      in_++;
      if (i == 4){
        for (i = 0; i < 4; i++){char_array_4[i] = chars.find(char_array_4[i]);}
        char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
        char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
        char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
        for (i = 0; (i < 3); i++){ret += char_array_3[i];}
        i = 0;
      }
    }
    if (i){
      for (j = i; j < 4; j++){char_array_4[j] = 0;}
      for (j = 0; j < 4; j++){char_array_4[j] = chars.find(char_array_4[j]);}
      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];
      for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
    }
    return ret;
  }

  /// Encodes a single character as two hex digits in string form.
  std::string Hex::chr(char dec){
    char dig1 = (dec & 0xF0) >> 4;
    char dig2 = (dec & 0x0F);
    if (dig1 <= 9) dig1 += 48;
    if (10 <= dig1 && dig1 <= 15) dig1 += 97 - 10;
    if (dig2 <= 9) dig2 += 48;
    if (10 <= dig2 && dig2 <= 15) dig2 += 97 - 10;
    std::string r;
    r.append(&dig1, 1);
    r.append(&dig2, 1);
    return r;
  }

  /// Decodes a hex-encoded std::string to a raw binary std::string.
  std::string Hex::decode(const std::string &in){
    std::string ret(in.size() / 2, '\000');
    for (size_t i = 0; i < in.size(); ++i){
      char c = in[i];
      ret[i >> 1] |= ((c & 15) + (((c & 64) >> 6) | ((c & 64) >> 3))) << ((~i & 1) << 2);
    }
    return ret;
  }

  /// urlencodes std::string data, leaving only the characters A-Za-z0-9~!&()' alone.
  std::string URL::encode(const std::string &c){
    std::string escaped = "";
    int max = c.length();
    for (int i = 0; i < max; i++){
      if (('0' <= c[i] && c[i] <= '9') || ('a' <= c[i] && c[i] <= 'z') ||
          ('A' <= c[i] && c[i] <= 'Z') ||
          (c[i] == '~' || c[i] == '!' || c[i] == '*' || c[i] == '(' || c[i] == ')' || c[i] == '/' ||
           c[i] == '\'')){
        escaped.append(&c[i], 1);
      }else{
        if (c[i] == ' '){
          escaped.append("+");
        }else{
          escaped.append("%");
          escaped.append(Hex::chr(c[i]));
        }
      }
    }
    return escaped;
  }

  /// urldecodes std::string data, parsing out both %-encoded characters and +-encoded spaces.
  std::string URL::decode(const std::string &in){
    std::string out;
    for (unsigned int i = 0; i < in.length(); ++i){
      if (in[i] == '%'){
        char tmp = 0;
        ++i;
        if (i < in.length()){tmp = Hex::ord(in[i]) << 4;}
        ++i;
        if (i < in.length()){tmp += Hex::ord(in[i]);}
        out += tmp;
      }else{
        if (in[i] == '+'){
          out += ' ';
        }else{
          out += in[i];
        }
      }
    }
    return out;
  }

}// namespace Encodings

