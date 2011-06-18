#include<string>

struct Group{
  int GrpID;
  std::string GrpName;
  std::vector<int> GrpServers;
  std::vector< std::pair<int,std::string> > GrpStreams;
};//Group
