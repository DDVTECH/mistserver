/// \file config.cpp
/// Contains generic functions for managing configuration.

#include "config.h"
#include "defines.h"
#include "stream.h"
#include "timing.h"
#include <thread>
#include <mutex>
#include <signal.h>
#include <string.h>
#include "procs.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h> //for getMyExec
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iostream>
#include <pwd.h>
#include <stdarg.h> // for va_list
#include <stdlib.h>

#ifdef __CYGWIN__
#include <windows.h>
#endif

#ifdef HASSYSWAIT
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

bool Util::Config::is_active = false;
bool Util::Config::is_restarting = false;
static int serv_sock_fd = -1;
uint32_t Util::printDebugLevel = DEBUG;
__thread char Util::streamName[256] = {0};
__thread char Util::exitReason[256] = {0};
__thread char* Util::mRExitReason = (char*)ER_UNKNOWN;
Util::binType Util::Config::binaryType = UNSET;

void Util::setStreamName(const std::string & sn){
  strncpy(Util::streamName, sn.c_str(), 256);
}

void Util::logExitReason(const char *shortString, const char *format, ...) {
  if (exitReason[0]) { return; }
  va_list args;
  va_start(args, format);
  vsnprintf(exitReason, 255, format, args);
  va_end(args);
  mRExitReason = (char *)shortString;

  if (!strncmp(mRExitReason, "CLEAN", 5)) {
    INFO_MSG("Logging clean exit reason: %s", exitReason);
  } else {
    FAIL_MSG("Logging unclean exit reason: %s", exitReason);
  }
}

#ifdef DISKSERIAL

#ifdef __CYGWIN__
static bool checkSerial(const std::string &ser, const std::string & directory = "/proc/registry/HKEY_LOCAL_MACHINE/HARDWARE/DEVICEMAP"){
  bool ret = false;
  struct stat statbuf;
  char serial[512];
  DIR *d = opendir(directory.c_str());
  if (!d){return false;}
  struct dirent *dp;
  do{
    errno = 0;
    if ((dp = readdir(d))){
      std::string newFile = directory + "/" + dp->d_name;
      if (dp->d_type == DT_DIR){
        ret |= checkSerial(ser, newFile);
        if (ret){break;}
        continue;
      }
      if (strncmp(dp->d_name, "SerialNumber", 12) == 0){
        if (!stat(newFile.c_str(), &statbuf)){
          FILE * fd = fopen(newFile.c_str(), "r");
          int len = fread(serial, 1, 512, fd);
          std::string currSer(serial, len);
          while (currSer.size() && (currSer[0] == ' ' || currSer[0] == 0)){currSer.erase(0, 1);}
          while (currSer.size() && (currSer[currSer.size()-1] == ' ' || currSer[currSer.size()-1] == 0)){currSer.erase(currSer.size()-1);}
          if (currSer == ser){ret = true;}
          fclose(fd);
        }
      }
      if (ret){break;}
    }
  }while (dp != NULL);
  closedir(d);
  return ret;
}
#else
static bool checkSerial(const std::string &ser){
  //INFO_MSG("Checking serial: %s", ser.c_str());
  bool ret = false;
  char serFile[300];
  struct stat statbuf;
  char serial[300];
  DIR *d = opendir("/sys/block");
  struct dirent *dp;
  if (d){
    do{
      errno = 0;
      if ((dp = readdir(d))){
        if (strncmp(dp->d_name, "loop", 4) != 0 && dp->d_name[0] != '.'){
          snprintf(serFile, 300, "/sys/block/%s/device/serial", dp->d_name);
          if (!stat(serFile, &statbuf)){
            FILE * fd = fopen(serFile, "r");
            int len = fread(serial, 1, 300, fd);
            if (len && len >= ser.size()){
              //INFO_MSG("Comparing with: %.*s", len, serial);
              if (!strncmp(ser.data(), serial, ser.size())){
                ret = true;
                fclose(fd);
                break;
              }
            }
            fclose(fd);
          }
          snprintf(serFile, 300, "/sys/block/%s/device/wwid", dp->d_name);
          if (!stat(serFile, &statbuf)){
            FILE * fd = fopen(serFile, "r");
            int len = fread(serial, 1, 300, fd);
            if (len && len >= ser.size()){
              std::string fullLine(serial, len);
              while (fullLine.size() && fullLine[fullLine.size()-1] < 33){fullLine.erase(fullLine.size()-1);}
              size_t lSpace = fullLine.rfind(' ');
              if (lSpace != std::string::npos){
                std::string curSer = fullLine.substr(lSpace+1);
                if (curSer.size() > ser.size()){curSer = curSer.substr(0, ser.size());}
                //INFO_MSG("Comparing with: %s", curSer.c_str());
                if (ser == curSer){
                  ret = true;
                  fclose(fd);
                  break;
                }
              }else{
                if (ser == fullLine){
                  ret = true;
                  fclose(fd);
                  break;
                }
              }
            }
            fclose(fd);
          }
        }
      }
    }while (dp != NULL);
    closedir(d);
  }
  if (ret){return true;}
  d = opendir("/dev/disk/by-id");
  if (d){
    do{
      errno = 0;
      if ((dp = readdir(d))){
        std::string fn = dp->d_name;
        if (fn.size() >= ser.size() && fn.substr(fn.size() - ser.size()) == ser){
          ret = true;
          break;
        }
      }
    }while (dp != NULL);
    closedir(d);
  }
  return ret;
}
#endif
#endif

