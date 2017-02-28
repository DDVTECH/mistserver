#include INPUTTYPE 

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0]);
  mistIn conv(&conf);
  return conv.boot(argc, argv);
}

