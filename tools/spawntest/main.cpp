/// \file spawntest/main.cpp
/// Contains a testing program for the Util::Proc utility class.

#include <iostream>
#include <string>
#include "../../util/util.h" //Process utility

/// Sleeps a maximum of five seconds, each second being interruptable by a signal.
void sleepFive(){
  sleep(1); sleep(1); sleep(1); sleep(1); sleep(1);
}

/// Testing program for Util::Proc utility class.
int main(){
  Util::Procs::Start("number1", "./test.sh Koekjes");
  sleepFive();
  Util::Procs::Start("number2", "./testpipein.sh", "./testpipeout.sh");
  sleepFive();
  Util::Procs::Start("number3", "./infitest.sh");
  sleepFive();
  Util::Procs::Stop("number3");
  Util::Procs::Start("number4", "./infitest.sh", "./testpipeout.sh");
  sleepFive();
  Util::Procs::Stop("number4");
  return 0;
}//main
