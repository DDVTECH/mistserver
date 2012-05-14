#pragma once

class TS_Packet {
  public:
    TS_Packet();
    ~TS_Packet();
    void PID( int NewVal );
    int PID();
    void ContinuityCounter( int NewVal );
    int ContinuityCounter();
    void MsgLen( int NewVal );
    int MsgLen();
    void Clear();
    void SetPAT();
    void SetPMT();
    int Free();
  private:
    int Free;
    char Buffer[188];///< The actual data
};
