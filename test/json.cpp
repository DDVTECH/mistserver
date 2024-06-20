#include "../lib/json.cpp"

#include <iostream>
#include <string>

int main(int argc, char **argv){
  JSON::Value J;
  // If JSON_STRING is set, parse it as JSON.
  if (getenv("JSON_STRING")) {
    J = JSON::fromString(getenv("JSON_STRING"));
  } else {
    // Otherwise, read from stdin
    J = JSON::Value(std::cin);
  }
  std::cout << J.toString() << std::endl;
  std::cout << J.toPrettyString() << std::endl;
  // If JSON_RESULT is set, compare the toString output to it, return 1 if they do not match, printing the two
  if (getenv("JSON_RESULT")) {
    if (J.toString() != getenv("JSON_RESULT")) {
      std::cerr << "Result '" << J.toString() << "' does not match expected '" << getenv("JSON_RESULT") << "'" << std::endl;
      return 1;
    }
  }
  return 0;
}