void Util::Config::wipeShm(){
  DIR *d = opendir("/dev/shm");
  char fileName[300];
  struct dirent *dp;
  uint64_t deleted = 0;
  if (d){
    do{
      errno = 0;
      if ((dp = readdir(d))){
        if (strstr(dp->d_name, "Mst")){
          snprintf(fileName, sizeof(fileName), "/dev/shm/%s", dp->d_name);
          unlink(fileName);
          ++deleted;
        }
      }
    }while (dp != NULL);
    closedir(d);
  }
  if (deleted){WARN_MSG("Wiped %" PRIu64 " shared memory file(s)", deleted);}
}

Util::Config::Config(){
  // global options here
  vals["debug"]["long"] = "debug";
  vals["debug"]["short"] = "g";
  vals["debug"]["arg"] = "integer";
  vals["debug"]["help"] = "The debug level at which messages need to be printed.";
  vals["debug"]["value"].append((int64_t)DEBUG);
}

/// Creates a new configuration manager.
Util::Config::Config(std::string cmd){
  vals.null();
  long_count = 2;
  vals["cmd"]["value"].append(cmd);
  vals["version"]["long"] = "version";
  vals["version"]["short"] = "v";
  vals["version"]["help"] = "Display library and application version, then exit.";
  vals["help"]["long"] = "help";
  vals["help"]["short"] = "h";
  vals["help"]["help"] = "Display usage and version information, then exit.";
  vals["debug"]["long"] = "debug";
  vals["debug"]["short"] = "g";
  vals["debug"]["arg"] = "integer";
  vals["debug"]["help"] = "The debug level at which messages need to be printed.";
  vals["debug"]["value"].append((int64_t)DEBUG);
}

/// Adds an option to the configuration parser.
/// The option needs an unique name (doubles will overwrite the previous) and can contain the
/// following in the option itself:
///\code
///{
///   "short":"o",          //The short option letter
///   "long":"onName",      //The long option
///   "arg":"integer",      //The type of argument, if required.
///   "value":[],           //The default value(s) for this option if it is not given on the
///   commandline. "arg_num":1,          //The count this value has on the commandline, after all
///   the options have been processed. "help":"Blahblahblah" //The helptext for this option.
///}
///\endcode
void Util::Config::addOption(const std::string & optname, const JSON::Value & option) {
  JSON::Value & O = vals[optname];
  O = option;
  if (!O.isMember("value") && O.isMember("default")) {
    O["value"].append(O["default"]);
    O.removeMember("default");
  }
  if (O.isMember("value") && O["value"].isArray() && O["value"].size()) { O["has_default"] = true; }
  long_count = 0;
  jsonForEach(vals, it){
    if (it->isMember("long")){long_count++;}
  }
}

void Util::Config::addOption(const std::string & optname, const char *jsonStr) {
  addOption(optname, JSON::fromString(jsonStr));
}

/// Prints a usage message to the given output.
void Util::Config::printHelp(std::ostream &output){
  unsigned int longest = 0;
  std::map<long long int, std::string> args;
  jsonForEach(vals, it){
    unsigned int current = 0;
    if (it->isMember("long")){current += (*it)["long"].asString().size() + 4;}
    if (it->isMember("short")){current += (*it)["short"].asString().size() + 3;}
    if (current > longest){longest = current;}
    current = 0;
    if (current > longest){longest = current;}
    if (it->isMember("arg_num")){
      current = it.key().size() + 3;
      if (current > longest){longest = current;}
      args[(*it)["arg_num"].asInt()] = it.key();
    }
  }
  output << "Usage: " << getString("cmd") << " [options]";
  for (std::map<long long int, std::string>::iterator i = args.begin(); i != args.end(); i++){
    if (vals[i->second].isMember("value") && vals[i->second]["value"].size()){
      output << " [" << i->second << "]";
    }else{
      output << " " << i->second;
    }
  }
  output << std::endl << std::endl;
  jsonForEach(vals, it){
    std::string f;
    if (it->isMember("long") || it->isMember("short")){
      if (it->isMember("long") && it->isMember("short")){
        f = "--" + (*it)["long"].asString() + ", -" + (*it)["short"].asString();
      }else{
        if (it->isMember("long")){f = "--" + (*it)["long"].asString();}
        if (it->isMember("short")){f = "-" + (*it)["short"].asString();}
      }
      while (f.size() < longest){f.append(" ");}
      if (it->isMember("arg")){
        output << f << "(" << (*it)["arg"].asString() << ") " << (*it)["help"].asString() << std::endl;
      }else{
        output << f << (*it)["help"].asString() << std::endl;
      }
    }
    if (it->isMember("arg_num")){
      f = it.key();
      while (f.size() < longest){f.append(" ");}
      output << f << "(" << (*it)["arg"].asString() << ") " << (*it)["help"].asString() << std::endl;
    }
  }
}

