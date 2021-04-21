#include "../lib/bitstream.cpp"
#include <cassert>
#include <iomanip>
#include <iostream>

int main(int argc, char **argv){
  Utils::bitWriter bw;
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  bw.append(1, 3);
  std::string res = bw.str();

  assert(res.size() == 3);
  assert(res[0] == 0x24);
  assert(res[1] == 0x92);
  assert(res[2] == 0x49);
  return 0;
}
