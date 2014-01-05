#include "filesystem.h"

Filesystem::Directory::Directory(std::string PathName, std::string BasePath){
  MyBase = BasePath;
  if (PathName[0] == '/'){
    PathName.erase(0, 1);
  }
  if (BasePath[BasePath.size() - 1] != '/'){
    BasePath += "/";
  }
  MyPath = PathName;
  FillEntries();
}

Filesystem::Directory::~Directory(){
}

void Filesystem::Directory::FillEntries(){
  ValidDir = true;
  struct stat StatBuf;
  Entries.clear();
  DIR * Dirp = opendir((MyBase + MyPath).c_str());
  if ( !Dirp){
    ValidDir = false;
  }else{
    dirent * entry;
    while ((entry = readdir(Dirp))){
      if (stat((MyBase + MyPath + "/" + entry->d_name).c_str(), &StatBuf) == -1){
#if DEBUG >= 4
        fprintf(stderr, "\tSkipping %s\n\t\tReason: %s\n", entry->d_name, strerror(errno));
#endif
        continue;
      }
      ///Convert stat to string
      Entries[std::string(entry->d_name)] = StatBuf;
    }
  }
}

void Filesystem::Directory::Print(){
  if ( !ValidDir){
    printf("%s is not a valid directory\n", (MyBase + MyPath).c_str());
    return;
  }
  printf("%s:\n", (MyBase + MyPath).c_str());
  for (std::map<std::string, struct stat>::iterator it = Entries.begin(); it != Entries.end(); it++){
    printf("\t%s\n", ( *it).first.c_str());
  }
  printf("\n");
}

bool Filesystem::Directory::IsDir(){
  return ValidDir;
}

std::string Filesystem::Directory::PWD(){
  return "/" + MyPath;
}

std::string Filesystem::Directory::LIST(std::vector<std::string> ActiveStreams){
  FillEntries();
  int MyPermissions;
  std::stringstream Converter;
  passwd* pwd; //For Username
  group* grp; //For Groupname
  tm* tm; //For time localisation
  char datestring[256]; //For time localisation

  std::string MyLoc = MyBase + MyPath;
  if (MyLoc[MyLoc.size() - 1] != '/'){
    MyLoc += "/";
  }

  for (std::map<std::string, struct stat>::iterator it = Entries.begin(); it != Entries.end(); it++){

    bool Active = (std::find(ActiveStreams.begin(), ActiveStreams.end(), ( *it).first) != ActiveStreams.end());
    fprintf(stderr, "%s active?: %d\n", ( *it).first.c_str(), Active);
    fprintf(stderr, "\tMyPath: %s\n\tVisible: %d\n", MyPath.c_str(), MyVisible[MyPath]);
    fprintf(stderr, "\t\tBitmask S_ACTIVE: %d\n\t\tBitmask S_INACTIVE: %d\n", MyVisible[MyPath] & S_ACTIVE, MyVisible[MyPath] & S_INACTIVE);
    if ((Active && (MyVisible[MyPath] & S_ACTIVE)) || (( !Active) && (MyVisible[MyPath] & S_INACTIVE)) || ((( *it).second.st_mode / 010000) == 4)){
      if ((( *it).second.st_mode / 010000) == 4){
        Converter << 'd';
      }else{
        Converter << '-';
      }
      MyPermissions = ((( *it).second.st_mode % 010000) / 0100);
      if (MyPermissions & 4){
        Converter << 'r';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 2){
        Converter << 'w';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 1){
        Converter << 'x';
      }else{
        Converter << '-';
      }
      MyPermissions = ((( *it).second.st_mode % 0100) / 010);
      if (MyPermissions & 4){
        Converter << 'r';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 2){
        Converter << 'w';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 1){
        Converter << 'x';
      }else{
        Converter << '-';
      }
      MyPermissions = (( *it).second.st_mode % 010);
      if (MyPermissions & 4){
        Converter << 'r';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 2){
        Converter << 'w';
      }else{
        Converter << '-';
      }
      if (MyPermissions & 1){
        Converter << 'x';
      }else{
        Converter << '-';
      }
      Converter << ' ';
      Converter << ( *it).second.st_nlink;
      Converter << ' ';
      if ((pwd = getpwuid(( *it).second.st_uid))){
        Converter << pwd->pw_name;
      }else{
        Converter << ( *it).second.st_uid;
      }
      Converter << ' ';
      if ((grp = getgrgid(( *it).second.st_gid))){
        Converter << grp->gr_name;
      }else{
        Converter << ( *it).second.st_gid;
      }
      Converter << ' ';
      Converter << ( *it).second.st_size;
      Converter << ' ';
      tm = localtime( &(( *it).second.st_mtime));
      strftime(datestring, sizeof(datestring), "%b %d %H:%M", tm);
      Converter << datestring;
      Converter << ' ';
      Converter << ( *it).first;
      Converter << '\n';
    }
  }
  return Converter.str();
}

