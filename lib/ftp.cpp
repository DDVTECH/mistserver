#include "ftp.h"

FTP::User::User(Socket::Connection NewConnection, std::map<std::string, std::string> Credentials) {
  Conn = NewConnection;
  MyPassivePort = 0;
  USER = "";
  PASS = "";
  MODE = MODE_STREAM;
  STRU = STRU_FILE;
  TYPE = TYPE_ASCII_NONPRINT;
  PORT = 20;
  RNFR = "";
  AllCredentials = Credentials;

  MyDir = Filesystem::Directory("", FTPBasePath);
  MyDir.SetPermissions("", Filesystem::P_LIST);
  MyDir.SetPermissions("Unconverted", Filesystem::P_LIST | Filesystem::P_DELE | Filesystem::P_RNFT | Filesystem::P_STOR | Filesystem::P_RETR);
  MyDir.SetPermissions("Converted", Filesystem::P_LIST | Filesystem::P_DELE | Filesystem::P_RNFT | Filesystem::P_RETR);
  MyDir.SetPermissions("OnDemand", Filesystem::P_LIST | Filesystem::P_RETR);
  MyDir.SetPermissions("Live", Filesystem::P_LIST);

  MyDir.SetVisibility("Converted", Filesystem::S_INACTIVE);
  MyDir.SetVisibility("OnDemand", Filesystem::S_ACTIVE);

  JSON::Value MyConfig = JSON::fromFile("/tmp/mist/streamlist");
  for (JSON::ObjIter it = MyConfig["streams"].ObjBegin(); it != MyConfig["streams"].ObjEnd(); it++) {
    std::string ThisStream = (*it).second["channel"]["URL"].toString();
    ThisStream.erase(ThisStream.begin());
    ThisStream.erase(ThisStream.end() - 1);
    while (ThisStream.find('/') != std::string::npos) {
      ThisStream.erase(0, ThisStream.find('/') + 1);
    }
    ActiveStreams.push_back(ThisStream);
  }
}

FTP::User::~User() {
}

