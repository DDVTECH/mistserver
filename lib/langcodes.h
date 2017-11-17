#include <string>

namespace Encodings {

  class ISO639{
    public:
      static std::string decode(const std::string & lang);
      static std::string twoToThree(const std::string & lang);
      static std::string encode(const std::string & lang);
  };


}