/// Parses commandline arguments.
/// Calls exit if an unknown option is encountered, printing a help message.
bool Util::Config::parseArgs(int &argc, char **&argv){
  int opt = 0;
  std::string shortopts;
  struct option *longOpts = (struct option *)calloc(long_count + 1, sizeof(struct option));
  int long_i = 0;
  int arg_count = 0;
  if (vals.size()){
    jsonForEach(vals, it){
      if (it->isMember("short")){
        shortopts += (*it)["short"].asString();
        if (it->isMember("arg")){shortopts += ":";}
      }
      if (it->isMember("long")){
        longOpts[long_i].name = (*it)["long"].asStringRef().c_str();
        longOpts[long_i].val = (*it)["short"].asString()[0];
        if (it->isMember("arg")){longOpts[long_i].has_arg = 1;}
        long_i++;
      }
      if (it->isMember("arg_num") && !(it->isMember("value") && (*it)["value"].size())){
        if ((*it)["arg_num"].asInt() > arg_count){arg_count = (*it)["arg_num"].asInt();}
      }
    }
  }

  while ((opt = getopt_long(argc, argv, shortopts.c_str(), longOpts, 0)) != -1){
    switch (opt){
    case 'h':
    case '?': printHelp(std::cout);
    case 'v': std::cout << "Version: " PACKAGE_VERSION ", release " RELEASE << std::endl;
#ifndef SHM_ENABLED
      std::cout << "- Flag: Shared memory disabled. Will use shared files in stead of shared "
                   "memory as IPC method."
                << std::endl;
#endif
#ifdef WITH_THREADNAMES
      std::cout << "- Flag: With threadnames. Debuggers will show sensible human-readable thread "
                   "names."
                << std::endl;
#endif
#ifdef STAT_CUTOFF
      if (STAT_CUTOFF != 600){
        std::cout << "- Setting: Stats cutoff point "
                  << STAT_CUTOFF << " seconds. Statistics and session cache are only kept for this long, as opposed to the default of 600 seconds."
                  << std::endl;
      }
#endif
#ifndef SSL
      std::cout << "- Flag: SSL support disabled. HTTPS/RTMPS/WebRTC/WebSockets are either unavailable or may not function fully." << std::endl;
#endif
/*LTS-START*/
#ifndef UPDATER
      std::cout << "- Flag: Updater disabled. Server will not call back home and attempt to search "
                   "for updates at regular intervals."
                << std::endl;
#endif
#ifdef NOAUTH
      std::cout << "- Flag: No authentication. API calls do not require logging in with a valid "
                   "account first. Make sure access to API port isn't public!"
                << std::endl;
#endif
#ifdef STATS_DELAY
      if (STATS_DELAY != 15){
        std::cout << "- Setting: Stats delay " << STATS_DELAY << ". Statistics of viewer counts are delayed by "
                  << STATS_DELAY << " seconds as opposed to the default of 15 seconds. ";
        if (STATS_DELAY > 15){
          std::cout << "This makes them more accurate." << std::endl;
        }else{
          std::cout << "This makes them less accurate." << std::endl;
        }
      }
#endif
      /*LTS-END*/
      std::cout << "Built on " __DATE__ ", " __TIME__ << std::endl;
      exit(0);
      break;
    default:
      jsonForEach(vals, it){
        if (it->isMember("short") && (*it)["short"].asString()[0] == opt){
          if (it->isMember("arg")){
            (*it)["value"].append(optarg);
          }else{
            (*it)["value"].append((int64_t)1);
          }
          break;
        }
      }
      break;
    }
  }// commandline options parser
  free(longOpts);         // free the long options array
  long_i = 1;             // re-use long_i as an argument counter
  while (optind < argc){// parse all remaining options, ignoring anything unexpected.
    jsonForEach(vals, it){
      if (it->isMember("arg_num") && (*it)["arg_num"].asInt() == long_i){
        (*it)["value"].append((std::string)argv[optind]);
        break;
      }
    }
    optind++;
    long_i++;
  }
  if (long_i <= arg_count){return false;}
  printDebugLevel = getInteger("debug");
  return true;
}

bool Util::Config::hasOption(const std::string &optname){
  return vals.isMember(optname);
}

/// Returns a reference to the current value of an option or default if none was set.
/// If the option does not exist, this exits the application with a return code of 37.
JSON::Value &Util::Config::getOption(std::string optname, bool asArray){
  if (!vals.isMember(optname)){
    std::cout << "Fatal error: a non-existent option '" << optname << "' was accessed." << std::endl;
    exit(37);
  }
  if (!vals[optname].isMember("value") || !vals[optname]["value"].isArray()){
    vals[optname]["value"].append(JSON::Value());
    vals[optname]["value"].shrink(0);
  }
  if (asArray){return vals[optname]["value"];}
  int n = vals[optname]["value"].size();
  if (!n){
    static JSON::Value empty = "";
    return empty;
  }
  return vals[optname]["value"][n - 1];
}

