/// \file config.cpp
/// Contains generic functions for managing configuration.

#include "config.h"
#include "defines.h"
#include "timing.h"
#include "tinythread.h"
#include "stream.h"
#include <string.h>
#include <signal.h>

#ifdef __CYGWIN__
#include <windows.h>
#endif

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__)
#include <sys/wait.h>
#else
#include <wait.h>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#include <errno.h>
#include <iostream>
#include <signal.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <pwd.h>
#include <getopt.h>
#include <stdlib.h>
#include <fstream>
#include <dirent.h> //for getMyExec
#include "procs.h"

bool Util::Config::is_active = false;
static Socket::Server * serv_sock_pointer = 0;
unsigned int Util::Config::printDebugLevel = DEBUG;//

Util::Config::Config() {
  //global options here
  vals["debug"]["long"] = "debug";
  vals["debug"]["short"] = "g";
  vals["debug"]["arg"] = "integer";
  vals["debug"]["help"] = "The debug level at which messages need to be printed.";
  vals["debug"]["value"].append((long long)DEBUG);
}

/// Creates a new configuration manager.
Util::Config::Config(std::string cmd) {
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
  vals["debug"]["value"].append((long long)DEBUG);
}

/// Adds an option to the configuration parser.
/// The option needs an unique name (doubles will overwrite the previous) and can contain the following in the option itself:
///\code
/// {
///   "short":"o",          //The short option letter
///   "long":"onName",      //The long option
///   "arg":"integer",      //The type of argument, if required.
///   "value":[],           //The default value(s) for this option if it is not given on the commandline.
///   "arg_num":1,          //The count this value has on the commandline, after all the options have been processed.
///   "help":"Blahblahblah" //The helptext for this option.
/// }
///\endcode
void Util::Config::addOption(std::string optname, JSON::Value option) {
  vals[optname] = option;
  if (!vals[optname].isMember("value") && vals[optname].isMember("default")) {
    vals[optname]["value"].append(vals[optname]["default"]);
    vals[optname].removeMember("default");
  }
  long_count = 0;
  jsonForEach(vals, it) {
    if (it->isMember("long")) {
      long_count++;
    }
  }
}

/// Prints a usage message to the given output.
void Util::Config::printHelp(std::ostream & output) {
  unsigned int longest = 0;
  std::map<long long int, std::string> args;
  jsonForEach(vals, it) {
    unsigned int current = 0;
    if (it->isMember("long")) {
      current += (*it)["long"].asString().size() + 4;
    }
    if (it->isMember("short")) {
      current += (*it)["short"].asString().size() + 3;
    }
    if (current > longest) {
      longest = current;
    }
    current = 0;
    if (current > longest) {
      longest = current;
    }
    if (it->isMember("arg_num")) {
      current = it.key().size() + 3;
      if (current > longest) {
        longest = current;
      }
      args[(*it)["arg_num"].asInt()] = it.key();
    }
  }
  output << "Usage: " << getString("cmd") << " [options]";
  for (std::map<long long int, std::string>::iterator i = args.begin(); i != args.end(); i++) {
    if (vals[i->second].isMember("value") && vals[i->second]["value"].size()) {
      output << " [" << i->second << "]";
    } else {
      output << " " << i->second;
    }
  }
  output << std::endl << std::endl;
  jsonForEach(vals, it) {
    std::string f;
    if (it->isMember("long") || it->isMember("short")) {
      if (it->isMember("long") && it->isMember("short")) {
        f = "--" + (*it)["long"].asString() + ", -" + (*it)["short"].asString();
      } else {
        if (it->isMember("long")) {
          f = "--" + (*it)["long"].asString();
        }
        if (it->isMember("short")) {
          f = "-" + (*it)["short"].asString();
        }
      }
      while (f.size() < longest) {
        f.append(" ");
      }
      if (it->isMember("arg")) {
        output << f << "(" << (*it)["arg"].asString() << ") " << (*it)["help"].asString() << std::endl;
      } else {
        output << f << (*it)["help"].asString() << std::endl;
      }
    }
    if (it->isMember("arg_num")) {
      f = it.key();
      while (f.size() < longest) {
        f.append(" ");
      }
      output << f << "(" << (*it)["arg"].asString() << ") " << (*it)["help"].asString() << std::endl;
    }
  }
}

