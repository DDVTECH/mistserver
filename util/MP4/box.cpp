#pragma once

#include <stdint.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <string>
#include <vector>

class Box {
  public:
    Box();
    Box(uint32_t BoxType);
    Box(uint8_t * Content, uint32_t length);
    ~Box();
    void SetBoxType(uint32_t BoxType);
    uint32_t GetBoxType();
    void SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index = 0);
    uint32_t GetPayloadSize();
    uint8_t * GetPayload();
    uint8_t * GetPayload(uint32_t Index, uint32_t & Size);
    uint32_t GetBoxedDataSize();
    uint8_t * GetBoxedData( );
    static uint8_t * uint32_to_uint8( uint32_t data );
    static uint8_t * uint16_to_uint8( uint16_t data );
    static uint8_t * uint8_to_uint8( uint8_t data );
    void ResetPayload( );
  private:
    uint8_t * Payload;
    uint32_t PayloadSize;
};//Box Class

Box::Box() {
  Payload = (uint8_t *)malloc(8);
  PayloadSize = 0;
}

Box::Box(uint32_t BoxType) {
  Payload = (uint8_t *)malloc(8);
  SetBoxType(BoxType);
  PayloadSize = 0;
}

Box::Box(uint8_t * Content, uint32_t length) {
  PayloadSize = length-8;
  Payload = (uint8_t *)malloc(length);
  memcpy(Payload, Content, length);
}

Box::~Box() {
  if (Payload) free(Payload);
}

void Box::SetBoxType(uint32_t BoxType) {
  ((unsigned int*)Payload)[1] = htonl(BoxType);
}

uint32_t Box::GetBoxType() {
  return ntohl(((unsigned int*)Payload)[1]);
}

void Box::SetPayload(uint32_t Size, uint8_t * Data, uint32_t Index) {
  if ( Index + Size > PayloadSize ) {
    PayloadSize = Index + Size;
    ((unsigned int*)Payload)[0] = htonl(PayloadSize+8);
    Payload = (uint8_t *)realloc(Payload, PayloadSize + 8);
  }
  memcpy(Payload + 8 + Index, Data, Size);
}

uint32_t Box::GetPayloadSize() {
  return PayloadSize;
}

uint8_t * Box::GetPayload() {
  return Payload+8;
}

uint8_t * Box::GetPayload(uint32_t Index, uint32_t & Size) {
  if(Index > PayloadSize) {Size = 0;}
  if(Index + Size > PayloadSize) { Size = PayloadSize - Index; }
  return Payload + 8 + Index;
}

uint32_t Box::GetBoxedDataSize() {
  return ntohl(((unsigned int*)Payload)[0]);
}

uint8_t * Box::GetBoxedData( ) {
  return Payload;
}


uint8_t * Box::uint32_to_uint8( uint32_t data ) {
  uint8_t * temp = new uint8_t[4];
  temp[0] = (data >> 24) & 0x000000FF;
  temp[1] = (data >> 16 ) & 0x000000FF;
  temp[2] = (data >> 8 ) & 0x000000FF;
  temp[3] = (data ) & 0x000000FF;
  return temp;
}

uint8_t * Box::uint16_to_uint8( uint16_t data ) {
  uint8_t * temp = new uint8_t[2];
  temp[0] = (data >> 8) & 0x00FF;
  temp[1] = (data  ) & 0x00FF;
  return temp;
}

uint8_t * Box::uint8_to_uint8( uint8_t data ) {
   uint8_t * temp = new uint8_t[1];
   temp[0] = data;
   return temp;
}

void Box::ResetPayload( ) {
  PayloadSize = 0;
  Payload = (uint8_t *)realloc(Payload, PayloadSize + 8);
  ((unsigned int*)Payload)[0] = htonl(0);
}
