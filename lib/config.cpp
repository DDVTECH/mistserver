/// \file config.cpp
/// Contains generic functions for managing configuration.

#include "config.h"
#include <string.h>
#include <signal.h>

#if defined(__FreeBSD__) || defined(__APPLE__) || defined(__MACH__)
#include <sys/wait.h>
#else
#include <wait.h>
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
#include <stdio.h>
#include <fstream>

bool Util::Config::is_active = false;
std::string Util::Config::libver = PACKAGE_VERSION;

/// Creates a new configuration manager.
Util::Config::Config(std::string cmd, std::string version){
  vals.null();
  long_count = 2;
  vals["cmd"]["value"].append(cmd);
  vals["version"]["long"] = "version";
  vals["version"]["short"] = "v";
  vals["version"]["help"] = "Display library and application version, then exit.";
  vals["help"]["long"] = "help";
  vals["help"]["short"] = "h";
  vals["help"]["help"] = "Display usage and version information, then exit.";
  vals["version"]["value"].append((std::string)PACKAGE_VERSION);
  vals["version"]["value"].append(version);
}

/// Adds an option to the configuration parser.
/// The option needs an unique name (doubles will overwrite the previous) and can contain the following in the option itself:
///\code
/// {
///   "short":"o",          //The short option letter
///   "long":"onName",      //The long option
///   "short_off":"n",      //The short option-off letter
///   "long_off":"offName", //The long option-off
///   "arg":"integer",      //The type of argument, if required.
///   "value":[],           //The default value(s) for this option if it is not given on the commandline.
///   "arg_num":1,          //The count this value has on the commandline, after all the options have been processed.
///   "help":"Blahblahblah" //The helptext for this option.
/// }
///\endcode
void Util::Config::addOption(std::string optname, JSON::Value option){
  vals[optname] = option;
  if ( !vals[optname].isMember("value") && vals[optname].isMember("default")){
    vals[optname]["value"].append(vals[optname]["default"]);
    vals[optname].removeMember("default");
  }
  long_count = 0;
  for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
    if (it->second.isMember("long")){
      long_count++;
    }
    if (it->second.isMember("long_off")){
      long_count++;
    }
  }
}

/// Prints a usage message to the given output.
void Util::Config::printHelp(std::ostream & output){
  unsigned int longest = 0;
  std::map<long long int, std::string> args;
  for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
    unsigned int current = 0;
    if (it->second.isMember("long")){
      current += it->second["long"].asString().size() + 4;
    }
    if (it->second.isMember("short")){
      current += it->second["short"].asString().size() + 3;
    }
    if (current > longest){
      longest = current;
    }
    current = 0;
    if (it->second.isMember("long_off")){
      current += it->second["long_off"].asString().size() + 4;
    }
    if (it->second.isMember("short_off")){
      current += it->second["short_off"].asString().size() + 3;
    }
    if (current > longest){
      longest = current;
    }
    if (it->second.isMember("arg_num")){
      current = it->first.size() + 3;
      if (current > longest){
        longest = current;
      }
      args[it->second["arg_num"].asInt()] = it->first;
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
  for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
    std::string f;
    if (it->second.isMember("long") || it->second.isMember("short")){
      if (it->second.isMember("long") && it->second.isMember("short")){
        f = "--" + it->second["long"].asString() + ", -" + it->second["short"].asString();
      }else{
        if (it->second.isMember("long")){
          f = "--" + it->second["long"].asString();
        }
        if (it->second.isMember("short")){
          f = "-" + it->second["short"].asString();
        }
      }
      while (f.size() < longest){
        f.append(" ");
      }
      if (it->second.isMember("arg")){
        output << f << "(" << it->second["arg"].asString() << ") " << it->second["help"].asString() << std::endl;
      }else{
        output << f << it->second["help"].asString() << std::endl;
      }
    }
    if (it->second.isMember("long_off") || it->second.isMember("short_off")){
      if (it->second.isMember("long_off") && it->second.isMember("short_off")){
        f = "--" + it->second["long_off"].asString() + ", -" + it->second["short_off"].asString();
      }else{
        if (it->second.isMember("long_off")){
          f = "--" + it->second["long_off"].asString();
        }
        if (it->second.isMember("short_off")){
          f = "-" + it->second["short_off"].asString();
        }
      }
      while (f.size() < longest){
        f.append(" ");
      }
      if (it->second.isMember("arg")){
        output << f << "(" << it->second["arg"].asString() << ") " << it->second["help"].asString() << std::endl;
      }else{
        output << f << it->second["help"].asString() << std::endl;
      }
    }
    if (it->second.isMember("arg_num")){
      f = it->first;
      while (f.size() < longest){
        f.append(" ");
      }
      output << f << "(" << it->second["arg"].asString() << ") " << it->second["help"].asString() << std::endl;
    }
  }
}