/// Parses commandline arguments.
/// Calls exit if an unknown option is encountered, printing a help message.
bool Util::Config::parseArgs(int & argc, char ** & argv) {
  int opt = 0;
  std::string shortopts;
  struct option * longOpts = (struct option *)calloc(long_count + 1, sizeof(struct option));
  int long_i = 0;
  int arg_count = 0;
  if (vals.size()) {
    jsonForEach(vals, it) {
      if (it->isMember("short")) {
        shortopts += (*it)["short"].asString();
        if (it->isMember("arg")) {
          shortopts += ":";
        }
      }
      if (it->isMember("long")) {
        longOpts[long_i].name = (*it)["long"].asStringRef().c_str();
        longOpts[long_i].val = (*it)["short"].asString()[0];
        if (it->isMember("arg")) {
          longOpts[long_i].has_arg = 1;
        }
        long_i++;
      }
      if (it->isMember("arg_num") && !(it->isMember("value") && (*it)["value"].size())) {
        if ((*it)["arg_num"].asInt() > arg_count) {
          arg_count = (*it)["arg_num"].asInt();
        }
      }
    }
  }
  
  while ((opt = getopt_long(argc, argv, shortopts.c_str(), longOpts, 0)) != -1) {
    switch (opt) {
      case 'h':
      case '?':
        printHelp(std::cout);
      case 'v':
        std::cout << "Version: " PACKAGE_VERSION ", release " RELEASE << std::endl;
        #ifdef NOCRASHCHECK
        std::cout << "- Flag: No crash check. Will not attempt to detect and kill crashed processes." << std::endl;
        #endif
        #ifndef SHM_ENABLED
        std::cout << "- Flag: Shared memory disabled. Will use shared files in stead of shared memory as IPC method." << std::endl;
        #endif
        #ifdef WITH_THREADNAMES
        std::cout << "- Flag: With threadnames. Debuggers will show sensible human-readable thread names." << std::endl;
        #endif
        std::cout << "Built on " __DATE__ ", " __TIME__ << std::endl;
        exit(0);
        break;
      default:
        jsonForEach(vals, it) {
          if (it->isMember("short") && (*it)["short"].asString()[0] == opt) {
            if (it->isMember("arg")) {
              (*it)["value"].append((std::string)optarg);
            } else {
              (*it)["value"].append((long long int)1);
            }
            break;
          }
        }
        break;
    }
  } //commandline options parser
  free(longOpts); //free the long options array
  long_i = 1; //re-use long_i as an argument counter
  while (optind < argc) { //parse all remaining options, ignoring anything unexpected.
    jsonForEach(vals, it) {
      if (it->isMember("arg_num") && (*it)["arg_num"].asInt() == long_i) {
        (*it)["value"].append((std::string)argv[optind]);
        break;
      }
    }
    optind++;
    long_i++;
  }
  if (long_i <= arg_count) {
    return false;
  }
  printDebugLevel = getInteger("debug");
  return true;
}

bool Util::Config::hasOption(const std::string & optname) {
  return vals.isMember(optname);
}

/// Returns a reference to the current value of an option or default if none was set.
/// If the option does not exist, this exits the application with a return code of 37.
JSON::Value & Util::Config::getOption(std::string optname, bool asArray) {
  if (!vals.isMember(optname)) {
    std::cout << "Fatal error: a non-existent option '" << optname << "' was accessed." << std::endl;
    exit(37);
  }
  if (!vals[optname].isMember("value") || !vals[optname]["value"].isArray()) {
    vals[optname]["value"].append(JSON::Value());
    vals[optname]["value"].shrink(0);
  }
  if (asArray) {
    return vals[optname]["value"];
  } else {
    int n = vals[optname]["value"].size();
    if (!n){
      static JSON::Value empty = "";
      return empty;
    }else{
      return vals[optname]["value"][n - 1];
    }
  }
}

