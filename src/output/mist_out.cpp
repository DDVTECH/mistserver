#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/stream.h>
#include <mist/util.h>

#include <signal.h>

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  HIGH_MSG("USR1 received - triggering rolling restart");
  Util::Config::is_restarting = true;
  Util::logExitReason(ER_CLEAN_SIGNAL, "signal USR1");
  Util::Config::is_active = false;
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_EXT_HUMAN;
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistOut::init(&conf);
  if (!conf.parseArgs(argc, argv)) {
    INFO_MSG("Exit reason: %s", Util::exitReason);
    return 1;
  }

  if (conf.getBool("json")) {
    mistOut::capa["version"] = PACKAGE_VERSION;
    std::cout << mistOut::capa.toString() << std::endl;
    return -1;
  }

  {
    std::string defTrkSrt = conf.getString("default_track_sorting");
    if (!defTrkSrt.size()) {
      // defTrkSrt = Util::getGlobalConfig("default_track_sorting").asString();
    }
    if (defTrkSrt.size()) {
      if (defTrkSrt == "bps_lth") { Util::defaultTrackSortOrder = Util::TRKSORT_BPS_LTH; }
      if (defTrkSrt == "bps_htl") { Util::defaultTrackSortOrder = Util::TRKSORT_BPS_HTL; }
      if (defTrkSrt == "id_lth") { Util::defaultTrackSortOrder = Util::TRKSORT_ID_LTH; }
      if (defTrkSrt == "id_htl") { Util::defaultTrackSortOrder = Util::TRKSORT_ID_HTL; }
      if (defTrkSrt == "res_lth") { Util::defaultTrackSortOrder = Util::TRKSORT_RES_LTH; }
      if (defTrkSrt == "res_htl") { Util::defaultTrackSortOrder = Util::TRKSORT_RES_HTL; }
    }
  }

  conf.activate();
  if (conf.getString("connection_handler").size() || !mistOut::listenMode()) {
    {
      struct sigaction new_action;
      new_action.sa_handler = SIG_IGN;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = 0;
      sigaction(SIGUSR1, &new_action, NULL);
    }
    Socket::Connection S(fileno(stdout), fileno(stdin));
    mistOut tmp(S);
    return tmp.run();
  }

  {
    struct sigaction new_action;
    new_action.sa_sigaction = handleUSR1;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, NULL);
  }

  mistOut::listener(conf, [&argc, &argv](Socket::Connection & S, Socket::Server &) {
    int sock = S.getSocket();
    int fdErr = STDERR_FILENO;
    char *const *childArgs = (char *const *)malloc(sizeof(char *) * (argc + 3));
    memcpy((void *)childArgs, argv, sizeof(char *) * argc);
    std::string sockHost = S.getHost();
    ((char **)childArgs)[argc] = (char *)"--connection_handler";
    ((char **)childArgs)[argc + 1] = (char *)sockHost.c_str();
    ((char **)childArgs)[argc + 2] = 0;

    Util::Procs::StartPiped(childArgs, &sock, &sock, &fdErr);
  });

  if (conf.is_restarting && Socket::checkTrueSocket(0)) {
    INFO_MSG("Reloading input while re-using server socket");
    execvp(argv[0], argv);
    FAIL_MSG("Error reloading: %s", strerror(errno));
  }

  INFO_MSG("Exit reason: %s", Util::exitReason);
  return 0;
}