/// Parses commandline arguments.
/// Calls exit if an unknown option is encountered, printing a help message.
void Util::Config::parseArgs(int argc, char ** argv){
  int opt = 0;
  std::string shortopts;
  struct option * longOpts = (struct option*)calloc(long_count + 1, sizeof(struct option));
  int long_i = 0;
  int arg_count = 0;
  if (vals.size()){
    for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
      if (it->second.isMember("short")){
        shortopts += it->second["short"].asString();
        if (it->second.isMember("arg")){
          shortopts += ":";
        }
      }
      if (it->second.isMember("short_off")){
        shortopts += it->second["short_off"].asString();
        if (it->second.isMember("arg")){
          shortopts += ":";
        }
      }
      if (it->second.isMember("long")){
        longOpts[long_i].name = it->second["long"].asString().c_str();
        longOpts[long_i].val = it->second["short"].asString()[0];
        if (it->second.isMember("arg")){
          longOpts[long_i].has_arg = 1;
        }
        long_i++;
      }
      if (it->second.isMember("long_off")){
        longOpts[long_i].name = it->second["long_off"].asString().c_str();
        longOpts[long_i].val = it->second["short_off"].asString()[0];
        if (it->second.isMember("arg")){
          longOpts[long_i].has_arg = 1;
        }
        long_i++;
      }
      if (it->second.isMember("arg_num") && !(it->second.isMember("value") && it->second["value"].size())){
        if (it->second["arg_num"].asInt() > arg_count){
          arg_count = it->second["arg_num"].asInt();
        }
      }
    }
  }
  while ((opt = getopt_long(argc, argv, shortopts.c_str(), longOpts, 0)) != -1){
    switch (opt){
      case 'h':
      case '?':
        printHelp(std::cout);
      case 'v':
        std::cout << "Library version: " PACKAGE_VERSION << std::endl;
        std::cout << "Application version: " << getString("version") << std::endl;
        exit(1);
        break;
      default:
        for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
          if (it->second.isMember("short") && it->second["short"].asString()[0] == opt){
            if (it->second.isMember("arg")){
              it->second["value"].append((std::string)optarg);
            }else{
              it->second["value"].append((long long int)1);
            }
            break;
          }
          if (it->second.isMember("short_off") && it->second["short_off"].asString()[0] == opt){
            it->second["value"].append((long long int)0);
          }
        }
        break;
    }
  } //commandline options parser
  free(longOpts); //free the long options array
  long_i = 1; //re-use long_i as an argument counter
  while (optind < argc){ //parse all remaining options, ignoring anything unexpected.
    for (JSON::ObjIter it = vals.ObjBegin(); it != vals.ObjEnd(); it++){
      if (it->second.isMember("arg_num") && it->second["arg_num"].asInt() == long_i){
        it->second["value"].append((std::string)argv[optind]);
        optind++;
        long_i++;
        break;
      }
    }
  }
  if (long_i <= arg_count){
    std::cerr << "Usage error: missing argument(s)." << std::endl;
    printHelp(std::cout);
    exit(1);
  }
}

/// Returns a reference to the current value of an option or default if none was set.
/// If the option does not exist, this exits the application with a return code of 37.
JSON::Value & Util::Config::getOption(std::string optname, bool asArray){
  if ( !vals.isMember(optname)){
    std::cout << "Fatal error: a non-existent option '" << optname << "' was accessed." << std::endl;
    exit(37);
  }
  if ( !vals[optname].isMember("value") || !vals[optname]["value"].isArray()){
    vals[optname]["value"].append(JSON::Value());
  }
  if (asArray){
    return vals[optname]["value"];
  }else{
    int n = vals[optname]["value"].size();
    return vals[optname]["value"][n - 1];
  }
}

/// Returns the current value of an option or default if none was set as a string.
/// Calls getOption internally.
std::string Util::Config::getString(std::string optname){
  return getOption(optname).asString();
}

/// Returns the current value of an option or default if none was set as a long long int.
/// Calls getOption internally.
long long int Util::Config::getInteger(std::string optname){
  return getOption(optname).asInt();
}

