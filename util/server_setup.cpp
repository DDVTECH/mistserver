int mainHandler(int CONN_fd);//define this function in your own code!
#include <signal.h>
#include "ddv_socket.cpp" //DDVTech Socket wrapper
#include "flv_sock.cpp" //FLV parsing with DDVTech Socket wrapper
int server_socket = 0;

void termination_handler (int signum){
  if (server_socket == 0) return;
  switch (signum){
    case SIGINT: break;
    case SIGHUP: break;
    case SIGTERM: break;
    default: return; break;
  }
  close(server_socket);
  server_socket = 0;
}

int main(int argc, char ** argv){
  int CONN_fd = 0;
  
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
  
  int opt = 0;
  static const char *optString = "np:h?";
  static const struct option longOpts[] = {
    {"help",0,0,'h'},
    {"port",1,0,'p'},
    {"no-daemon",0,0,'n'}
  };
  while ((opt = getopt_long(argc, argv, optString, longOpts, 0)) != -1){
    switch (opt){
      case 'p':
        listen_port = atoi(optarg);
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
  
  server_socket = DDV_Listen(listen_port);
  #if DEBUG >= 3
  fprintf(stderr, "Made a listening socket on port %i...\n", listen_port);
  #endif
  if (server_socket > 0){
    if (daemon_mode){
      daemon(1, 0);
      #if DEBUG >= 3
      fprintf(stderr, "Going into background mode...");
      #endif
    }
  }else{
    #if DEBUG >= 1
    fprintf(stderr, "Error: could not make listening socket");
    #endif
    return 1;
  }
  int status;
  while (server_socket > 0){
    waitpid((pid_t)-1, &status, WNOHANG);
    CONN_fd = DDV_Accept(server_socket);
    if (CONN_fd > 0){
      pid_t myid = fork();
      if (myid == 0){
        break;
      }else{
        #if DEBUG >= 3
        fprintf(stderr, "Spawned new process %i for handling socket %i\n", (int)myid, CONN_fd);
        #endif
      }
    }
  }
  if (server_socket <= 0){
    return 0;
  }
  return mainHandler(CONN_fd);
}
