#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

#define Int64 long long int

/// Contains all MP4 format related code.
namespace MP4{

  /// Creates a new box, optionally using the indicated pointer for storage.
  /// If manage is set to true, the pointer will be realloc'ed when the box needs to be resized.
  /// If the datapointer is NULL, manage is assumed to be true even if explicitly given as false.
  /// If managed, the pointer will be free'd upon destruction.
  Box::Box(char * datapointer, bool manage){
    data = datapointer;
    managed = manage;
    if (data == 0){
      clear();
    }else{
      data_size = ntohl(((int*)data)[0]);
    }
  }

  /// If managed, this will free the data pointer.
  Box::~Box(){
    if (managed && data != 0){
      free(data);
      data = 0;
    }
  }

  /// Returns the values at byte positions 4 through 7.
  std::string Box::getType() {
    return std::string(data+4,4);
  }

  /// Returns true if the given 4-byte boxtype is equal to the values at byte positions 4 through 7.
  bool Box::isType( char* boxType ) {
    return !memcmp(boxType, data + 4, 4);
  }

  /// Reads out a whole box (if possible) from newData, copying to the internal data storage and removing from the input string.
  /// \returns True on success, false otherwise.
  bool Box::read(std::string & newData){
    if (!managed){return false;}
    if (newData.size() > 4){
      size_t size = ntohl( ((int*)newData.c_str())[0] );
      if (newData.size() >= size){
        void * ret = malloc(size);
        if (!ret){return false;}
        free(data);
        data = (char*)ret;
        memcpy(data, newData.c_str(), size);
        newData.erase( 0, size );
        return true;
      }
    }
    return false;
  }

  /// Returns the total boxed size of this box, including the header.
  size_t Box::boxedSize() {
    return ntohl(((int*)data)[0]);
  }

  /// Retruns the size of the payload of thix box, excluding the header.
  /// This value is defined as boxedSize() - 8.
  size_t Box::payloadSize() {
    return boxedSize() - 8;
  }

  /// Returns a copy of the data pointer.
  char * Box::asBox() {
    return data;
  }

  /// Makes this box managed if it wasn't already, resetting the internal storage to 8 bytes (the minimum).
  /// If this box wasn't managed, the original data is left intact - otherwise it is free'd.
  /// If it was somehow impossible to allocate 8 bytes (should never happen), this will cause segfaults later.
  void Box::clear() {
    if (data && managed){free(data);}
    managed = true;
    data = (char*)malloc(8);
    if (data){
      data_size = 8;
      ((int*)data)[0] = htonl(data_size);
    }else{
      data_size = 0;
    }
  }

  /// Attempts to typecast this Box to a more specific type and call the toPrettyString() function of that type.
  /// If this failed, it will print out a message saying pretty-printing is not implemented for <boxtype>.
  std::string Box::toPrettyString(int indent){
    switch (ntohl( *((int*)(data+4)) )){ //type is at this address
      //case 0x6D666864: return ((MFHD*)this)->toPrettyString(indent); break;
      //case 0x6D6F6F66: return ((MOOF*)this)->toPrettyString(indent); break;
      case 0x61627374: return ((ABST*)this)->toPrettyString(indent); break;
      //case 0x61667274: return ((AFRT*)this)->toPrettyString(indent); break;
      case 0x61737274: return ((ASRT*)this)->toPrettyString(indent); break;
      default: return std::string(indent, ' ')+"Unimplemented pretty-printing for box "+std::string(data+4,4)+"\n"; break;
    }
  }