/// Returns the current value of an option or default if none was set as a string.
/// Calls getOption internally.
std::string Util::Config::getString(std::string optname){
  return getOption(optname).asString();
}

/// Returns the current value of an option or default if none was set as a long long int.
/// Calls getOption internally.
int64_t Util::Config::getInteger(std::string optname){
  return getOption(optname).asInt();
}

/// Returns the current value of an option or default if none was set as a bool.
/// Calls getOption internally.
bool Util::Config::getBool(std::string optname){
  return getOption(optname).asBool();
}

/// Reads out the current configuration and puts any options passed into the argument string.
/// Note: Only the last-set option is included for each option (no multi-select support)
/// Any argument that is unset or set to its default is not included.
void Util::Config::fillEffectiveArgs(std::deque<std::string> & args, bool longForm) {
  jsonForEachConst (vals, opt) {
    // Positional arguments, ignore
    if (opt->isMember("arg_num")) { continue; }
    if (!opt->isMember("value")) { continue; }
    const JSON::Value & v = (*opt)["value"];
    if (!v.isArray() || !v.size()) { continue; }
    if (opt->isMember("arg")) {
      // Arguments with a value
      if (opt->isMember("has_default") && (v.size() < 2 || v[v.size() - 1] == v[0u])) { continue; }
    } else {
      // Arguments that are toggles
      if (!v[v.size() - 1]) { continue; }
    }
    if (opt->isMember("long") && (longForm || !opt->isMember("short"))) {
      args.push_back("--" + (*opt)["long"].asStringRef());
    } else if (opt->isMember("short")) {
      args.push_back("-" + (*opt)["short"].asStringRef());
    } else {
      continue;
    }
    if (opt->isMember("arg")) { args.push_back(v[v.size() - 1].asString()); }
  }
}

struct callbackData{
  Socket::Connection *sock;
  int (*cb)(Socket::Connection &);
};

/// Sets up the passed Socket::Server to listen on the configured address,
/// re-uses file descriptor 0 if it is a socket instead,
/// dups it to file descriptor 0 (if not zero already),
/// sets boundServer address and adds the listening socket to the socket list.
/// Returns true on success and false on failure.
bool Util::Config::setupServerSocket(Socket::Server & s) {
  if (Socket::checkTrueSocket(0)){
    s = Socket::Server(0);
  }else if (vals.isMember("socket")){
    s = Socket::Server(Util::getTmpFolder() + getString("socket"));
  }else if (vals.isMember("port") && vals.isMember("interface")){
    s = Socket::Server(getInteger("port"), getString("interface"), false);
  }
  if (!s.connected()) {
    FAIL_MSG("Failed to open listening socket");
    return false;
  }
  serv_sock_fd = s.getSocket();
  activate();
  if (s.getSocket()) {
    int oldSock = s.getSocket();
    if (!dup2(oldSock, 0)){
      s = Socket::Server(0);
      close(oldSock);
    }
  }
  Util::Procs::socketList.insert(s.getSocket());
  boundServer = s.getBoundAddr();
  return true;
}

bool Util::Config::serveThreadedSocket(int (*callback)(Socket::Connection &)) {
  Socket::Server server_socket;
  if (!setupServerSocket(server_socket)) { return false; }
  while (is_active && server_socket.connected()) {
    Socket::Connection S = server_socket.accept();
    if (S.connected()) { // check if the new connection is valid
      Socket::Connection *sock = new Socket::Connection(S);
      // spawn a new thread for this connection
      std::thread T([](Socket::Connection *sock, int (*callback)(Socket::Connection &)) {
        callback(*sock);
        sock->close();
        delete sock;
      }, sock, callback);
      // detach it, no need to keep track of it anymore
      T.detach();
      HIGH_MSG("Spawned new thread for socket %i", S.getSocket());
    } else {
      Util::sleep(10); // sleep 10ms
    }
  }
  Util::Procs::socketList.erase(server_socket.getSocket());
  server_socket.close();
  serv_sock_fd = -1;
  return true;
}

bool Util::Config::serveCallbackSocket(std::function<void(Socket::Connection &, Socket::Server &)> callback) {
  Socket::Server server_socket;
  if (!setupServerSocket(server_socket)) { return false; }
  while (is_active && server_socket.connected()) {
    Socket::Connection S = server_socket.accept();
    if (S.connected()) { // check if the new connection is valid
      callback(S, server_socket);
    } else {
      Util::sleep(10); // sleep 10ms
    }
  }
  Util::Procs::socketList.erase(server_socket.getSocket());
  if (!is_restarting) { server_socket.close(); }
  serv_sock_fd = -1;
  return true;
}

