#include <iostream>
#include "interface.h"

int main( ) {
  std::cout << "Creating Interface\n";
  Interface * file = new Interface();
  std::cout << "Interface created, start linking them\n";
  file->link();
  std::cout << "Linking finished, deleting boxes\n";
  delete file;
  std::cout << "Interface deleted\n";
  return 0;
}
