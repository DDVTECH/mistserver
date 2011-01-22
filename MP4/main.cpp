#include <iostream>
#include "interface.h"

int main( ) {
  std::cout << "Creating Interface\n";
  Interface * file = new Interface();
  std::cout << "Interface created, deleting it again\n";
  delete file;
  std::cout << "Interface deleted\n";
  return 0;
}
