#pragma once

#include <cstdio>
#include <string>
#include <cstring>
#include <vector>
#include <algorithm>
#include <map>
#include <sstream>
#include <fstream>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <pwd.h>
#include <grp.h>
#include <time.h>
#include <locale.h>
#include <langinfo.h>
#include <stdint.h>
#include <errno.h>

namespace Filesystem {
  enum DIR_Permissions {
    P_LIST = 0x01, //List
    P_RETR = 0x02, //Retrieve
    P_STOR = 0x04, //Store
    P_RNFT = 0x08, //Rename From/To
    P_DELE = 0x10, //Delete
    P_MKD = 0x20, //Make directory
    P_RMD = 0x40, //Remove directory
  };

  enum DIR_Show {
    S_NONE = 0x00, S_ACTIVE = 0x01, S_INACTIVE = 0x02, S_ALL = 0x03,
  };

  class Directory {
    public:
      Directory(std::string PathName = "", std::string BasePath = ".");
      ~Directory();
      void Print();
      bool IsDir();
      std::string PWD();
      std::string LIST(std::vector<std::string> ActiveStreams = std::vector<std::string>());
      bool CWD(std::string Path);
      bool CDUP();
      bool DELE(std::string Path);
      bool MKD(std::string Path);
      std::string RETR(std::string Path);
      void STOR(std::string Path, std::string Data);
      bool Rename(std::string From, std::string To);
      void SetPermissions(std::string PathName, char Permissions);
      bool HasPermission(char Permission);
      void SetVisibility(std::string Pathname, char Visible);
    private:
      bool ValidDir;
      bool SimplifyPath();
      void FillEntries();
      std::string MyBase;
      std::string MyPath;
      std::map<std::string, struct stat> Entries;
      std::map<std::string, char> MyPermissions;
      std::map<std::string, char> MyVisible;
  };
//Directory Class
}//Filesystem namespace