bool Filesystem::Directory::CWD(std::string Path){
  if (Path[0] == '/'){
    Path.erase(0, 1);
    MyPath = Path;
  }else{
    if (MyPath != ""){
      MyPath += "/";
    }
    MyPath += Path;
  }
  FillEntries();
  printf("New Path: %s\n", MyPath.c_str());
  if (MyPermissions.find(MyPath) != MyPermissions.end()){
    printf("\tPermissions: %d\n", MyPermissions[MyPath]);
  }
  return SimplifyPath();
}

bool Filesystem::Directory::CDUP(){
  return CWD("..");
}

std::string Filesystem::Directory::RETR(std::string Path){
  std::string Result;
  std::string FileName;
  if (Path[0] == '/'){
    Path.erase(0, 1);
    FileName = MyBase + Path;
  }else{
    FileName = MyBase + MyPath + "/" + Path;
  }
  std::ifstream File;
  File.open(FileName.c_str());
  while (File.good()){
    Result += File.get();
  }
  File.close();
  return Result;
}

void Filesystem::Directory::STOR(std::string Path, std::string Data){
  if (MyPermissions.find(MyPath) == MyPermissions.end() || (MyPermissions[MyPath] & P_STOR)){
    std::string FileName;
    if (Path[0] == '/'){
      Path.erase(0, 1);
      FileName = MyBase + Path;
    }else{
      FileName = MyBase + MyPath + "/" + Path;
    }
    std::ofstream File;
    File.open(FileName.c_str());
    File << Data;
    File.close();
  }
}

bool Filesystem::Directory::SimplifyPath(){
  MyPath += "/";
  fprintf(stderr, "MyPath: %s\n", MyPath.c_str());
  std::vector<std::string> TempPath;
  std::string TempString;
  for (std::string::iterator it = MyPath.begin(); it != MyPath.end(); it++){
    if (( *it) == '/'){
      if (TempString == ".."){
        if ( !TempPath.size()){
          return false;
        }
        TempPath.erase((TempPath.end() - 1));
      }else if (TempString != "." && TempString != ""){
        TempPath.push_back(TempString);
      }
      TempString = "";
    }else{
      TempString += ( *it);
    }
  }
  MyPath = "";
  for (std::vector<std::string>::iterator it = TempPath.begin(); it != TempPath.end(); it++){
    MyPath += ( *it);
    if (it != (TempPath.end() - 1)){
      MyPath += "/";
    }
  }
  if (MyVisible.find(MyPath) == MyVisible.end()){
    MyVisible[MyPath] = S_ALL;
  }
  return true;
}

bool Filesystem::Directory::DELE(std::string Path){
  if (MyPermissions.find(MyPath) == MyPermissions.end() || (MyPermissions[MyPath] & P_DELE)){
    std::string FileName;
    if (Path[0] == '/'){
      Path.erase(0, 1);
      FileName = MyBase + Path;
    }else{
      FileName = MyBase + MyPath + "/" + Path;
    }
    if (std::remove(FileName.c_str())){
      fprintf(stderr, "Removing file %s unsuccesfull\n", FileName.c_str());
      return false;
    }
    return true;
  }
  return false;
}

bool Filesystem::Directory::MKD(std::string Path){
  std::string FileName;
  if (Path[0] == '/'){
    Path.erase(0, 1);
    FileName = MyBase + Path;
  }else{
    FileName = MyBase + MyPath + "/" + Path;
  }
  if (mkdir(FileName.c_str(), S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH)){
    fprintf(stderr, "Creating directory %s unsuccesfull\n", FileName.c_str());
    return false;
  }
  MyVisible[FileName] = S_ALL;
  return true;
}

bool Filesystem::Directory::Rename(std::string From, std::string To){
  if (MyPermissions.find(MyPath) == MyPermissions.end() || (MyPermissions[MyPath] & P_RNFT)){
    std::string FileFrom;
    if (From[0] == '/'){
      From.erase(0, 1);
      FileFrom = MyBase + From;
    }else{
      FileFrom = MyBase + MyPath + "/" + From;
    }
    std::string FileTo;
    if (To[0] == '/'){
      FileTo = MyBase + To;
    }else{
      FileTo = MyBase + MyPath + "/" + To;
    }
    if (std::rename(FileFrom.c_str(), FileTo.c_str())){
      fprintf(stderr, "Renaming file %s to %s unsuccesfull\n", FileFrom.c_str(), FileTo.c_str());
      return false;
    }
    return true;
  }
  return false;
}

void Filesystem::Directory::SetPermissions(std::string Path, char Permissions){
  MyPermissions[Path] = Permissions;
}

bool Filesystem::Directory::HasPermission(char Permission){
  if (MyPermissions.find(MyPath) == MyPermissions.end() || (MyPermissions[MyPath] & Permission)){
    return true;
  }
  return false;
}

void Filesystem::Directory::SetVisibility(std::string Pathname, char Visible){
  MyVisible[Pathname] = Visible;
}