/// Returns the current value of an option or default if none was set as a bool.
/// Calls getOption internally.
bool Util::Config::getBool(std::string optname){
  return getOption(optname).asBool();
}

/// Activated the stored config. This will:
/// - Drop permissions to the stored "username", if any.
/// - Daemonize the process if "daemonize" exists and is true.
/// - Set is_active to true.
/// - Set up a signal handler to set is_active to false for the SIGINT, SIGHUP and SIGTERM signals.
void Util::Config::activate(){
  if (vals.isMember("username")){
    setUser(getString("username"));
  }
  if (vals.isMember("daemonize") && getBool("daemonize")){
    Daemonize();
  }
  struct sigaction new_action;
  new_action.sa_handler = signal_handler;
  sigemptyset( &new_action.sa_mask);
  new_action.sa_flags = 0;
  sigaction(SIGINT, &new_action, NULL);
  sigaction(SIGHUP, &new_action, NULL);
  sigaction(SIGTERM, &new_action, NULL);
  sigaction(SIGPIPE, &new_action, NULL);
  sigaction(SIGCHLD, &new_action, NULL);
  is_active = true;
}

/// Basic signal handler. Sets is_active to false if it receives
/// a SIGINT, SIGHUP or SIGTERM signal, reaps children for the SIGCHLD
/// signal, and ignores all other signals.
void Util::Config::signal_handler(int signum){
  switch (signum){
    case SIGINT: //these three signals will set is_active to false.
    case SIGHUP:
    case SIGTERM:
      is_active = false;
      break;
    case SIGCHLD: //when a child dies, reap it.
      wait(0);
      break;
    default: //other signals are ignored
      break;
  }
} //signal_handler

/// Adds the default connector options to this Util::Config object.
void Util::Config::addConnectorOptions(int port){
  JSON::Value stored_port = JSON::fromString("{\"long\":\"port\", \"short\":\"p\", \"arg\":\"integer\", \"help\":\"TCP port to listen on.\"}");
  stored_port["value"].append((long long int)port);
  addOption("listen_port", stored_port);
  addOption("listen_interface",
      JSON::fromString(
          "{\"long\":\"interface\", \"value\":[\"0.0.0.0\"], \"short\":\"i\", \"arg\":\"string\", \"help\":\"Interface address to listen on, or 0.0.0.0 for all available interfaces.\"}"));
  addOption("username",
      JSON::fromString(
          "{\"long\":\"username\", \"value\":[\"root\"], \"short\":\"u\", \"arg\":\"string\", \"help\":\"Username to drop privileges to, or root to not drop provileges.\"}"));
  addOption("daemonize",
      JSON::fromString(
          "{\"long\":\"daemon\", \"short\":\"d\", \"value\":[1], \"long_off\":\"nodaemon\", \"short_off\":\"n\", \"help\":\"Whether or not to daemonize the process after starting.\"}"));
} //addConnectorOptions

/// Gets directory the current executable is stored in.
std::string Util::getMyPath(){
  char mypath[500];
  int ret = readlink("/proc/self/exe", mypath, 500);
  if (ret != -1){
    mypath[ret] = 0;
  }else{
    mypath[0] = 0;
  }
  std::string tPath = mypath;
  size_t slash = tPath.rfind('/');
  if (slash == std::string::npos){
    slash = tPath.rfind('\\');
    if (slash == std::string::npos){
      return "";
    }
  }
  tPath.resize(slash + 1);
  return tPath;
}

/// Sets the current process' running user
void Util::setUser(std::string username){
  if (username != "root"){
    struct passwd * user_info = getpwnam(username.c_str());
    if ( !user_info){
#if DEBUG >= 1
      fprintf(stderr, "Error: could not setuid %s: could not get PID\n", username.c_str());
#endif
      return;
    }else{
      if (setuid(user_info->pw_uid) != 0){
#if DEBUG >= 1
        fprintf(stderr, "Error: could not setuid %s: not allowed\n", username.c_str());
#endif
      }else{
#if DEBUG >= 3
        fprintf(stderr, "Changed user to %s\n", username.c_str());
#endif
      }
    }
  }
}

/// Will turn the current process into a daemon.
/// Works by calling daemon(1,0):
/// Does not change directory to root.
/// Does redirect output to /dev/null
void Util::Daemonize(){
#if DEBUG >= 3
  fprintf(stderr, "Going into background mode...\n");
#endif
  if (daemon(1, 0) < 0){
#if DEBUG >= 1
    fprintf(stderr, "Failed to daemonize: %s\n", strerror(errno));
#endif
  }
}