/// Activated the stored config. This will:
/// - Drop permissions to the stored "username", if any.
/// - Set is_active to true.
/// - Set up a signal handler to set is_active to false for the SIGINT, SIGHUP and SIGTERM signals.
void Util::Config::activate(){
#ifdef DISKSERIAL
  if (!checkSerial(DISKSERIAL)){
    ERROR_MSG("Not licensed");
    exit(1);
    return;
  }
#endif
  if (vals.isMember("username")){
    setUser(getString("username"));
    vals.removeMember("username");
  }
  struct sigaction new_action;
  struct sigaction cur_action;
  new_action.sa_sigaction = signal_handler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = SA_SIGINFO;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);
  // check if a child signal handler isn't set already, ignore child processes explicitly if unset
  sigaction(SIGCHLD, 0, &cur_action);
  if (cur_action.sa_handler == SIG_DFL || cur_action.sa_handler == SIG_IGN){
    new_action.sa_handler = SIG_IGN;
    sigaction(SIGCHLD, &new_action, NULL);
  }
  is_active = true;
}

std::mutex * mutabort = 0;
void Util::Config::setMutexAborter(void * mutex){
  mutabort = (std::mutex*)mutex;
}

void Util::Config::setServerFD(int fd){
  serv_sock_fd = fd;
}

void Util::Config::installDefaultChildSignalHandler(){
  struct sigaction new_action;
  new_action.sa_sigaction = signal_handler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = SA_SIGINFO;
  sigaction(SIGCHLD, &new_action, NULL);
}

/// Basic signal handler. Sets is_active to false if it receives
/// a SIGINT, SIGHUP or SIGTERM signal, reaps children for the SIGCHLD
/// signal, and ignores all other signals.
void Util::Config::signal_handler(int signum, siginfo_t *sigInfo, void *ignore){
  switch (signum){
    case SIGINT: // these three signals will set is_active to false.
    case SIGTERM:
      if (!mutabort || mutabort->try_lock()) {
        if (serv_sock_fd != -1) { close(serv_sock_fd); }
        if (mutabort) { mutabort->unlock(); }
      }
    case SIGHUP:
#if DEBUG >= DLVL_DEVEL
    static int ctr = 0;
    if (!is_active && ++ctr > 4){BACKTRACE;}
#endif
    switch (sigInfo->si_code){
    case SI_USER:
    case SI_QUEUE:
    case SI_TIMER:
    case SI_ASYNCIO:
    case SI_MESGQ:
      logExitReason(ER_CLEAN_SIGNAL, "signal %s (%d) from process %d", strsignal(signum), signum, sigInfo->si_pid);
      break;
    default: logExitReason(ER_CLEAN_SIGNAL, "signal %s (%d)", strsignal(signum), signum);
    }
    is_active = false;
  default:
    switch (sigInfo->si_code){
    case SI_USER:
    case SI_QUEUE:
    case SI_TIMER:
    case SI_ASYNCIO:
    case SI_MESGQ:
      INFO_MSG("Received signal %s (%d) from process %d", strsignal(signum), signum, sigInfo->si_pid);
      break;
    default: INFO_MSG("Received signal %s (%d)", strsignal(signum), signum); break;
    }
    break;
  case SIGCHLD:{// when a child dies, reap it.
    int status;
    pid_t ret = -1;
    while (ret != 0){
      ret = waitpid(-1, &status, WNOHANG);
      if (ret < 0 && errno != EINTR){break;}
    }
    HIGH_MSG("Received signal %s (%d) from process %d", strsignal(signum), signum, sigInfo->si_pid);
    break;
  }
  case SIGPIPE:
    // We ignore SIGPIPE to prevent messages triggering another SIGPIPE.
    // Loops are bad, m'kay?
    break;
  case SIGFPE: break;
  }
}// signal_handler

/// Adds the options from the given JSON capabilities structure.
/// Recurses into optional and required, added options as needed.
void Util::Config::addOptionsFromCapabilities(const JSON::Value &capa){
  // First add the required options.
  if (capa.isMember("required") && capa["required"].size()){
    jsonForEachConst(capa["required"], it){
      if (!it->isMember("short") || !it->isMember("option") || !it->isMember("type")){
        FAIL_MSG("Incomplete required option: %s", it.key().c_str());
        continue;
      }
      JSON::Value opt;
      opt["short"] = (*it)["short"];
      opt["long"] = (*it)["option"].asStringRef().substr(2);
      if (it->isMember("type")){
        // int, uint, debug, select, str
        if ((*it)["type"].asStringRef() == "int" || (*it)["type"].asStringRef() == "uint"){
          opt["arg"] = "integer";
        }else{
          opt["arg"] = "string";
        }
      }
      if (it->isMember("default")){opt["value"].append((*it)["default"]);}
      opt["help"] = (*it)["help"];
      addOption(it.key(), opt);
    }
  }
  // Then, the optionals.
  if (capa.isMember("optional") && capa["optional"].size()){
    jsonForEachConst(capa["optional"], it){
      if (it.key() == "debug"){continue;}
      if (!it->isMember("short") || !it->isMember("option") || !it->isMember("default")){
        FAIL_MSG("Incomplete optional option: %s", it.key().c_str());
        continue;
      }
      JSON::Value opt;
      opt["short"] = (*it)["short"];
      opt["long"] = (*it)["option"].asStringRef().substr(2);
      if (it->isMember("type")){
        // int, uint, debug, select, str
        if ((*it)["type"].asStringRef() == "int" || (*it)["type"].asStringRef() == "uint"){
          opt["arg"] = "integer";
        }else{
          opt["arg"] = "string";
        }
      }
      if (it->isMember("default")){opt["value"].append((*it)["default"]);}
      opt["help"] = (*it)["help"];
      addOption(it.key(), opt);
    }
  }
}

