#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/defines.h>
#include <mist/socket.h>
#include <mist/socket_srt.h>
#include <mist/util.h>

Socket::SRTServer server_socket;
static uint64_t sockCount = 0;

void (*oldSignal)(int, siginfo_t *,void *) = 0;
void signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  server_socket.close();
  if (oldSignal){
    oldSignal(signum, sigInfo, ignore);
  }
}

void handleUSR1(int signum, siginfo_t *sigInfo, void *ignore){
  if (!sockCount){
    INFO_MSG("USR1 received - triggering rolling restart (no connections active)");
    Util::Config::is_restarting = true;
    Util::logExitReason("signal USR1, no connections");
    server_socket.close();
    Util::Config::is_active = false;
  }else{
    INFO_MSG("USR1 received - triggering rolling restart when connection count reaches zero");
    Util::Config::is_restarting = true;
    Util::logExitReason("signal USR1, after disconnect wait");
  }
}

// Callback for SRT-serving threads
static void callThreadCallbackSRT(void *srtPtr){
  sockCount++;
  Socket::SRTConnection & srtSock = *(Socket::SRTConnection*)srtPtr;
  int fds[2];
  pipe(fds);
  Socket::Connection Sconn(fds[0], fds[1]);
  HIGH_MSG("Started thread for socket %i", srtSock.getSocket());
  mistOut tmp(Sconn,srtSock);
  tmp.run();
  HIGH_MSG("Closing thread for socket %i", srtSock.getSocket());
  Sconn.close();
  srtSock.close();
  delete &srtSock;
  sockCount--;
  if (!sockCount && Util::Config::is_restarting){
    server_socket.close();
    Util::Config::is_active = false;
    INFO_MSG("Last active connection closed; triggering rolling restart now!");
  }
}

int main(int argc, char *argv[]){
  DTSC::trackValidMask = TRACK_VALID_EXT_HUMAN;
  Util::redirectLogsIfNeeded();
  Util::Config conf(argv[0]);
  mistOut::init(&conf);
  if (conf.parseArgs(argc, argv)){
    if (conf.getBool("json")){
      mistOut::capa["version"] = PACKAGE_VERSION;
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    conf.activate();
    if (mistOut::listenMode()){
      {
        struct sigaction new_action;
        new_action.sa_sigaction = handleUSR1;
        sigemptyset(&new_action.sa_mask);
        new_action.sa_flags = 0;
        sigaction(SIGUSR1, &new_action, NULL);
      }
      if (conf.getInteger("port") && conf.getString("interface").size()){
        server_socket = Socket::SRTServer(conf.getInteger("port"), conf.getString("interface"), false, "output");
      }
      if (!server_socket.connected()){
        DEVEL_MSG("Failure to open socket");
        return 1;
      }
      struct sigaction new_action;
      struct sigaction cur_action;
      new_action.sa_sigaction = signal_handler;
      sigemptyset(&new_action.sa_mask);
      new_action.sa_flags = SA_SIGINFO;
      sigaction(SIGINT, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
      sigaction(SIGHUP, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
      sigaction(SIGTERM, &new_action, &cur_action);
      if (cur_action.sa_sigaction && cur_action.sa_sigaction != oldSignal){
        if (oldSignal){WARN_MSG("Multiple signal handlers! I can't deal with this.");}
        oldSignal = cur_action.sa_sigaction;
      }
      Util::Procs::socketList.insert(server_socket.getSocket());
      while (conf.is_active && server_socket.connected()){
        Socket::SRTConnection S = server_socket.accept(false, "output");
        if (S.connected()){// check if the new connection is valid
          // spawn a new thread for this connection
          tthread::thread T(callThreadCallbackSRT, (void *)new Socket::SRTConnection(S));
          // detach it, no need to keep track of it anymore
          T.detach();
        }else{
          Util::sleep(10); // sleep 10ms
        }
      }
      Util::Procs::socketList.erase(server_socket.getSocket());
      server_socket.close();
      if (conf.is_restarting){
        INFO_MSG("Reloading input...");
        execvp(argv[0], argv);
        FAIL_MSG("Error reloading: %s", strerror(errno));
      }
    }else{
      Socket::Connection S(fileno(stdout), fileno(stdin));
      Socket::SRTConnection tmpSock;
      mistOut tmp(S, tmpSock);
      return tmp.run();
    }
  }
  INFO_MSG("Exit reason: %s", Util::exitReason);
  return 0;
}
