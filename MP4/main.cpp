#include <iostream>
#include "box_avcC.h"
#include "box_stsd.h"
#include <string>

int main() {
  Box_avcC * Testing = new Box_avcC();
  Testing->SetCompressorName( "Test123" );
  std::cout << "H264::Size: " << Testing->GetBox()->GetHeader().TotalSize << "\n";
  Box_stsd * Testsample = new Box_stsd();
  std::cout << "STSD::Before Content: " << Testsample->GetBox()->GetHeader().TotalSize << "\n";
  Testsample->AddContent( Testing->GetBox() );
  std::cout << "STSD::After 1 Content: " << Testsample->GetBox()->GetHeader().TotalSize << "\n";
  Testsample->AddContent( Testing->GetBox(), 1 );
  std::cout << "STSD::After 2 Content: " << Testsample->GetBox()->GetHeader().TotalSize << "\n";
  delete Testsample;
  delete Testing;
  return 0;
}
