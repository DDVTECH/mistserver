#pragma once
#include <string>

namespace Secure {
  std::string md5(std::string input);
  std::string md5(const char * input, const unsigned int in_len);
}

