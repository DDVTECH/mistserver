#include "box_hdlr.h"

Box_hdlr::Box_hdlr( ) {
  Container = new Box( 0x68646C72 );
  CurrentHandlerType = 0;
  SetReserved();
}

Box_hdlr::~Box_hdlr() {
  delete Container;
}

Box * Box_hdlr::GetBox() {
  return Container;
}

void Box_hdlr::SetReserved( ) {
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),20);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),16);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),12);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0),4);
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(0));
}

void Box_hdlr::SetHandlerType( uint32_t HandlerType ) {
  if( HandlerType != 0 ) {
    CurrentHandlerType = HandlerType;
  }
  Container->SetPayload((uint32_t)4,Box::uint32_to_uint8(CurrentHandlerType),8);
}

void Box_hdlr::SetName ( std::string Name ) {
  char * tmp = new char[Name.size()+1];
  strcpy(tmp,Name.c_str());
  Container->ResetPayload();
  SetReserved();
  SetHandlerType(0);
  Container->SetPayload((uint32_t)strlen(tmp)+1,(uint8_t*)tmp,24);
}

void Box_hdlr::SetDefaults( ) {
  SetName( );
  SetHandlerType( );
}
