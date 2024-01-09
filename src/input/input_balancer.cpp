#include "input_balancer.h"
#include <mist/defines.h>
#include <mist/encode.h>
#include <mist/http_parser.h>
#include <mist/stream.h>
#include <mist/url.h>

namespace Mist{
  inputBalancer::inputBalancer(Util::Config *cfg) : Input(cfg){
    capa["name"] = "Balancer";
    capa["desc"] =
        "The load balancer input restarts itself as the input a load balancer tells it it should "
        "be. The syntax is in the form 'balance:http://HOST:PORT[?fallback=FALLBACK]', where HOST "
        "and PORT are the host and port of the load balancer and the FALLBACK is the full source "
        "URL that should be used if the load balancer cannot be reached.";
    capa["source_match"] = "balance:*";
    capa["priority"] = 9;
    capa["morphic"] = 1;

    JSON::Value option;
    option["arg"] = "integer";
    option["long"] = "buffer";
    option["short"] = "b";
    option["help"] = "DVR buffer time in ms";
    option["value"].append(50000);
    config->addOption("bufferTime", option);
    capa["optional"]["DVR"]["name"] = "Buffer time (ms)";
    capa["optional"]["DVR"]["help"] =
        "The target available buffer time for this live stream, in milliseconds. This is the time "
        "available to seek around in, and will automatically be extended to fit whole keyframes as "
        "well as the minimum duration needed for stable playback.";
    capa["optional"]["DVR"]["option"] = "--buffer";
    capa["optional"]["DVR"]["type"] = "uint";
    capa["optional"]["DVR"]["default"] = 50000;

    option.null();
    option["arg"] = "integer";
    option["long"] = "cut";
    option["short"] = "c";
    option["help"] = "Any timestamps before this will be cut from the live buffer";
    option["value"].append(0);
    config->addOption("cut", option);
    capa["optional"]["cut"]["name"] = "Cut time (ms)";
    capa["optional"]["cut"]["help"] =
        "Any timestamps before this will be cut from the live buffer.";
    capa["optional"]["cut"]["option"] = "--cut";
    capa["optional"]["cut"]["type"] = "uint";
    capa["optional"]["cut"]["default"] = 0;

    option.null();

    option["arg"] = "integer";
    option["long"] = "resume";
    option["short"] = "R";
    option["help"] = "Enable resuming support (1) or disable resuming support (0, default)";
    option["value"].append(0);
    config->addOption("resume", option);
    capa["optional"]["resume"]["name"] = "Resume support";
    capa["optional"]["resume"]["help"] =
        "If enabled, the buffer will linger after source disconnect to allow resuming the stream "
        "later. If disabled, the buffer will instantly close on source disconnect.";
    capa["optional"]["resume"]["option"] = "--resume";
    capa["optional"]["resume"]["type"] = "select";
    capa["optional"]["resume"]["select"][0u][0u] = "0";
    capa["optional"]["resume"]["select"][0u][1u] = "Disabled";
    capa["optional"]["resume"]["select"][1u][0u] = "1";
    capa["optional"]["resume"]["select"][1u][1u] = "Enabled";
    capa["optional"]["resume"]["default"] = 0;

    option.null();

    option["arg"] = "integer";
    option["long"] = "segment-size";
    option["short"] = "S";
    option["help"] = "Target time duration in milliseconds for segments";
    option["value"].append(5000);
    config->addOption("segmentsize", option);
    capa["optional"]["segmentsize"]["name"] = "Segment size (ms)";
    capa["optional"]["segmentsize"]["help"] = "Target time duration in milliseconds for segments.";
    capa["optional"]["segmentsize"]["option"] = "--segment-size";
    capa["optional"]["segmentsize"]["type"] = "uint";
    capa["optional"]["segmentsize"]["default"] = 5000;
  }

  int inputBalancer::boot(int argc, char *argv[]){
    if (!config->parseArgs(argc, argv)){return 1;}
    if (config->getBool("json")){return Input::boot(argc, argv);}

    streamName = config->getString("streamname");

    std::string blncr = config->getString("input");
    if (blncr.substr(0, 8) != "balance:"){
      FAIL_MSG("Input must start with \"balance:\"");
      return 1;
    }

    HTTP::URL url(blncr.substr(8));
    if (url.protocol != "http"){
      FAIL_MSG("Load balancer protocol %s is not supported", url.protocol.c_str());
      return 1;
    }

    std::string source; // empty by default

    // Parse fallback from URL arguments, if possible.
    if (url.args.size()){
      std::map<std::string, std::string> args;
      HTTP::parseVars(url.args, args);
      if (args.count("fallback")){source = args.at("fallback");}
    }

    Socket::Connection balConn(url.host, url.getPort(), true);
    if (!balConn){
      WARN_MSG("Failed to reach %s on port %" PRIu16, url.host.c_str(), url.getPort());
    }else{
      HTTP::Parser http;
      http.url = "/" + url.path;
      http.SetVar("source", streamName);
      if (source.size()){http.SetVar("fallback", source);}
      http.method = "GET";
      http.SetHeader("Host", url.host);
      http.SetHeader("X-MistServer", PACKAGE_VERSION);
      balConn.SendNow(http.BuildRequest());
      http.Clean();

      unsigned int startTime = Util::epoch();
      while ((Util::epoch() - startTime < 10) && (balConn || balConn.Received().size())){
        if (balConn.spool() || balConn.Received().size()){
          if (http.Read(balConn.Received().get())){
            HTTP::URL newUrl(http.body);
            if (Socket::isLocalhost(newUrl.host)){
              WARN_MSG("Load balancer returned a local address - ignoring");
              startTime = 0;
              break; // break out of while loop, ignore return value - it's local.
            }
            source = http.body;
            startTime = 0; // note success
            break;         // break out of while loop
          }
        }else{
          //Prevent 100% CPU usage if the response is slow
          Util::sleep(25);
        }
      }
      if (startTime){
        FAIL_MSG("Timeout while trying to contact load balancer at %s!", blncr.c_str() + 8);
      }
      balConn.close();
    }

    if (!source.size()){
      FAIL_MSG("Could not determine source to use for %s", streamName.c_str());
      return 1;
    }

    // Attempt to boot the source we got
    Util::startInput(streamName, source, false, getenv("MISTPROVIDER"));
    return 1;
  }

}// namespace Mist
