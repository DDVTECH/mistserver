#include <map>
#include <string>

struct Channel{
  int ChID;
  std::string ChName;
  std::string ChSrc;
  std::map<std::string,bool> Presets;
};//Channel
