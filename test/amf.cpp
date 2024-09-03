#include <mist/amf.h>
#include <mist/json.h>

#include <iostream>

// Helper functions for TAP
size_t testCount = 0;
void testRes(bool success, std::string desc, std::function<void()> onFail) {
  std::cout << (success ? "ok" : "not ok") << " " << ++testCount << " - " << desc << std::endl;
  if (!success) { onFail(); }
}

int main(int argc, const char *argv[]) {
  // TAP header line listing test count
  std::cout << "TAP version 14" << std::endl << "1..2" << std::endl;

  AMF::Object A = AMF::parse("\002\000\013onClockSync\003\000\013streamClock\000A\302{\t "
                             "\000\000\000\000\017streamClockBase\000@"
                             "\306\322\200\000\000\000\000\000\twallClock\000A\302{.P\200\000\000\000\000\t",
                             86);
  JSON::Value jData;
  jData["streamClock"] = 620106304;
  jData["streamClockBase"] = 11685;
  jData["wallClock"] = 620125345;
  testRes((A.getContentP(0)->StrValue() == "onClockSync") && (A.getContentP(1)->toJSON() == jData),
          "Clock metadata message", [&]() {
    std::cerr << "Mismatch: " << A.getContentP(0)->StrValue() << " != onClockSync or " << A.getContentP(1)->toJSON()
              << " != " << jData;
  });

  JSON::Value preJSON = JSON::fromString(R"({"0":0, "test":"test", "double":0.1, "array":[0,1,2,3,{"foo":"bar"}]})");
  testRes(AMF::fromJSON(preJSON).toJSON() == preJSON, "AMF<->JSON conversion",
          [&]() { std::cerr << "Mismatch: " << AMF::fromJSON(preJSON).toJSON() << " != " << preJSON; });

  return 0;
}
