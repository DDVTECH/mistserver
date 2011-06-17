#include<string>

struct Group{
  std::string GrpName;
  std::vector<Server> GrpServers;
  std::vector< std::pair<std::string,std::string> > GrpStreams;
};//Group
