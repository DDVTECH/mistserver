
#include<iostream>
#include<string>

int main( ) {
  std::string Temp;
  while( std::cin.good() ) {
    Temp += std::cin.get();
  }
  while( Temp[0] == (char)0x00 ) {
    Temp.erase( Temp.begin() );
  }
  Temp.erase( Temp.end() - 1 );
  while( Temp[ Temp.size()-1 ] == (char)0x00 ) {
    Temp.erase( Temp.end() - 1 );
  }
  for( int i = 0; i < Temp.size(); i++ ) {
    std::cout << Temp[i];
  }
}