int FTP::User::ParseCommand(std::string Command) {
  Commands ThisCmd = CMD_NOCMD;
  if (Command.substr(0, 4) == "NOOP") {
    ThisCmd = CMD_NOOP;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "USER") {
    ThisCmd = CMD_USER;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "PASS") {
    ThisCmd = CMD_PASS;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "QUIT") {
    ThisCmd = CMD_QUIT;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "PORT") {
    ThisCmd = CMD_PORT;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "RETR") {
    ThisCmd = CMD_RETR;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "STOR") {
    ThisCmd = CMD_STOR;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "TYPE") {
    ThisCmd = CMD_TYPE;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "MODE") {
    ThisCmd = CMD_MODE;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "STRU") {
    ThisCmd = CMD_STRU;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "EPSV") {
    ThisCmd = CMD_EPSV;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "PASV") {
    ThisCmd = CMD_PASV;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "LIST") {
    ThisCmd = CMD_LIST;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "CDUP") {
    ThisCmd = CMD_CDUP;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "DELE") {
    ThisCmd = CMD_DELE;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "RNFR") {
    ThisCmd = CMD_RNFR;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 4) == "RNTO") {
    ThisCmd = CMD_RNTO;
    Command.erase(0, 5);
  }
  if (Command.substr(0, 3) == "PWD") {
    ThisCmd = CMD_PWD;
    Command.erase(0, 4);
  }
  if (Command.substr(0, 3) == "CWD") {
    ThisCmd = CMD_CWD;
    Command.erase(0, 4);
  }
  if (Command.substr(0, 3) == "RMD") {
    ThisCmd = CMD_RMD;
    Command.erase(0, 4);
  }
  if (Command.substr(0, 3) == "MKD") {
    ThisCmd = CMD_MKD;
    Command.erase(0, 4);
  }
  if (ThisCmd != CMD_RNTO) {
    RNFR = "";
  }
  switch (ThisCmd) {
    case CMD_NOOP: {
        return 200; //Command okay.
        break;
      }
    case CMD_USER: {
        USER = "";
        PASS = "";
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        USER = Command;
        return 331; //User name okay, need password.
        break;
      }
    case CMD_PASS: {
        if (USER == "") {
          return 503;
        } //Bad sequence of commands
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        PASS = Command;
        if (!LoggedIn()) {
          USER = "";
          PASS = "";
          return 530; //Not logged in.
        }
        return 230;
        break;
      }
    case CMD_LIST: {
        Socket::Connection Connected = Passive.accept();
        if (Connected.connected()) {
          Conn.SendNow("125 Data connection already open; transfer starting.\n");
        } else {
          Conn.SendNow("150 File status okay; about to open data connection.\n");
        }
        while (!Connected.connected()) {
          Connected = Passive.accept();
        }
        std::string tmpstr = MyDir.LIST(ActiveStreams);
        Connected.SendNow(tmpstr);
        Connected.close();
        return 226;
        break;
      }
    case CMD_QUIT: {
        return 221; //Service closing control connection. Logged out if appropriate.
        break;
      }
    case CMD_PORT: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        PORT = atoi(Command.c_str());
        return 200; //Command okay.
        break;
      }
    case CMD_EPSV: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        MyPassivePort = (rand() % 9999);
        Passive = Socket::Server(MyPassivePort, "0.0.0.0", true);
        return 229;
        break;
      }
    case CMD_PASV: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        MyPassivePort = (rand() % 9999) + 49152;
        Passive = Socket::Server(MyPassivePort, "0.0.0.0", true);
        return 227;
        break;
      }
    case CMD_RETR: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (!MyDir.HasPermission(Filesystem::P_RETR)) {
          return 550;
        } //Access denied.
        Socket::Connection Connected = Passive.accept();
        if (Connected.connected()) {
          Conn.SendNow("125 Data connection already open; transfer starting.\n");
        } else {
          Conn.SendNow("150 File status okay; about to open data connection.\n");
        }
        while (!Connected.connected()) {
          Connected = Passive.accept();
        }
        std::string tmpstr = MyDir.RETR(Command);
        Connected.SendNow(tmpstr);
        Connected.close();
        return 226;
        break;
      }
    case CMD_STOR: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (!MyDir.HasPermission(Filesystem::P_STOR)) {
          return 550;
        } //Access denied.
        Socket::Connection Connected = Passive.accept();
        if (Connected.connected()) {
          Conn.SendNow("125 Data connection already open; transfer starting.\n");
        } else {
          Conn.SendNow("150 File status okay; about to open data connection.\n");
        }
        while (!Connected.connected()) {
          Connected = Passive.accept();
        }
        std::string Buffer;
        while (Connected.spool()) {
        }
        /// \todo Comment me back in. ^_^
        //Buffer = Connected.Received();
        MyDir.STOR(Command, Buffer);
        return 250;
        break;
      }
    case CMD_TYPE: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (Command.size() != 1 && Command.size() != 3) {
          return 501;
        } //Syntax error in parameters or arguments.
        switch (Command[0]) {
          case 'A': {
              if (Command.size() > 1) {
                if (Command[1] != ' ') {
                  return 501;
                } //Syntax error in parameters or arguments.
                if (Command[2] != 'N') {
                  return 504;
                } //Command not implemented for that parameter.
              }
              TYPE = TYPE_ASCII_NONPRINT;
              break;
            }
          case 'I': {
              if (Command.size() > 1) {
                if (Command[1] != ' ') {
                  return 501;
                } //Syntax error in parameters or arguments.
                if (Command[2] != 'N') {
                  return 504;
                } //Command not implemented for that parameter.
              }
              TYPE = TYPE_IMAGE_NONPRINT;
              break;
            }
          default: {
              return 504; //Command not implemented for that parameter.
              break;
            }
        }
        return 200; //Command okay.
        break;
      }
    case CMD_MODE: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (Command.size() != 1) {
          return 501;
        } //Syntax error in parameters or arguments.
        if (Command[0] != 'S') {
          return 504;
        } //Command not implemented for that parameter.
        MODE = MODE_STREAM;
        return 200; //Command okay.
        break;
      }
    case CMD_STRU: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (Command.size() != 1) {
          return 501;
        } //Syntax error in parameters or arguments.
        switch (Command[0]) {
          case 'F': {
              STRU = STRU_FILE;
              break;
            }
          case 'R': {
              STRU = STRU_RECORD;
              break;
            }
          default: {
              return 504; //Command not implemented for that parameter.
              break;
            }
        }
        return 200; //Command okay.
        break;
      }
    case CMD_PWD: {
        if (!LoggedIn()) {
          return 550;
        } //Not logged in.
        if (Command != "") {
          return 501;
        } //Syntax error in parameters or arguments.
        return 2570; //257 -- 0 to indicate PWD over MKD
        break;
      }
    case CMD_CWD: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        Filesystem::Directory TmpDir = MyDir;
        if (TmpDir.CWD(Command)) {
          if (TmpDir.IsDir()) {
            MyDir = TmpDir;
            return 250;
          }
        }
        return 550;
        break;
      }
    case CMD_CDUP: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command != "") {
          return 501;
        } //Syntax error in parameters or arguments.
        Filesystem::Directory TmpDir = MyDir;
        if (TmpDir.CDUP()) {
          if (TmpDir.IsDir()) {
            MyDir = TmpDir;
            return 250;
          }
        }
        return 550;
        break;
      }
    case CMD_DELE: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (!MyDir.DELE(Command)) {
          return 550;
        }
        return 250;
        break;
      }
    case CMD_RMD: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (!MyDir.HasPermission(Filesystem::P_RMD)) {
          return 550;
        }
        if (!MyDir.DELE(Command)) {
          return 550;
        }
        return 250;
        break;
      }
    case CMD_MKD: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (!MyDir.HasPermission(Filesystem::P_MKD)) {
          return 550;
        }
        if (!MyDir.MKD(Command)) {
          return 550;
        }
        return 2571;
        break;
      }
    case CMD_RNFR: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        RNFR = Command;
        return 350; //Awaiting further information
      }
    case CMD_RNTO: {
        if (!LoggedIn()) {
          return 530;
        } //Not logged in.
        if (Command == "") {
          return 501;
        } //Syntax error in parameters or arguments.
        if (RNFR == "") {
          return 503;
        } //Bad sequence of commands
        if (!MyDir.Rename(RNFR, Command)) {
          return 550;
        }
        return 250;
      }
    default: {
        return 502; //Command not implemented.
        break;
      }
  }
}