  /// Sets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt8( char newData, size_t index ) {
    index += 8;
    if (index >= boxedSize()){
      if (!reserve(index, 0, 1)){return;}
    }
    data[index] = newData;
  }

  /// Gets the 8 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  char Box::getInt8( size_t index ) {
    index += 8;
    if (index >= boxedSize()){
      if (!reserve(index, 0, 1)){return 0;}
    }
    return data[index];
  }

  /// Sets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt16( short newData, size_t index ) {
    index += 8;
    if (index+1 >= boxedSize()){
      if (!reserve(index, 0, 2)){return;}
    }
    newData = htons( newData );
    memcpy( data + index, (char*)&newData, 2 );
  }

  /// Gets the 16 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  short Box::getInt16( size_t index ) {
    index += 8;
    if (index+1 >= boxedSize()){
      if (!reserve(index, 0, 2)){return 0;}
    }
    short result;
    memcpy( (char*)&result, data + index, 2 );
    return ntohs(result);
  }
  
  /// Sets the 24 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt24( long newData, size_t index ) {
    index += 8;
    if (index+2 >= boxedSize()){
      if (!reserve(index, 0, 3)){return;}
    }
    data[index] = (newData & 0x00FF0000) >> 16;
    data[index+1] = (newData & 0x0000FF00) >> 8;
    data[index+2] = (newData & 0x000000FF);
  }

  /// Gets the 24 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  long Box::getInt24( size_t index ) {
    index += 8;
    if (index+2 >= boxedSize()){
      if (!reserve(index, 0, 3)){return 0;}
    }
    long result = data[index];
    result <<= 8;
    result += data[index+1];
    result <<= 8;
    result += data[index+2];
    return result;
  }

  /// Sets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt32( long newData, size_t index ) {
    index += 8;
    if (index+3 >= boxedSize()){
      if (!reserve(index, 0, 4)){return;}
    }
    newData = htonl( newData );
    memcpy( data + index, (char*)&newData, 4 );
  }

  /// Gets the 32 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  long Box::getInt32( size_t index ) {
    index += 8;
    if (index+3 >= boxedSize()){
      if (!reserve(index, 0, 4)){return 0;}
    }
    long result;
    memcpy( (char*)&result, data + index, 4 );
    return ntohl(result);
  }

  /// Sets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt64( Int64 newData, size_t index ) {
    index += 8;
    if (index+7 >= boxedSize()){
      if (!reserve(index, 0, 8)){return;}
    }
    data[index] = ( newData & 0xFF00000000000000 ) >> 56;
    data[index+1] = ( newData & 0x00FF000000000000 ) >> 48;
    data[index+2] = ( newData & 0x0000FF0000000000 ) >> 40;
    data[index+3] = ( newData & 0x000000FF00000000 ) >> 32;
    data[index+4] = ( newData & 0x00000000FF000000 ) >> 24;
    data[index+5] = ( newData & 0x0000000000FF0000 ) >> 16;
    data[index+6] = ( newData & 0x000000000000FF00 ) >> 8;
    data[index+7] = ( newData & 0x00000000000000FF );
  }

  /// Gets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Returns zero if resizing failed.
  Int64 Box::getInt64( size_t index ) {
    index += 8;
    if (index+7 >= boxedSize()){
      if (!reserve(index, 0, 8)){return 0;}
    }
    long result = data[index];
    result <<= 8; result += data[index+1];
    result <<= 8; result += data[index+2];
    result <<= 8; result += data[index+3];
    result <<= 8; result += data[index+4];
    result <<= 8; result += data[index+5];
    result <<= 8; result += data[index+6];
    result <<= 8; result += data[index+7];
    return result;
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(std::string newData, size_t index ) {
    setString( (char*)newData.c_str(), newData.size(), index );
  }

  /// Sets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string doesn't fit.
  /// Fails silently if resizing failed.
  void Box::setString(char* newData, size_t size, size_t index ) {
    index += 8;
    if (index >= boxedSize()){
      if (!reserve(index, 0, 1)){return;}
      data[index] = 0;
    }
    if (getStringLen(index) != size){
      if (!reserve(index, getStringLen(index)+1, size+1)){return;}
    }
    memcpy(data + index, newData, size+1);
  }

  /// Gets the NULL-terminated string at the given index.
  /// Will attempt to resize if the string is out of range.
  /// Returns null if resizing failed.
  char * Box::getString(size_t index){
    index += 8;
    if (index >= boxedSize()){
      if (!reserve(index, 0, 1)){return 0;}
      data[index] = 0;
    }
    return data+index;
  }

  /// Returns the length of the NULL-terminated string at the given index.
  /// Returns 0 if out of range.
  size_t Box::getStringLen(size_t index){
    index += 8;
    if (index >= boxedSize()){return 0;}
    return strlen(data+index);
  }

  /// Attempts to reserve enough space for wanted bytes of data at given position, where current bytes of data is now reserved.
  /// This will move any existing data behind the currently reserved space to the proper location after reserving.
  /// \returns True on success, false otherwise.
  bool Box::reserve(size_t position, size_t current, size_t wanted){
    if (current == wanted){return true;}
    if (current < wanted){
      //make bigger
      if (boxedSize() + (wanted-current) > data_size){
        //realloc if managed, otherwise fail
        if (!managed){return false;}
        void * ret = realloc(data, boxedSize() + (wanted-current));
        if (!ret){return false;}
        data = (char*)ret;
        data_size = boxedSize() + (wanted-current);
      }
      //move data behind backward, if any
      if (boxedSize() - (position+current) > 0){
        memmove(data+position+wanted, data+position+current, boxedSize() - (position+current));
      }
      //calculate and set new size
      int newSize = boxedSize() + (wanted-current);
      ((int*)data)[0] = htonl(newSize);
      return true;
    }else{
      //make smaller
      //move data behind forward, if any
      if (boxedSize() - (position+current) > 0){
        memmove(data+position+wanted, data+position+current, boxedSize() - (position+current));
      }
      //calculate and set new size
      int newSize = boxedSize() - (current-wanted);
      ((int*)data)[0] = htonl(newSize);
      return true;
    }
  }

  ABST::ABST( ) {
    memcpy(data + 4, "abst", 4);
    setVersion( 0 );
    setFlags( 0 );
    setBootstrapinfoVersion( 0 );
    setProfile( 0 );
    setLive( 1 );
    setUpdate( 0 );
    setTimeScale( 1000 );
    setCurrentMediaTime( 0 );
    setSmpteTimeCodeOffset( 0 );
    std::string empty;
    setMovieIdentifier( empty );
    setDrmData( empty );
    setMetaData( empty );  
  }
  
  void ABST::setVersion(char newVersion){setInt8(newVersion, 0);}
  
  char ABST::getVersion(){return getInt8(0);}
  
  void ABST::setFlags(long newFlags){setInt24(newFlags, 1);}
  
  long ABST::getFlags(){return getInt24(1);}

  void ABST::setBootstrapinfoVersion(long newVersion){setInt32(newVersion, 4);}
  
  long ABST::getBootstrapinfoVersion(){return getInt32(4);}

  void ABST::setProfile(char newProfile){
    //profile = bit 1 and 2 of byte 8.
    setInt8((getInt8(8) & 0x3F) + ((newProfile & 0x03) << 6), 8);
  }
  
  char ABST::getProfile(){return (getInt8(8) & 0xC0);};

  void ABST::setLive(bool newLive){
    //live = bit 4 of byte 8.
    setInt8((getInt8(8) & 0xDF) + (newLive ? 0x10 : 0 ), 8);
  }
  
  bool ABST::getLive(){return (getInt8(8) & 0x10);}

  void ABST::setUpdate(bool newUpdate) {
    //update = bit 5 of byte 8.
    setInt8((getInt8(8) & 0xEF) + (newUpdate ? 0x08 : 0), 8);
  }
  
  bool ABST::getUpdate(){return (getInt8(8) & 0x08);}

  void ABST::setTimeScale(long newScale){setInt32(newScale, 9);}
  
  long ABST::getTimeScale(){return getInt32(9);}
  
  void ABST::setCurrentMediaTime(Int64 newTime){setInt64(newTime, 13);}
  
  Int64 ABST::getCurrentMediaTime(){return getInt64(13);}

  void ABST::setSmpteTimeCodeOffset(Int64 newTime){setInt64(newTime, 21);}
  
  Int64 ABST::getSmpteTimeCodeOffset(){return getInt64(21);}

  void ABST::setMovieIdentifier(std::string & newIdentifier){setString(newIdentifier, 29);}
  
  char* ABST::getMovieIdentifier(){return getString(29);}

  long ABST::getServerEntryCount(){
    int countLoc = 29 + getStringLen(29)+1;
    return getInt8(countLoc);
  }

  void ABST::setServerEntry(std::string & newEntry, long no){
    int countLoc = 29 + getStringLen(29)+1;
    int tempLoc = countLoc+1;
    for (int i = 0; i < no; i++){
      if (i < getServerEntryCount()){
        tempLoc += getStringLen(tempLoc)+1;
      } else {
        if(!reserve(tempLoc, 0, no - getServerEntryCount())){return;};
        memset(data+tempLoc, 0, no - getServerEntryCount());
        tempLoc += no - getServerEntryCount();
        setInt8(no, countLoc);//set new serverEntryCount
        break;
      }
    }
    setString(newEntry, tempLoc);
  }
  
  ///\return Empty string if no > serverEntryCount(), serverEntry[no] otherwise.
  const char* ABST::getServerEntry(long no){
    if (no > getServerEntryCount()){return "";}
    int tempLoc = 29+getStringLen(29)+1 + 1;//position of entry count;
    for (int i = 0; i < no; i++){tempLoc += getStringLen(tempLoc)+1;}
    return getString(tempLoc);
  }
  
  long ABST::getQualityEntryCount(){
    int countLoc = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      countLoc += getStringLen(countLoc)+1;
    }
    return getInt8(countLoc);
  }
  
  void ABST::setQualityEntry(std::string & newEntry, long no){
    int countLoc = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      countLoc += getStringLen(countLoc)+1;
    }
    int tempLoc = countLoc+1;
    for (int i = 0; i < no; i++){
      if (i < getQualityEntryCount()){
        tempLoc += getStringLen(tempLoc)+1;
      } else {
        if(!reserve(tempLoc, 0, no - getQualityEntryCount())){return;};
        memset(data+tempLoc, 0, no - getQualityEntryCount());
        tempLoc += no - getQualityEntryCount();
        setInt8(no, countLoc);//set new qualityEntryCount
        break;
      }
    }
    setString(newEntry, tempLoc);
  }
  
  const char* ABST::getQualityEntry(long no){
    if (no > getQualityEntryCount()){return "";}
    int tempLoc = 29+getStringLen(29)+1 + 1;//position of serverentry count;
    for (int i = 0; i < getServerEntryCount(); i++){tempLoc += getStringLen(tempLoc)+1;}
    tempLoc += 1;
    for (int i = 0; i < no; i++){tempLoc += getStringLen(tempLoc)+1;}
    return getString(tempLoc);
  }
  
  void ABST::setDrmData( std::string newDrm ) {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    setString(newDrm, offset);
  }
  
  char* ABST::getDrmData() {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    return getString(offset);
  }

  void ABST::setMetaData( std::string newMetaData ) {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;
    setString(newMetaData, offset);
  }
  
  char* ABST::getMetaData() {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;
    return getString(offset);
  }

  long ABST::getSegmentRunTableCount(){
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;//DrmData
    offset+=getStringLen(offset)+1;//MetaData
    return getInt8(offset);
  }
  
  void ABST::setSegmentRunTable( ASRT newSegment, long no ) {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;//DrmData
    offset+=getStringLen(offset)+1;//MetaData
    int countLoc = offset;
    int tempLoc = countLoc + 1;//segmentRuntableCount
    for (int i = 0; i < no; i++){
      if (i < getSegmentRunTableCount()){
        tempLoc += Box(data+8+tempLoc,false).boxedSize();
      } else {
        if(!reserve(tempLoc, 0, 8 * (no - getSegmentRunTableCount()))){return;};
        for( int j = 0; j < (no - getSegmentRunTableCount())*8; j += 8 ) {
          setInt32(8,tempLoc+j);
        }
        tempLoc += (no - getServerEntryCount() ) * 8;
        setInt8(no, countLoc);//set new serverEntryCount
        break;
      }
    }
  }
  
  ASRT & ABST::getSegmentRunTable( long no ) {
    static Box result;
    if( no > getSegmentRunTableCount() ) {
      static Box res;
      return (ASRT&)res;
    }
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;//DrmData
    offset+=getStringLen(offset)+1;//MetaData
    int countLoc = offset;
    int tempLoc = countLoc + 1;//segmentRuntableCount
    for (int i = 0; i < no; i++){
      tempLoc += Box(data+8+tempLoc,false).boxedSize();
    }
    result = Box(data+8+tempLoc,false);
    return (ASRT&)result;
  }
  
  long ABST::getFragmentRunTableCount() {
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;//DrmData
    offset+=getStringLen(offset)+1;//MetaData
    int countLoc = offset;
    int tempLoc = countLoc + 1;//segmentRuntableCount
    for (int i = 0; i < getSegmentRunTableCount(); i++){
      tempLoc += Box(data+8+tempLoc,false).boxedSize();
    }
    return getInt8( tempLoc );
  }
  
  //tot hier

  AFRT & ABST::getFragmentRunTable( long no ) {
    static Box result;
    if( no > getFragmentRunTableCount() ) {
      static Box res;
      return (AFRT&)res;
    }
    long offset = 29 + getStringLen(29)+1 + 1;
    for( int i = 0; i< getServerEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset++;
    for( int i = 0; i< getQualityEntryCount(); i++ ) {
      offset += getStringLen(offset)+1;
    }
    offset+=getStringLen(offset)+1;//DrmData
    offset+=getStringLen(offset)+1;//MetaData
    int countLoc = offset;
    int tempLoc = countLoc + 1;//segmentRuntableCount
    for (int i = 0; i < getSegmentRunTableCount(); i++){
      tempLoc += Box(data+8+tempLoc,false).boxedSize();
    }
    tempLoc ++;//segmentRuntableCount
    for (int i = 0; i < no; i++){
      tempLoc += Box(data+8+tempLoc,false).boxedSize();
    }
    result = Box(data+8+tempLoc,false);
    return (AFRT&)result;
  }
  
  std::string ABST::toPrettyString( long indent ) {
    std::stringstream r;
    r << std::string(indent, ' ') << "[abst] Bootstrap Info" << std::endl;
    r << std::string(indent+1, ' ') << "Version " <<  getVersion() << std::endl;
    r << std::string(indent+1, ' ') << "BootstrapinfoVersion " << getBootstrapinfoVersion() << std::endl;
    r << std::string(indent+1, ' ') << "Profile " << getProfile() << std::endl;
    if( getLive() ) {
      r << std::string(indent+1, ' ' ) << "Live" << std::endl;
    }else{
      r << std::string(indent+1, ' ' ) << "Recorded" << std::endl;
    }
    if( getUpdate() ) {
      r << std::string(indent+1, ' ') << "Update" << std::endl;
    } else {
      r << std::string(indent+1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent+1, ' ') << "Timescale " << getTimeScale() << std::endl;
    r << std::string(indent+1, ' ') << "CurrMediaTime " << getCurrentMediaTime() << std::endl;
    r << std::string(indent+1, ' ') << "SmpteTimeCodeOffset " << getSmpteTimeCodeOffset() << std::endl;
    r << std::string(indent+1, ' ') << "MovieIdentifier " << getMovieIdentifier() << std::endl;
    r << std::string(indent+1, ' ') << "ServerEntryTable (" << getServerEntryCount() << ")" << std::endl;
    for( int i = 0; i < getServerEntryCount(); i++ ) {
      r << std::string(indent+2, ' ') << getServerEntry(i) << std::endl;
    }
    r << std::string(indent+1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for( int i = 0; i < getQualityEntryCount(); i++ ) {
      r << std::string(indent+2, ' ') << getQualityEntry(i) << std::endl;
    }
    
    r << std::string(indent+1, ' ') << "DrmData " << getDrmData() << std::endl;
    r << std::string(indent+1, ' ') << "MetaData " << getMetaData() << std::endl;

    r << std::string(indent+1, ' ') << "SegmentRunTableEntries (" << getSegmentRunTableCount() << ")" << std::endl;
    for( uint32_t i = 0; i < getSegmentRunTableCount(); i++ ) {
      r << ((Box)getSegmentRunTable(i)).toPrettyString(indent+2);
    }
    r << std::string(indent+1, ' ')+"FragmentRunTableEntries (" << getFragmentRunTableCount() << ")" << std::endl;
    for( uint32_t i = 0; i < getFragmentRunTableCount(); i++ ) {
      r << ((Box)getFragmentRunTable(i)).toPrettyString(indent+2);
    }
    return r.str();
  }
  
  AFRT::AFRT(){
    memcpy(data + 4, "afrt", 4);
    setVersion( 0 );
    setUpdate( 0 );
    setTimeScale( 1000 );
  }
  
  void AFRT::setVersion( char newVersion ) {
    setInt8( newVersion ,0 );
  }
  
  void AFRT::setUpdate( long newUpdate ) {
    setInt24( newUpdate, 1 );
  }
  
  void AFRT::setTimeScale( long newScale ) {
    setInt32( newScale, 4 );
  }
  
  void AFRT::addQualityEntry( std::string newQuality ) {
    qualityModifiers.push_back( newQuality );
  }
  
  void AFRT::addFragmentRun( long firstFragment, Int64 firstTimestamp, long duration, char discontinuity ) {
    fragmentRun newRun;
    newRun.firstFragment = firstFragment;
    newRun.firstTimestamp = firstTimestamp;
    newRun.duration = duration;
    newRun.discontinuity = discontinuity;
    fragmentRunTable.push_back( newRun );
  }
  
  std::string AFRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Fragment Run Table\n";
    if (getInt24(1)){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((Int64)getInt32(4)).asString()+"\n";
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((Int64)qualityModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < qualityModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+qualityModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Fragments "+JSON::Value((Int64)fragmentRunTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < fragmentRunTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+"Duration "+JSON::Value((Int64)fragmentRunTable[i].duration).asString()+", starting at "+JSON::Value((Int64)fragmentRunTable[i].firstFragment).asString()+" @ "+JSON::Value((Int64)fragmentRunTable[i].firstTimestamp).asString()+"\n";
    }
    return r;
  }
  
  ASRT::ASRT(){
    memcpy(data + 4, "asrt", 4);
    setVersion( 0 );
    setUpdate( 0 );
  }
  
  void ASRT::setVersion( char newVersion ) {
    setInt8( newVersion, 0 );
  }
  
  long ASRT::getVersion(){return getInt8(0);}
  
  void ASRT::setUpdate( long newUpdate ) {
    setInt24( newUpdate, 1 );
  }
  
  long ASRT::getUpdate(){return getInt24(1);}
  
  long ASRT::getQualityEntryCount(){
    return getInt8(4);
  }
  
  void ASRT::setQualityEntry(std::string & newEntry, long no){
    int countLoc = 4;
    int tempLoc = countLoc+1;
    for (int i = 0; i < no; i++){
      if (i < getQualityEntryCount()){
        tempLoc += getStringLen(tempLoc)+1;
      } else {
        if(!reserve(tempLoc, 0, no - getQualityEntryCount())){return;};
        memset(data+tempLoc, 0, no - getQualityEntryCount());
        tempLoc += no - getQualityEntryCount();
        setInt8(no, countLoc);//set new qualityEntryCount
        break;
      }
    }
    setString(newEntry, tempLoc);
  }
  
  const char* ASRT::getQualityEntry(long no){
    if (no > getQualityEntryCount()){return "";}
    int tempLoc = 5;//position of qualityentry count;
    for (int i = 0; i < no; i++){tempLoc += getStringLen(tempLoc)+1;}
    return getString(tempLoc);
  }
  
  long ASRT::getSegmentRunEntryCount() {
    int tempLoc = 5;//position of qualityentry count;
    for (int i = 0; i < getQualityEntryCount(); i++){tempLoc += getStringLen(tempLoc)+1;}
    return getInt32(tempLoc);
  }
  
  void ASRT::setSegmentRun( long firstSegment, long fragmentsPerSegment, long no ) {
    int tempLoc = 5;//position of qualityentry count;
    for (int i = 0; i < getSegmentRunEntryCount(); i++){tempLoc += getStringLen(tempLoc)+1;}
    int countLoc = tempLoc;
    tempLoc += 4;
    for (int i = 0; i < no; i++){
      if (i < getSegmentRunEntryCount()){
        tempLoc += 8;
      } else {
        if(!reserve(tempLoc, 0, (no - getQualityEntryCount())*8)){return;};
        memset(data+tempLoc, 0, (no - getQualityEntryCount())*8);
        tempLoc += (no - getQualityEntryCount())*8;
        setInt32(no, countLoc);//set new qualityEntryCount
        break;
      }
    }
    setInt32(firstSegment,tempLoc);
    setInt32(fragmentsPerSegment,tempLoc+4);
  }
  
  asrt_runtable ASRT::getSegmentRun( long no ) {
    int tempLoc = 5;//position of qualityentry count;
    for (int i = 0; i < getSegmentRunEntryCount(); i++){tempLoc += getStringLen(tempLoc)+1;}
    int countLoc = tempLoc;
    tempLoc += 4;
    for (int i = 0; i < no; i++){tempLoc += 8;}
    asrt_runtable res;
    res.firstSegment = getInt32(tempLoc);
    res.fragmentsPerSegment = getInt32(tempLoc+4);
    return res;
  }
  
  std::string ASRT::toPrettyString(int indent){
    std::stringstream r;
    r << std::string(indent, ' ') << "[asrt] Segment Run Table" << std::endl;
    r << std::string(indent+1, ' ') << "Version " << getVersion() << std::endl;
    if (getUpdate()){
      r << std::string(indent+1, ' ') << "Update" << std::endl;
    }else{
      r << std::string(indent+1, ' ') << "Replacement or new table" << std::endl;
    }
    r << std::string(indent+1, ' ') << "QualityEntryTable (" << getQualityEntryCount() << ")" << std::endl;
    for( int i = 0; i < getQualityEntryCount(); i++ ) {
      r << std::string(indent+2, ' ') << getQualityEntry(i) << std::endl;
    }
    r << std::string(indent+1, ' ') << "SegmentRunEntryTable (" << getSegmentRunEntryCount()<< ")" << std::endl;
    for( int i = 0; i < getSegmentRunEntryCount(); i ++ ) {
      r << std::string(indent+2, ' ') << "FirstSegment " << getSegmentRun(i).firstSegment << std::endl;
      r << std::string(indent+2, ' ') << "FragmentsPerSegment " << getSegmentRun(i).fragmentsPerSegment << std::endl;
    }
    return r.str();
  }
  
  MFHD::MFHD(){
    setInt32(0,0);
    memcpy(data + 4, "mfhd", 4);
  }
  
  void MFHD::setSequenceNumber( long newSequenceNumber ) {
    setInt32( newSequenceNumber, 4 );
  }
  
  std::string MFHD::toPrettyString( int indent ) {
    std::string r;
    r += std::string(indent, ' ')+"Movie Fragment Header\n";
    r += std::string(indent+1, ' ')+"SequenceNumber: "+JSON::Value((Int64)getInt32(4)).asString()+"\n";
  }
  
  MOOF::MOOF(){
    memcpy(data + 4, "moof", 4);
  }
  
  void MOOF::addContent( Box* newContent ) {
    content.push_back( newContent );
  }
  
  std::string MOOF::toPrettyString( int indent ) {
    
  }
  
  TRUN::TRUN(){
    memcpy(data + 4, "trun", 4);
  }
  
  void TRUN::setFlags( long newFlags ) {
    setInt24(newFlags,1);
  }
  
  void TRUN::setDataOffset( long newOffset ) {
    dataOffset = newOffset;
  }
  
  void TRUN::setFirstSampleFlags( char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy, char sampleIsDifferenceSample ) {
    firstSampleFlags = getSampleFlags( sampleDependsOn, sampleIsDependedOn, sampleHasRedundancy, sampleIsDifferenceSample );
  }
  
  void TRUN::addSampleInformation( long newDuration, long newSize, char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy,char sampleIsDifferenceSample, long newCompositionTimeOffset ) {
    trunSampleInformation newSample;
    newSample.sampleDuration = newDuration;
    newSample.sampleSize = newSize;
    newSample.sampleFlags = getSampleFlags( sampleDependsOn, sampleIsDependedOn, sampleHasRedundancy, sampleIsDifferenceSample );
    newSample.sampleCompositionTimeOffset = newCompositionTimeOffset;
    allSamples.push_back( newSample );
  }
  
  long TRUN::getSampleFlags( char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy, char sampleIsDifferenceSample ) {
    long sampleFlags = ( sampleDependsOn & 0x03 );
    sampleFlags <<= 2;
    sampleFlags += ( sampleIsDependedOn & 0x03 );
    sampleFlags <<= 2;
    sampleFlags += ( sampleHasRedundancy & 0x03 );
    sampleFlags <<= 5;
    sampleFlags += ( sampleIsDifferenceSample & 0x01 );
    sampleFlags <<= 17;
    sampleFlags += 0x0000FFFF;
    return sampleFlags;
  }
};
