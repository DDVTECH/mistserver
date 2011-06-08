/// \file Admin/main.cpp
/// Attempted administration tool for DDVTECH Clients.
/// Never finished - perhaps now obsolete...?
/// \todo This could serve as a basis for a new, more robust, control method for gearbox / the API.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <cstdio>
#include <stdlib.h>

int getparam (std::string input) {
  if(input.size() <= 3) { return -1;}
  int result = 0;
  for (int i = 3; i < input.size(); i++) {
    if ( input[i] < '0' || input[i] > '9') { return -2; }
    result = result * 10;
    result += input[i] - '0';
  }
  return result;
}

std::string getstringparam(std::string input) {
  std::string result;
  for (int i = 3; i < input.size(); i++) {
    result.push_back(input[i]);
  }
  return result;
}

void GetThroughPut(int * Down, int * Up){
  //deze file bevat o.a. totaal bytes verstuurd/ontvangen sinds de interface up ging, voor alle interfaces.
  //voor een snelheidsmeting moet je dus 2X lezen en het verschil gebruiken...
  std::ifstream netdev ("/proc/net/dev");
  std::string line;
  int tmpDown, tmpUp;
  int ret = 0;
  *Down = 0;
  *Up = 0;
  while (netdev.good()){
    getline(netdev, line);
    //probeer een geldige interface te parsen - 2 = 2 matches, en dus goed geparsed.
    ret = sscanf(line.c_str(), " %*[^:]: %i %*i %*i %*i %*i %*i %*i %*i %i %*i %*i %*i %*i %*i %*i %*i", &tmpDown, &tmpUp);
    if (ret == 2){*Down += tmpDown;*Up += tmpUp;}
  }
  netdev.close();
}//GetThroughPut

std::string MeasureThroughPut(){
  std::stringstream output;
  int frstDown, frstUp;
  int totDown, totUp;
  GetThroughPut(&frstDown, &frstUp);
  sleep(5);
  GetThroughPut(&totDown, &totUp);
  //return totaal bytes down, up, gemiddelde bytes per seconde over de afgelopen 5 secs down, up.
  output << totDown << " " << totUp << " " << ((totDown - frstDown)/5) << " " << ((totUp - frstUp)/5);
  return output.str();
}//MeasureThroughPut

std::string GetConnectedUsers(){
  std::string output;
  //laat ps aux de processen Client_PLS opvragen, zonder de grep zelf, en tel het aantal lines.
  system("ps aux | grep Client_PLS | grep -v grep | wc -l > ./tmpfile");
  //lees de file, en return de inhoud
  std::ifstream tmpfile ("./tmpfile");
  tmpfile >> output;
  tmpfile.close();
  return output;
}//GetConnectedUsers

void readpreset( unsigned int values[], std::string & filename ) {
  std::ifstream presetfile ("preset");
  presetfile >> filename;
  for (int i = 0; i < 9; i++ ) { presetfile >> values[i]; }
  presetfile.close();
}

void writepreset( unsigned int values[], std::string filename ) {
  std::ofstream presetfile ("preset");
  presetfile << filename;
  for (int i = 0; i < 9; i++ ) {
    presetfile << values[i];
    presetfile << "\n";
  }
  presetfile.close();
}

void writesh( unsigned int values[], std::string filename ) {
  std::ofstream shfile ("run.sh");
  shfile << "ffmpeg -i " << filename << " -f flv -re ";
  if (values[0] != 0 && values[1] != 0) { shfile << "-s " << values[0] << "x" << values[1] << " "; }
  if (values[2] != 0) { shfile << "-b " <<   values[2] << " "; }
  if (values[3] != 0) { shfile << "-gop " << values[3] << " "; }
  if (values[4] != 0) { shfile << "-r " <<   values[4] << " "; }
  if (values[5] != 0) { shfile << "-ab " <<  values[5] << " "; }
  if (values[6] != 0) { shfile << "-ar " <<  values[6] << " "; }
  shfile << "rtmp://projectlivestream.com/oflaDemo/test &";
  shfile.close();
  system("sh ./run.sh");
}