bool FTP::User::LoggedIn() {
  if (USER == "" || PASS == "") {
    return false;
  }
  if (!AllCredentials.size()) {
    return true;
  }
  if ((AllCredentials.find(USER) != AllCredentials.end()) && AllCredentials[USER] == PASS) {
    return true;
  }
  return false;
}

std::string FTP::User::NumToMsg(int MsgNum) {
  std::string Result;
  switch (MsgNum) {
    case 200: {
        Result = "200 Message okay.\n";
        break;
      }
    case 221: {
        Result = "221 Service closing control connection. Logged out if appropriate.\n";
        break;
      }
    case 226: {
        Result = "226 Closing data connection.\n";
        break;
      }
    case 227: {
        std::stringstream sstr;
        sstr << "227 Entering passive mode (0,0,0,0,";
        sstr << (MyPassivePort >> 8) % 256;
        sstr << ",";
        sstr << MyPassivePort % 256;
        sstr << ").\n";
        Result = sstr.str();
        break;
      }
    case 229: {
        std::stringstream sstr;
        sstr << "229 Entering extended passive mode (|||";
        sstr << MyPassivePort;
        sstr << "|).\n";
        Result = sstr.str();
        break;
      }
    case 230: {
        Result = "230 User logged in, proceed.\n";
        break;
      }
    case 250: {
        Result = "250 Requested file action okay, completed.\n";
        break;
      }
    case 2570: { //PWD
        Result = "257 \"" + MyDir.PWD() + "\" selected as PWD\n";
        break;
      }
    case 2571: { //MKD
        Result = "257 \"" + MyDir.PWD() + "\" created\n";
        break;
      }
    case 331: {
        Result = "331 User name okay, need password.\n";
        break;
      }
    case 350: {
        Result = "350 Requested file action pending further information\n";
        break;
      }
    case 501: {
        Result = "501 Syntax error in parameters or arguments.\n";
        break;
      }
    case 502: {
        Result = "502 Command not implemented.\n";
        break;
      }
    case 503: {
        Result = "503 Bad sequence of commands.\n";
        break;
      }
    case 504: {
        Result = "504 Command not implemented for that parameter.\n";
        break;
      }
    case 530: {
        Result = "530 Not logged in.\n";
        break;
      }
    case 550: {
        Result = "550 Requested action not taken.\n";
        break;
      }
    default: {
        Result = "Error msg not implemented?\n";
        break;
      }
  }
  return Result;
}
