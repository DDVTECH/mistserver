#include "output_forward.h"

#include <mist/http_parser.h>

namespace Mist {
  OutForward::OutForward(Socket::Connection & conn, Util::Config & _cfg, JSON::Value & _capa)
    : Output(conn, _cfg, _capa) {
    initialize();
    parseData = true;
    wantRequest = false;
  }

  void OutForward::init(Util::Config *cfg, JSON::Value & capa) {
    Output::init(cfg, capa);
    capa["name"] = "Forward";
    capa["friendly"] = "Forward HTTP-pushed data over HTTP without further processing";
    capa["desc"] = "HTTP Push Forwarder";
    capa["deps"] = "This output protocol can currently only be used by the pushing system.";
    capa["PUSHONLY"] = true;

    capa["codecs"][0u][0u].append("rawhls");

    JSON::Value opt;
    opt["arg"] = "string";
    opt["default"] = "";
    opt["arg_num"] = 1;
    opt["help"] = "Ignored, only exists to handle targetParams";
    cfg->addOption("target", opt);

    cfg->addOption("streamname", R"-({
      "arg":"string",
      "short":"s",
      "long":"stream",
      "help":"The name of the stream that this connector will transmit."
    })-");

    capa["push_urls"].append("rawhls://*");
    capa["push_urls"].append("rawhlss://*");

    cfg->addBasicConnectorOptions(capa);
  }

  bool OutForward::isRecording() {
    return true;
  }

  void OutForward::sendNext() {
    std::string tmpData(thisData, thisDataLen);
    HTTP::Parser H;
    H.headerOnly = true;
    if (!H.Read(tmpData)) { return; }
    size_t slash = H.url.find('/', 5); // Find the slash after "/hls/STREAMNAME"
    if (slash == std::string::npos) { return; }

    H.body = tmpData; // H.Read stripped the headers, leaving the raw body in tmpData
    HTTP::URL target(config->getString("target"));
    if (target.protocol == "rawhls") { target.protocol = "http"; }
    if (target.protocol == "rawhlss") { target.protocol = "https"; }

    std::map<std::string, std::string> args;
    if (target.args.size()) { HTTP::parseVars(target.args, args); }

    if (args.count("file") && !args["file"].size()) {
      args["file"] = H.url.substr(slash + 1);
      H.url = "/" + target.path + HTTP::argStr(args, true);
    } else {
      H.url = "/" + target.link(H.url.substr(slash + 1)).path;
      if (args.size()) { H.url += HTTP::argStr(args, true); }
    }

    myConn.open(target.host, target.getPort(), false, target.protocol == "https");
    if (target.getPort() == target.getDefaultPort()) {
      H.SetHeader("Host", target.host);
    } else {
      H.SetHeader("Host", target.host + ":" + std::to_string(target.getPort()));
    }

    H.sendRequest(myConn);

    // Process reply from server
    {
      HTTP::Parser response;
      bool gotResponse = false;
      Event::Loop ev;
      auto attemptFinish = [&]() {
        if (response.Read(myConn)) {
          INFO_MSG("Server response to upload of %s: %s %s", H.url.c_str(), response.url.c_str(), response.method.c_str());
          // If the response is a 2XX code, return 0, otherwise return the default response (2).
          if (response.url.size() && response.url[0] == '2') {
            // Success
          } else {
            // Failure
            WARN_MSG("Failed to upload %s", H.url.c_str());
          }
          gotResponse = true;
        }
      };
      myConn.setBlocking(false);
      ev.addSocket(myConn.getSocket(), [&](void *) {
        while (myConn.spool()) { attemptFinish(); }
      }, 0);
      uint64_t maxWait = Util::bootMS() + 5000;
      attemptFinish();
      while (!gotResponse && Util::bootMS() < maxWait && myConn) { ev.await(1000); }
      if (!gotResponse) { WARN_MSG("No reply from remote server to PUT request"); }
      myConn.close();
    }
  }

  bool OutForward::isReadyForPlay() {
    return true;
  }

} // namespace Mist