/// Returns the current value of an option or default if none was set as a string.
/// Calls getOption internally.
std::string Util::Config::getString(std::string optname) {
  return getOption(optname).asString();
}

/// Returns the current value of an option or default if none was set as a long long int.
/// Calls getOption internally.
long long int Util::Config::getInteger(std::string optname) {
  return getOption(optname).asInt();
}

/// Returns the current value of an option or default if none was set as a bool.
/// Calls getOption internally.
bool Util::Config::getBool(std::string optname) {
  return getOption(optname).asBool();
}

struct callbackData {
  Socket::Connection * sock;
  int (*cb)(Socket::Connection &);
};

static void callThreadCallback(void * cDataArg) {
  DEBUG_MSG(DLVL_INSANE, "Thread for %p started", cDataArg);
  callbackData * cData = (callbackData *)cDataArg;
  cData->cb(*(cData->sock));
  cData->sock->close();
  delete cData->sock;
  delete cData;
  DEBUG_MSG(DLVL_INSANE, "Thread for %p ended", cDataArg);
}

int Util::Config::threadServer(Socket::Server & server_socket, int (*callback)(Socket::Connection &)) {
  Util::Procs::socketList.insert(server_socket.getSocket());
  while (is_active && server_socket.connected()) {
    Socket::Connection S = server_socket.accept();
    if (S.connected()) { //check if the new connection is valid
      callbackData * cData = new callbackData;
      cData->sock = new Socket::Connection(S);
      cData->cb = callback;
      //spawn a new thread for this connection
      tthread::thread T(callThreadCallback, (void *)cData);
      //detach it, no need to keep track of it anymore
      T.detach();
      DEBUG_MSG(DLVL_HIGH, "Spawned new thread for socket %i", S.getSocket());
    } else {
      Util::sleep(10); //sleep 10ms
    }
  }
  Util::Procs::socketList.erase(server_socket.getSocket());
  server_socket.close();
  return 0;
}

int Util::Config::forkServer(Socket::Server & server_socket, int (*callback)(Socket::Connection &)) {
  Util::Procs::socketList.insert(server_socket.getSocket());
  while (is_active && server_socket.connected()) {
    Socket::Connection S = server_socket.accept();
    if (S.connected()) { //check if the new connection is valid
      pid_t myid = fork();
      if (myid == 0) { //if new child, start MAINHANDLER
        server_socket.drop();
        return callback(S);
      } else { //otherwise, do nothing or output debugging text
        DEBUG_MSG(DLVL_HIGH, "Forked new process %i for socket %i", (int)myid, S.getSocket());
        S.drop();
      }
    } else {
      Util::sleep(10); //sleep 10ms
    }
  }
  Util::Procs::socketList.erase(server_socket.getSocket());
  server_socket.close();
  return 0;
}

int Util::Config::serveThreadedSocket(int (*callback)(Socket::Connection &)) {
  Socket::Server server_socket;
  if (vals.isMember("socket")) {
    server_socket = Socket::Server(Util::getTmpFolder() + getString("socket"));
  }
  if (vals.isMember("port") && vals.isMember("interface")) {
    server_socket = Socket::Server(getInteger("port"), getString("interface"), false);
  }
  if (!server_socket.connected()) {
    DEBUG_MSG(DLVL_DEVEL, "Failure to open socket");
    return 1;
  }
  serv_sock_pointer = &server_socket;
  DEBUG_MSG(DLVL_DEVEL, "Activating threaded server: %s", getString("cmd").c_str());
  activate();
  int r = threadServer(server_socket, callback);
  serv_sock_pointer = 0;
  return r;
}