/// Adds the default connector options. Also updates the capabilities structure with the default
/// options. Besides the options addBasicConnectorOptions adds, this function also adds port and
/// interface options.
void Util::Config::addConnectorOptions(int port, JSON::Value &capabilities){
  capabilities["optional"]["port"]["name"] = "TCP port";
  capabilities["optional"]["port"]["help"] = "TCP port to listen on";
  capabilities["optional"]["port"]["type"] = "uint";
  capabilities["optional"]["port"]["short"] = "p";
  capabilities["optional"]["port"]["option"] = "--port";
  capabilities["optional"]["port"]["default"] = (int64_t)port;

  capabilities["optional"]["interface"]["name"] = "Interface";
  capabilities["optional"]["interface"]["help"] = "Address of the interface to listen on";
  capabilities["optional"]["interface"]["default"] = "0.0.0.0";
  capabilities["optional"]["interface"]["option"] = "--interface";
  capabilities["optional"]["interface"]["short"] = "i";
  capabilities["optional"]["interface"]["type"] = "str";

  addBasicConnectorOptions(capabilities);
}// addConnectorOptions

/// Adds the default connector options. Also updates the capabilities structure with the default
/// options.
void Util::Config::addBasicConnectorOptions(JSON::Value &capabilities){
  capabilities["optional"]["username"]["name"] = "Username";
  capabilities["optional"]["username"]["help"] =
      "Username to drop privileges to - default if unprovided means do not drop privileges";
  capabilities["optional"]["username"]["option"] = "--username";
  capabilities["optional"]["username"]["short"] = "u";
  capabilities["optional"]["username"]["default"] = "root";
  capabilities["optional"]["username"]["type"] = "str";

  addOptionsFromCapabilities(capabilities);

  JSON::Value option;
  option["long"] = "json";
  option["short"] = "j";
  option["help"] = "Output connector info in JSON format, then exit.";
  option["value"].append((int64_t)0);
  addOption("json", option);
}

