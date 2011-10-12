/// \file spawntest/main.cpp
/// Contains a testing program for the Util::Proc utility class.

#include <iostream>
#include <string>
#include "../util/proc.h" //Process utility

/// Testing program for Util::Proc utility class.
int main(){
  Util::Procs::Start("number1", "./test.sh Koekjes");
  sleep(1); sleep(1); sleep(1); sleep(1); sleep(1);
  Util::Procs::Start("number2", "./testpipein.sh", "./testpipeout.sh");
  sleep(1); sleep(1); sleep(1); sleep(1); sleep(1);
  return 0;
}//main