int Util::Config::serveForkedSocket(int (*callback)(Socket::Connection & S)) {
  Socket::Server server_socket;
  if (vals.isMember("socket")) {
    server_socket = Socket::Server(Util::getTmpFolder() + getString("socket"));
  }
  if (vals.isMember("port") && vals.isMember("interface")) {
    server_socket = Socket::Server(getInteger("port"), getString("interface"), false);
  }
  if (!server_socket.connected()) {
    DEBUG_MSG(DLVL_DEVEL, "Failure to open socket");
    return 1;
  }
  serv_sock_pointer = &server_socket;
  DEBUG_MSG(DLVL_DEVEL, "Activating forked server: %s", getString("cmd").c_str());
  activate();
  int r = forkServer(server_socket, callback);
  serv_sock_pointer = 0;
  return r;
}

/// Activated the stored config. This will:
/// - Drop permissions to the stored "username", if any.
/// - Set is_active to true.
/// - Set up a signal handler to set is_active to false for the SIGINT, SIGHUP and SIGTERM signals.
void Util::Config::activate() {
  if (vals.isMember("username")) {
    setUser(getString("username"));
    vals.removeMember("username");
  }
  struct sigaction new_action;
  struct sigaction cur_action;
  new_action.sa_sigaction = signal_handler;
  sigemptyset(&new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);
  //check if a child signal handler isn't set already, if so, set it.
  sigaction(SIGCHLD, 0, &cur_action);
  if (cur_action.sa_handler == SIG_DFL || cur_action.sa_handler == SIG_IGN) {
    sigaction(SIGCHLD, &new_action, NULL);
  }
  is_active = true;
}

/// Basic signal handler. Sets is_active to false if it receives
/// a SIGINT, SIGHUP or SIGTERM signal, reaps children for the SIGCHLD
/// signal, and ignores all other signals.
void Util::Config::signal_handler(int signum, siginfo_t * sigInfo, void * ignore) {
  switch (signum) {
    case SIGINT: //these three signals will set is_active to false.
    case SIGHUP:
    case SIGTERM:
      if (serv_sock_pointer){serv_sock_pointer->close();}
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
        default:
          INFO_MSG("Received signal %s (%d)", strsignal(signum), signum);
          break;
      }
      break;
    case SIGCHLD: { //when a child dies, reap it.
        int status;
        pid_t ret = -1;
        while (ret != 0) {
          ret = waitpid(-1, &status, WNOHANG);
          if (ret < 0 && errno != EINTR) {
            break;
          }
        }
        HIGH_MSG("Received signal %s (%d) from process %d", strsignal(signum), signum, sigInfo->si_pid);
        break;
      }
    case SIGPIPE:
      //We ignore SIGPIPE to prevent messages triggering another SIGPIPE.
      //Loops are bad, m'kay?
      break;
  }
} //signal_handler


/// Adds the options from the given JSON capabilities structure.
/// Recurses into optional and required, added options as needed.
void Util::Config::addOptionsFromCapabilities(const JSON::Value & capa){
  //First add the required options.
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
        //int, uint, debug, select, str
        if ((*it)["type"].asStringRef() == "int" || (*it)["type"].asStringRef() == "uint"){
          opt["arg"] = "integer";
        }else{
          opt["arg"] = "string";
        }
      }
      if (it->isMember("default")){
        opt["value"].append((*it)["default"]);
      }
      opt["help"] = (*it)["help"];
      addOption(it.key(), opt);
    }
  }
  //Then, the optionals.
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
        //int, uint, debug, select, str
        if ((*it)["type"].asStringRef() == "int" || (*it)["type"].asStringRef() == "uint"){
          opt["arg"] = "integer";
        }else{
          opt["arg"] = "string";
        }
      }
      if (it->isMember("default")){
        opt["value"].append((*it)["default"]);
      }
      opt["help"] = (*it)["help"];
      addOption(it.key(), opt);
    }
  }
}

