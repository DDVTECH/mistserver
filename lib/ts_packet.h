#pragma once
#include <string>
#include <cmath>
#include <stdint.h> //for uint64_t
#include <iostream>
#include <cstdio>
#include <algorithm>

class TS_Packet {
  public:
    TS_Packet();
    TS_Packet( std::string & Data );
    ~TS_Packet();
    void PID( int NewVal );
    int PID();
    void ContinuityCounter( int NewVal );
    int ContinuityCounter();
    void Clear();
    void PCR( int64_t NewVal );
    void AdaptionField( int NewVal );
    int AdaptionField( );
    int AdaptionFieldLen( );
    void DefaultPAT();
    void DefaultPMT();
    int UnitStart( );
    void UnitStart( int NewVal );
    int RandomAccess( );
    void RandomAccess( int NewVal );
    int BytesFree();
    int64_t PCR();
    void Print();
    std::string ToString();
    void PESLeadIn( int NewLen );
    void FillFree( std::string & PackageData );
    void AddStuffing( int NumBytes );
  private:
    int Free;
    char Buffer[188];///< The actual data
};
