#include <mist/json.h>

void addGenericProcessOptions(JSON::Value & capa){
  capa["optional"]["debug"]["name"] = "Debug level";
  capa["optional"]["debug"]["type"] = "debug";
  capa["optional"]["debug"]["help"] = "Debug message level for process. Default inherits from the stream setting.";
  capa["optional"]["debug"]["sort"] = "aaaaa";

  capa["optional"]["start_control"]["name"] = "Process start behaviour";
  capa["optional"]["start_control"]["type"] = "group";
  capa["optional"]["start_control"]["help"] = "Control when the process starts";
  capa["optional"]["start_control"]["sort"] = "aaaa";
  {
    JSON::Value &grp = capa["optional"]["start_control"]["options"];
    grp["restart_delay"]["name"] = "Restart delay";
    grp["restart_delay"]["help"] = "The time in milliseconds between restarts. If set to 0 it will restart immediately";
    grp["restart_delay"]["type"] = "uint";
    grp["restart_delay"]["unit"] = "ms";
    grp["restart_delay"]["default"] = 0;

    grp["restart_type"]["name"] = "Restart behaviour";
    grp["restart_type"]["help"] = "What to do when the process exits or fails for any reason. Fixed waits the restart delay every time. Exponential backoff will increase the delay up to max the configured delay for each restart. Disabled will not restart the process.";
    grp["restart_type"]["type"] = "select";
    grp["restart_type"]["select"][0u][0u] = "fixed";
    grp["restart_type"]["select"][0u][1u] = "Fixed delay";
    grp["restart_type"]["select"][1u][0u] = "backoff";
    grp["restart_type"]["select"][1u][1u] = "Exponential backoff";
    grp["restart_type"]["select"][2u][0u] = "disabled";
    grp["restart_type"]["select"][2u][1u] = "Disabled";
    grp["restart_type"]["value"] = "fixed";

    grp["track_inhibit"]["name"] = "Track inhibitor(s)";
    grp["track_inhibit"]["help"] =
        "What tracks to use as inhibitors. If this track selector is able to select a track, the "
        "process does not start. Defaults to none.";
    grp["track_inhibit"]["type"] = "string";
    grp["track_inhibit"]["validate"][0u] = "track_selector";
    grp["track_inhibit"]["default"] = "audio=none&video=none&subtitle=none";

    grp["inconsequential"]["name"] = "Inconsequential process";
    grp["inconsequential"]["help"] = "If set, this process need not be running for a stream to be considered fully active.";
    grp["inconsequential"]["default"] = false;
  }
}

