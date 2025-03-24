#include <mist/util.h>

#include <iostream>

int main(int argc, char **argv) {
  if (argc < 2 && !getenv("INSTR")) {
    std::cout << "Usage: " << argv[0] << " [string to split]" << std::endl;
    std::cout << "Also accepts string as INSTR env variable" << std::endl;
    std::cout << "If set, tests each parsed argument against env var OUT1, OUT2, OUT3, etc as well "
                 "as OUTC for the total count"
              << std::endl;
    return 1;
  }

  std::string input;
  if (getenv("INSTR")) { input = getenv("INSTR"); }
  if (argc >= 2) { input = argv[1]; }

  std::deque<std::string> out;
  Util::shellSplit(input, out);

  size_t i = 0;
  int ret = 0;
  for (const std::string & arg : out) {
    std::cout << "Argument " << ++i << " = " << arg << std::endl;
    std::string o = "OUT" + std::to_string(i);
    if (getenv(o.c_str())) {
      std::string target = getenv(o.c_str());
      if (target != arg) {
        std::cerr << "Argument " << i << " is '" << arg << "' but was expected to be '" << target
                  << "'" << std::endl;
        ret = 1;
      }
    }
  }
  if (getenv("OUTC") && atoi(getenv("OUTC")) != i) {
    std::cerr << "Argument count is " << i << " but was expected to be " << getenv("OUTC") << std::endl;
    ret = 1;
  }

  return ret;
}
