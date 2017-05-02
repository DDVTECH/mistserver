#include ANALYSERHEADER
#include <mist/config.h>
#include <mist/defines.h>

int main(int argc, char *argv[]){
  Util::Config conf(argv[0]);
  ANALYSERTYPE::init(conf);
  if (conf.parseArgs(argc, argv)){
    conf.activate();
    ANALYSERTYPE A(conf);
    return A.run(conf);
  }else{
    return 2;
  }
}

