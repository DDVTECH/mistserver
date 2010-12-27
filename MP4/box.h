#pragma once
#include "boxheader.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>

class Box {
  public:
    Box();
    Box(uint32_t BoxType);
    ~Box();

    void SetBoxType(uint32_t BoxType);
    uint32_t GetBoxType();

    void SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index = 0);
    uint32_t GetPayloadSize();
    uint8_t * GetPayload();
    uint8_t * GetPayload(uint32_t Index, uint32_t & Size);

    static uint8_t * uint32_to_uint8( uint32_t data );
    static uint8_t * uint16_to_uint8( uint16_t data );
    BoxHeader GetHeader( );
    void ResetPayload( );
  private:
    BoxHeader header;
    uint8_t * Payload;

    uint32_t PayloadSize;
};//Box Class

