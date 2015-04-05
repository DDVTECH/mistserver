#include <string>

namespace Encryption {
  std::string AES_Crypt(const std::string & data, const std::string & key, std::string & ivec);

  std::string PR_GenerateContentKey(std::string & keyseed, std::string & keyid);
  std::string PR_GuidToByteArray(std::string & guid);
}