void Util::Config::addStandardPushCapabilities(JSON::Value &cap){
  //Set up fast access to the push_parameters
  JSON::Value & pp = cap["push_parameters"];

  pp["track_selectors"]["type"] = "group";
  pp["track_selectors"]["name"] = "Track selectors";
  pp["track_selectors"]["help"] = "Control which tracks are part of the output";
  pp["track_selectors"]["sort"] = "v";
  {
    JSON::Value & o = pp["track_selectors"]["options"];
    o["audio"]["name"] = "Audio track(s)";
    o["audio"]["help"] = "Override which audio tracks of the stream should be selected";
    o["audio"]["type"] = "string";
    o["audio"]["validate"][0u] = "track_selector";
    o["audio"]["sort"] = "aa";

    o["video"]["name"] = "Video track(s)";
    o["video"]["help"] = "Override which video tracks of the stream should be selected";
    o["video"]["type"] = "string";
    o["video"]["validate"][0u] = "track_selector";
    o["video"]["sort"] = "ab";

    o["subtitle"]["name"] = "Subtitle track(s)";
    o["subtitle"]["help"] = "Override which subtitle tracks of the stream should be selected";
    o["subtitle"]["type"] = "string";
    o["subtitle"]["validate"].append("track_selector");
    o["subtitle"]["sort"] = "ac";
  }

  pp["trackwait_opts"]["type"] = "group";
  pp["trackwait_opts"]["name"] = "Wait for tracks";
  pp["trackwait_opts"]["help"] = "Before starting, ensure the available tracks satisfy certain conditions";
  pp["trackwait_opts"]["sort"] = "w";
  {
    JSON::Value & o = pp["trackwait_opts"]["options"];
    o["waittrackcount"]["name"] = "Wait for tracks count";
    o["waittrackcount"]["help"] = "Wait for this number of tracks to be available";
    o["waittrackcount"]["type"] = "int";
    o["waittrackcount"]["default"] = 2;
    o["waittrackcount"]["sort"] = "bda";

    o["mintrackkeys"]["name"] = "Wait for track keyframe count";
    o["mintrackkeys"]["help"] = "Consider a track available if it has this many keyframes buffered";
    o["mintrackkeys"]["type"] = "int";
    o["mintrackkeys"]["default"] = 2;
    o["mintrackkeys"]["sort"] = "bdaa";

    o["mintrackms"]["name"] = "Wait for track duration";
    o["mintrackms"]["help"] = "Consider a track available if this much data is buffered in it";
    o["mintrackms"]["type"] = "int";
    o["mintrackms"]["default"] = 500;
    o["mintrackms"]["unit"] = "ms";
    o["mintrackms"]["sort"] = "bdab";

    o["maxtrackms"]["name"] = "Max buffer duration for track wait";
    o["maxtrackms"]["help"] = "Proceed immediately when this much data is buffered in any available track";
    o["maxtrackms"]["type"] = "int";
    o["maxtrackms"]["default"] = "20000";
    o["maxtrackms"]["unit"] = "ms";
    o["maxtrackms"]["sort"] = "bdac";

    o["maxwaittrackms"]["name"] = "Track wait timeout";
    o["maxwaittrackms"]["help"] = "Give up and proceed regardless after this many milliseconds of waiting";
    o["maxwaittrackms"]["type"] = "int";
    o["maxwaittrackms"]["default"] = "5000 (5s), or 120000 (2m) if waittrackcount is specifically set to a value";
    o["maxwaittrackms"]["unit"] = "ms";
    o["maxwaittrackms"]["sort"] = "bdad";
  }

  pp["time_opts"]["type"] = "group";
  pp["time_opts"]["name"] = "Timing options";
  pp["time_opts"]["help"] = "Control speed and the start/stop timing";
  pp["time_opts"]["sort"] = "x";
  {
    JSON::Value & o = pp["time_opts"]["options"];
    o["rate"]["name"] = "Playback rate";
    o["rate"]["help"] = "Multiplier for the playback speed rate, or 0 for unlimited";
    o["rate"]["type"] = "int";
    o["rate"]["default"] = "1";
    o["rate"]["sort"] = "ba";

    o["realtime"]["name"] = "Don't speed up output";
    o["realtime"]["help"] = "If set to any value, removes the rate override to unlimited normally applied to push outputs";
    o["realtime"]["type"] = "bool";
    o["realtime"]["format"] = "set_or_unset";
    o["realtime"]["sort"] = "bb";

    o["pushdelay"]["name"] = "Push delay";
    o["pushdelay"]["help"] = "Ensures the stream is always delayed by at least this many seconds. Internally overrides the \"realtime\" and \"start\" parameters";
    o["pushdelay"]["type"] = "int";
    o["pushdelay"]["unit"] = "s";
    o["pushdelay"]["disable"].append("realtime");
    o["pushdelay"]["disable"].append("start");
    o["pushdelay"]["sort"] = "bg";

    o["duration"]["name"] = "Duration of push";
    o["duration"]["help"] = "How much media time to push, in seconds. Internally overrides the stop time(s).";
    o["duration"]["type"] = "int";
    o["duration"]["unit"] = "s";
    o["duration"]["disable"].append("stop");
    o["duration"]["disable"].append("stopunix");
    o["duration"]["sort"] = "bi";

    o["stop"]["name"] = "Media timestamp to stop at";
    o["stop"]["help"] = "Stop where the (internal) media time is at this timestamp";
    o["stop"]["type"] = "int";
    o["stop"]["unit"] = "s";
    o["stop"]["sort"] = "bk";

    o["start"]["name"] = "Media timestamp to start from";
    o["start"]["help"] = "Start where the (internal) media time is at this timestamp";
    o["start"]["type"] = "int";
    o["start"]["unit"] = "s";
    o["start"]["sort"] = "bl";

    o["stopunix"]["name"] = "Time to stop at";
    o["stopunix"]["help"] = "Stop where the stream timestamp reaches this point in time";
    o["stopunix"]["type"] = "unixtime";
    o["stopunix"]["unit"] = "s";
    o["stopunix"]["disable"].append("stop");
    o["stopunix"]["sort"] = "bm";

    o["startunix"]["name"] = "Time to start from";
    o["startunix"]["help"] = "Start where the stream timestamp is at this point in time";
    o["startunix"]["type"] = "unixtime";
    o["startunix"]["unit"] = "s";
    o["startunix"]["disable"].append("start");
    o["startunix"]["sort"] = "bn";

    o["split"]["name"] = "Split interval";
    o["split"]["help"] = "Performs a gapless restart of the recording every this many seconds. Always aligns to the next keyframe after this duration, to ensure each recording is fully playable. When set to zero (the default) will not split at all.";
    o["split"]["type"] = "int";
    o["split"]["unit"] = "s";
    o["split"]["sort"] = "bh";
  }

  pp["pls_opts"]["type"] = "group";
  pp["pls_opts"]["name"] = "Playlist writing options";
  pp["pls_opts"]["help"] = "Control the writing of a playlist file when recording to a segmented format";
  pp["pls_opts"]["sort"] = "y";
  {
    JSON::Value & o = pp["pls_opts"]["options"];
    o["noendlist"]["name"] = "Don't end playlist";
    o["noendlist"]["help"] = "If set, does not write #X-EXT-ENDLIST when finalizing the playlist on exit";
    o["noendlist"]["type"] = "bool";
    o["noendlist"]["format"] = "set_or_unset";
    o["noendlist"]["sort"] = "bfa";

    o["m3u8"]["name"] = "Playlist path (relative to segments)";
    o["m3u8"]["help"] = "If set, will write a m3u8 playlist file for the segments to the given path (relative from the first segment path). When this parameter is used, at least one of the variables $segmentCounter or $currentMediaTime must be part of the segment path (to keep segments from overwriting each other). The \"Split interval\" parameter will default to 60 seconds when using this option.";
    o["m3u8"]["type"] = "string";
    o["m3u8"]["sort"] = "apa";

    o["targetAge"]["name"] = "Playlist target age";
    o["targetAge"]["help"] = "When writing a playlist, delete segment entries that are more than this many seconds old from the playlist (and, if possible, also delete said segments themselves). When set to 0 or left empty, does not delete.";
    o["targetAge"]["type"] = "int";
    o["targetAge"]["unit"] = "s";
    o["targetAge"]["sort"] = "apb";

    o["maxEntries"]["name"] = "Playlist max entries";
    o["maxEntries"]["help"] = "When writing a playlist, delete oldest segment entries once this entry count has been reached (and, if possible, also delete said segments themselves). When set to 0 or left empty, does not delete.";
    o["maxEntries"]["type"] = "int";
    o["maxEntries"]["sort"] = "apc";
  }



  pp["misc_genopts"]["type"] = "group";
  pp["misc_genopts"]["name"] = "Miscellaneous options";
  pp["misc_genopts"]["sort"] = "z";
  {
    JSON::Value & o = pp["misc_genopts"]["options"];

    o["unmask"]["name"] = "Unmask tracks";
    o["unmask"]["help"] = "If set to any value, removes any applied track masking before selecting tracks, acting as if no mask was applied at all";
    o["unmask"]["type"] = "bool";
    o["unmask"]["format"] = "set_or_unset";
    o["unmask"]["sort"] = "bc";

    o["append"]["name"] = "Append to file";
    o["append"]["help"] = "If set to any value, will (if possible) append to an existing file, rather than overwriting it";
    o["append"]["type"] = "bool";
    o["append"]["format"] = "set_or_unset";
    o["append"]["sort"] = "bf";
  }

}