int main() {
  unsigned int values[9];
  std::string inputcommand = "";
  bool connection = true;
  int tempresult;
  std::string filename = "";
  readpreset(values, filename);
  while (connection) {
    std::cin >> inputcommand;
    switch (inputcommand[0]) {
      case 'G':
        switch (inputcommand[1]) {
          case 'S':
            switch(inputcommand[2]) {
              case 'H': std::cout << "OK" << values[0] << "\n"; break;
              case 'W': std::cout << "OK" << values[1] << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'V':
            switch(inputcommand[2]) {
              case 'B': std::cout << "OK" << values[2] << "\n"; break;
              case 'G': std::cout << "OK" << values[3] << "\n"; break;
              case 'R': std::cout << "OK" << values[4] << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'A':
            switch(inputcommand[2]) {
              case 'B': std::cout << "OK" << values[5] << "\n"; break;
              case 'R': std::cout << "OK" << values[6] << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'U':
            switch(inputcommand[2]) {
              case 'F': std::cout << "OK" << values[7] << "\n"; break;
              case 'P': std::cout << "OK" << values[8] << "\n"; break;
              case 'C': std::cout << "OK" << GetConnectedUsers() << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'F':
            switch(inputcommand[2]) {
              case 'N': std::cout << "OK" << filename << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'G':
            switch(inputcommand[2]) {
              case 'T': std::cout << "OK" << MeasureThroughPut() << "\n"; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          default: std::cout << "ER\n"; break;
        }
        break;
      case 'S':
        switch (inputcommand[1]) {
          case 'S':
            switch(inputcommand[2]) {
              case 'H':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[0] = tempresult; std::cout << "OK\n"; }
                break;
              case 'W':
                tempresult = getparam(inputcommand);
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[1] = tempresult; std::cout << "OK\n"; }
                break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'V':
            switch(inputcommand[2]) {
              case 'B':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[2] = tempresult; std::cout << "OK\n"; }
                break;
              case 'G':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[3] = tempresult; std::cout << "OK\n"; }
                break;
              case 'R':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[4] = tempresult; std::cout << "OK\n"; }
                break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'A':
            switch(inputcommand[2]) {
              case 'B':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[5] = tempresult; std::cout << "OK\n"; }
                break;
              case 'R':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[6] = tempresult; std::cout << "OK\n"; }
                break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'U':
            switch(inputcommand[2]) {
              case 'F':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[7] = tempresult; std::cout << "OK\n"; }
                break;
              case 'P':
                tempresult = getparam(inputcommand);
                if (tempresult < 0) { std::cout << "ER\n"; } else { values[8] = tempresult; std::cout << "OK\n"; }
                break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'F':
            switch(inputcommand[2]) {
              case 'N':
                filename = getstringparam(inputcommand); std::cout << "OK\n";
                break;
              default: std::cout << "ER\n"; break;
            }
            break;
          default: std::cout << "ER\n"; break;
        }
        break;
      case 'C':
        switch (inputcommand[1] ) {
          case 'C':
            switch (inputcommand[2]) {
              case 'D': std::cout << "OK\n"; connection = false; break;
              default: std::cout << "ER\n"; break;
            }
            break;
          case 'S':
            switch (inputcommand[2]) {
              case 'R': std::cout << "OK\n"; readpreset(values, filename); break;
              case 'S': std::cout << "OK\n"; writepreset(values, filename); break;
              case 'A': std::cout << "OK\n"; writesh(values, filename); break;
              default: std::cout << "ER\n"; break;
            }
            break;
          default: std::cout << "ER\n"; break;
        }
        break;
      default: std::cout << "ER\n"; break;
    }
  }
  return 0;
}