/// Adds the default connector options. Also updates the capabilities structure with the default options.
/// Besides the options addBasicConnectorOptions adds, this function also adds port and interface options.
void Util::Config::addConnectorOptions(int port, JSON::Value & capabilities) {
  capabilities["optional"]["port"]["name"] = "TCP port";
  capabilities["optional"]["port"]["help"] = "TCP port to listen on";
  capabilities["optional"]["port"]["type"] = "uint";
  capabilities["optional"]["port"]["short"] = "p";
  capabilities["optional"]["port"]["option"] = "--port";
  capabilities["optional"]["port"]["default"] = (long long)port;

  capabilities["optional"]["interface"]["name"] = "Interface";
  capabilities["optional"]["interface"]["help"] = "Address of the interface to listen on";
  capabilities["optional"]["interface"]["default"] = "0.0.0.0";
  capabilities["optional"]["interface"]["option"] = "--interface";
  capabilities["optional"]["interface"]["short"] = "i";
  capabilities["optional"]["interface"]["type"] = "str";

  addBasicConnectorOptions(capabilities);
} //addConnectorOptions

/// Adds the default connector options. Also updates the capabilities structure with the default options.
void Util::Config::addBasicConnectorOptions(JSON::Value & capabilities) {
  capabilities["optional"]["username"]["name"] = "Username";
  capabilities["optional"]["username"]["help"] = "Username to drop privileges to - default if unprovided means do not drop privileges";
  capabilities["optional"]["username"]["option"] = "--username";
  capabilities["optional"]["username"]["short"] = "u";
  capabilities["optional"]["username"]["default"] = "root";
  capabilities["optional"]["username"]["type"] = "str";

  addOptionsFromCapabilities(capabilities);

  JSON::Value option;
  option["long"] = "json";
  option["short"] = "j";
  option["help"] = "Output connector info in JSON format, then exit.";
  option["value"].append(0ll);
  addOption("json", option);
}



/// Gets directory the current executable is stored in.
std::string Util::getMyPath() {
  char mypath[500];
#ifdef __CYGWIN__
  GetModuleFileName(0, mypath, 500);
#else
#ifdef __APPLE__
  memset(mypath, 0, 500);
  unsigned int refSize = 500;
  int ret = _NSGetExecutablePath(mypath, &refSize);
#else
  int ret = readlink("/proc/self/exe", mypath, 500);
  if (ret != -1) {
    mypath[ret] = 0;
  } else {
    mypath[0] = 0;
  }
#endif
#endif
  std::string tPath = mypath;
  size_t slash = tPath.rfind('/');
  if (slash == std::string::npos) {
    slash = tPath.rfind('\\');
    if (slash == std::string::npos) {
      return "";
    }
  }
  tPath.resize(slash + 1);
  return tPath;
}

/// Gets all executables in getMyPath that start with "Mist".
void Util::getMyExec(std::deque<std::string> & execs) {
  std::string path = Util::getMyPath();
#ifdef __CYGWIN__
  path += "\\Mist*";
  WIN32_FIND_DATA FindFileData;
  HANDLE hdl = FindFirstFile(path.c_str(), &FindFileData);
  while (hdl != INVALID_HANDLE_VALUE) {
    execs.push_back(FindFileData.cFileName);
    if (!FindNextFile(hdl, &FindFileData)) {
      FindClose(hdl);
      hdl = INVALID_HANDLE_VALUE;
    }
  }
#else
  DIR * d = opendir(path.c_str());
  if (!d) {
    return;
  }
  struct dirent * dp;
  do {
    errno = 0;
    if ((dp = readdir(d))) {
      if (strncmp(dp->d_name, "Mist", 4) == 0) {
        execs.push_back(dp->d_name);
      }
    }
  } while (dp != NULL);
  closedir(d);
#endif
}

/// Sets the current process' running user
void Util::setUser(std::string username) {
  if (username != "root") {
    struct passwd * user_info = getpwnam(username.c_str());
    if (!user_info) {
      DEBUG_MSG(DLVL_ERROR, "Error: could not setuid %s: could not get PID", username.c_str());
      return;
    } else {
      if (setuid(user_info->pw_uid) != 0) {
        DEBUG_MSG(DLVL_ERROR, "Error: could not setuid %s: not allowed", username.c_str());
      } else {
        DEBUG_MSG(DLVL_DEVEL, "Change user to %s", username.c_str());
      }
    }
  }
}