/// Gets the directory and name of the binary the executable is stored in.
std::string Util::getMyPathWithBin() {
  char mypath[500];
#ifdef __CYGWIN__
  GetModuleFileName(0, mypath, 500);
#else
#ifdef __APPLE__
  memset(mypath, 0, 500);
  unsigned int refSize = 500;
  _NSGetExecutablePath(mypath, &refSize);
#else
  int ret = readlink("/proc/self/exe", mypath, 500);
  if (ret != -1){
    mypath[ret] = 0;
  }else{
    mypath[0] = 0;
  }
#endif
#endif
  return mypath;
}

/// Gets directory the current executable is stored in.
std::string Util::getMyPath() {
  std::string tPath = getMyPathWithBin();
  size_t slash = tPath.rfind('/');
  if (slash == std::string::npos){
    slash = tPath.rfind('\\');
    if (slash == std::string::npos){return "";}
  }
  tPath.resize(slash + 1);
  return tPath;
}

/// Gets all executables in getMyPath that start with "Mist".
void Util::getMyExec(std::deque<std::string> &execs){
  std::string path = Util::getMyPath();
#ifdef __CYGWIN__
  path += "\\Mist*";
  WIN32_FIND_DATA FindFileData;
  HANDLE hdl = FindFirstFile(path.c_str(), &FindFileData);
  while (hdl != INVALID_HANDLE_VALUE){
    if (!(FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)){
      execs.push_back(FindFileData.cFileName);
    }
    if (!FindNextFile(hdl, &FindFileData)){
      FindClose(hdl);
      hdl = INVALID_HANDLE_VALUE;
    }
  }
#else
  DIR *d = opendir(path.c_str());
  if (!d){return;}
  struct dirent *dp;
  do{
    errno = 0;
    if ((dp = readdir(d))){
      if (dp->d_type != DT_DIR && strncmp(dp->d_name, "Mist", 4) == 0){
        if (dp->d_type != DT_REG) {
          struct stat st = {};
          stat(dp->d_name, &st);
          if (!S_ISREG(st.st_mode))
            continue;
        }
        execs.push_back(dp->d_name);}
    }
  }while (dp != NULL);
  closedir(d);
#endif
}

/// Sets the current process' running user
void Util::setUser(std::string username){
  if (username != "root"){
    struct passwd *user_info = getpwnam(username.c_str());
    if (!user_info){
      ERROR_MSG("Error: could not setuid %s: could not get PID", username.c_str());
      return;
    }else{
      if (setuid(user_info->pw_uid) != 0){
        ERROR_MSG("Error: could not setuid %s: not allowed", username.c_str());
      }else{
        DEVEL_MSG("Change user to %s", username.c_str());
      }
    }
  }
}
