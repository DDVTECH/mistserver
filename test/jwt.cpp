#include <mist/jwt.h>

#include <cassert>
#include <string>

int main(int argc, char **argv) {
  assert(argc > 2 && "Usage: jwttest <alg> <token> <key>(for alg!=none)");
  JWT::JWS jws(std::string(argv[2]), 7, false);

  // The implementation should understand 'none' for 'alg' and always reject it as valid signature
  if (std::string(argv[1]) == "none") {
    assert(!jws.validateSignature());
    return 0;
  }

  // When algorithm is not 'none' we need the key present in the args
  assert(argc > 3 && "Usage: jwttest <alg> <token> <key>");

  // Check whether the signature is valid using permissions to do anything for every stream
  const JWT::Key key = JWT::Key(JSON::fromString(std::string(argv[3])), "*", 7);
  assert(jws.validateSignature(key));
  return 0;
}
