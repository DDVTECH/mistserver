#include <iostream>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <mist/stream.h>
#include <mist/defines.h>

#include "input_folder.h"

namespace Mist {
  inputFolder::inputFolder(Util::Config * cfg) : Input(cfg) {
    capa["name"] = "Folder";
    capa["decs"] = "Folder input, re-starts itself as the appropiate input.";
    capa["source_match"] = "/*/";
    capa["priority"] = 9ll;
  }
}
