#include <signal.h>
#include "ddv_socket.h" //DDVTech Socket wrapper
#include "flv_tag.h" //FLV parsing with DDVTech Socket wrapper
DDV::ServerSocket server_socket(-1);

void termination_handler (int signum){
  if (!server_socket.connected()) return;
  switch (signum){
    case SIGINT: break;
    case SIGHUP: break;
    case SIGTERM: break;
    default: return; break;
  }
  server_socket.close();
}

int main(int argc, char ** argv){
  DDV::Socket CONN_fd(-1);
  
  //setup signal handler
  struct sigaction new_action;
  new_action.sa_handler = termination_handler;
  sigemptyset (&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);
  
  int listen_port = DEFAULT_PORT;
  bool daemon_mode = true;
  std::string interface = "0.0.0.0";
  
  int opt = 0;
  static const char *optString = "np:i:h?";
  static const struct option longOpts[] = {
    {"help",0,0,'h'},
    {"port",1,0,'p'},
    {"interface",1,0,'i'},
    {"no-daemon",0,0,'n'}
  };
  while ((opt = getopt_long(argc, argv, optString, longOpts, 0)) != -1){
    switch (opt){
      case 'p':
        listen_port = atoi(optarg);
        break;
      case 'i':
        interface = optarg;
        break;
      case 'n':
        daemon_mode = false;
        break;
      case 'h':
      case '?':
        printf("Options: -h[elp], -?, -n[o-daemon], -p[ort] #\n");
        return 1;
        break;
    }
  }
  
  server_socket = DDV::ServerSocket(listen_port, interface);
  #if DEBUG >= 3
  fprintf(stderr, "Made a listening socket on %s:%i...\n", interface.c_str(), listen_port);
  #endif
  if (server_socket.connected()){
    if (daemon_mode){
      daemon(1, 0);
      #if DEBUG >= 3
      fprintf(stderr, "Going into background mode...\n");
      #endif
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Error: could not make listening socket\n");
    #endif
    return 1;
  }
  int status;
  while (server_socket.connected()){
    waitpid((pid_t)-1, &status, WNOHANG);
    CONN_fd = server_socket.accept();
    if (CONN_fd.connected()){
      pid_t myid = fork();
      if (myid == 0){
        break;
      }else{
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for socket %i\n", (int)myid, CONN_fd.getSocket());
        #endif
      }
    }
  }
  if (!server_socket.connected()){
    return 0;
  }
  return MAINHANDLER(CONN_fd);
}
