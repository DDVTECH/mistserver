#include <stdlib.h> //for malloc and free
#include <string.h> //for memcpy
#include <arpa/inet.h> //for htonl and friends
#include "mp4.h"
#include "json.h"

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
        data = ret;
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
    data = malloc(8);
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
    switch( ntohl(((int*)data.c_str())[1]) ) { //type is at this address
      case 0x6D666864: return ((MFHD*)this)->toPrettyString(indent); break;
      case 0x6D6F6F66: return ((MOOF*)this)->toPrettyString(indent); break;
      case 0x61627374: return ((ABST*)this)->toPrettyString(indent); break;
      case 0x61667274: return ((AFRT*)this)->toPrettyString(indent); break;
      case 0x61737274: return ((ASRT*)this)->toPrettyString(indent); break;
      default: return std::string(indent, ' ')+"Unimplemented pretty-printing for box "+std::string(data,4,4)+"\n"; break;
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
    memcpy( (void*)(data.c_str() + index), (void*)&newData, 2 );
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
    memcpy( (void*)&result, (void*)(data.c_str() + index), 2 );
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
    memcpy( (char*)data.c_str() + index, (char*)&newData, 4 );
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
    memcpy( (char*)&result, (char*)data.c_str() + index, 4 );
    return ntohl(result);
  }

  /// Sets the 64 bits integer at the given index.
  /// Attempts to resize the data pointer if the index is out of range.
  /// Fails silently if resizing failed.
  void Box::setInt64( long long int newData, size_t index ) {
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
  long long int Box::getInt64( size_t index ) {
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
  bool reserve(size_t position, size_t current, size_t wanted){
    if (current == wanted){return true;}
    if (current < wanted){
      //make bigger
      if (boxedSize() + (wanted-current) > data_size){
        //realloc if managed, otherwise fail
        if (!managed){return false;}
        void * ret = realloc(data, boxedSize() + (wanted-current));
        if (!ret){return false;}
        data = ret;
        data_size = boxedSize() + (wanted-current);
      }
      //move data behind backward, if any
      if (boxedSize() - (position+current) > 0){
        memmove(position+wanted, position+current, boxedSize() - (position+current));
      }
      //calculate and set new size
      int newSize = boxedSize() + (wanted-current);
      ((int*)data)[0] = htonl(newSize);
      return true;
    }else{
      //make smaller
      //move data behind forward, if any
      if (boxedSize() - (position+current) > 0){
        memmove(position+wanted, position+current, boxedSize() - (position+current));
      }
      //calculate and set new size
      int newSize = boxedSize() - (current-wanted);
      ((int*)data)[0] = htonl(newSize);
      return true;
    }
  }

  ABST::ABST( ) : Box("abst") {
    setVersion( 0 );
    setFlags( 0 );
    setBootstrapinfoVersion( 0 );
    setProfile( 0 );
    setLive( 1 );
    setUpdate( 0 );
    setTimeScale( 1000 );
    setCurrentMediaTime( 0 );
    setSmpteTimeCodeOffset( 0 );
    setMovieIdentifier( "" );
    setDrmData( "" );
    setMetaData( "" );  
  }
  
  void ABST::setVersion( char newVersion ) {
    setInt8( newVersion, 0 );
  }
  
  void ABST::setFlags( long newFlags ) {
    setInt24( newFlags, 1 );
  }

  void ABST::setBootstrapinfoVersion( long newVersion ) {
    setInt32( newVersion, 4 );
  }

  void ABST::setProfile( char newProfile ) {
    setInt8((getInt8(8) & 0x3F) + ((newProfile & 0x02) << 6), 8);
  }


  void ABST::setLive( char newLive ) {
    setInt8((getInt8(8) & 0xDF) + ((newLive & 0x01) << 5), 8);
  }


  void ABST::setUpdate( char newUpdate ) {
    setInt8((getInt8(8) & 0xEF) + ((newUpdate & 0x01) << 4), 8);
  }

  void ABST::setTimeScale( long newScale ) {
    setInt32(newScale, 9);
  }
  
  void ABST::setCurrentMediaTime( long long int newTime ) {
    setInt64( newTime, 13);
  }

  void ABST::setSmpteTimeCodeOffset( long long int newTime ) {
    setInt64( newTime, 21);
  }

  void ABST::setMovieIdentifier( std::string newIdentifier ) {
    movieIdentifier = newIdentifier;
    isUpdated = true;
  }

  void ABST::addServerEntry( std::string newEntry ) {
    if( std::find( Servers.begin(), Servers.end(), newEntry ) == Servers.end() ) {
      Servers.push_back( newEntry );
      isUpdated = true;
    }
  }
  
  void ABST::delServerEntry( std::string delEntry ) {
    for( std::deque<std::string>::iterator it = Servers.begin(); it != Servers.end(); it++ ) {
      if( (*it) == delEntry ) {
        Servers.erase( it );
        isUpdated = true;
        break;
      }
    }
  }
  
  void ABST::addQualityEntry( std::string newEntry ) {
    if( std::find( Qualities.begin(), Qualities.end(), newEntry ) == Qualities.end() ) {
      Servers.push_back( newEntry );
      isUpdated = true;
    }
  }
  
  void ABST::delQualityEntry( std::string delEntry ) {
    for( std::deque<std::string>::iterator it = Qualities.begin(); it != Qualities.end(); it++ ) {
      if( (*it) == delEntry ) {
        Qualities.erase( it );
        isUpdated = true;
        break;
      }
    }
  }

  void ABST::setDrmData( std::string newDrm ) {
    drmData = newDrm;
    isUpdated = true;
  }

  void ABST::setMetaData( std::string newMetaData ) {
    metaData = newMetaData;
    isUpdated = true;
  }

  void ABST::addSegmentRunTable( Box * newSegment ) {
    segmentTables.push_back(newSegment);
    isUpdated = true;
  }

  void ABST::addFragmentRunTable( Box * newFragment ) {
    fragmentTables.push_back(newFragment);
    isUpdated = true;
  }
  
  void ABST::regenerate( ) {
    if (!isUpdated){return;}//skip if already up to date
    data.resize(29);
    int myOffset = 29;
    //0-terminated movieIdentifier
    memcpy( (char*)data.c_str() + myOffset, movieIdentifier.c_str(), movieIdentifier.size() + 1);
    myOffset += movieIdentifier.size() + 1;
    //8-bit server amount
    setInt8( Servers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = Servers.begin(); it != Servers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    //8-bit quality amount
    setInt8( Qualities.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = Qualities.begin();it != Qualities.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    //0-terminated DrmData
    memcpy( (char*)data.c_str() + myOffset, drmData.c_str(), drmData.size() + 1);
    myOffset += drmData.size() + 1;
    //0-terminated MetaData
    memcpy( (char*)data.c_str() + myOffset, metaData.c_str(), metaData.size() + 1);
    myOffset += metaData.size() + 1;
    //8-bit segment run amount
    setInt8( segmentTables.size(), myOffset );
    myOffset ++;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = segmentTables.begin(); it != segmentTables.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() );
      myOffset += (*it)->boxedSize();
    }
    //8-bit fragment run amount
    setInt8( fragmentTables.size(), myOffset );
    myOffset ++;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = fragmentTables.begin(); it != fragmentTables.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() + 1);
      myOffset += (*it)->boxedSize();
    }
    isUpdated = false;
  }

  std::string ABST::toPrettyString( int indent ) {
    regenerate();
    std::stringbuffer r;
    r << std::string(indent, ' ') << "Bootstrap Info" << std::endl;
    r << std::string(indent+1, ' ') << "Version " <<  getInt8(0) << std::endl;
    if( getInt8(5) & 0x10 ) {
      r << std::string(indent+1, ' ') << "Update" << std::endl;
    } else {
      r << std::string(indent+1, ' ') << "Replacement or new table" << std::endl;
    }
    if( getInt8(5) & 0x20 ) {
      r << std::string(indent+1, ' ' ) << "Live" << std::endl;
    }else{
      r << std::string(indent+1, ' ' ) << "Recorded" << std::endl;
    }
    r << std::string(indent+1, ' ') << "Profile " << ( getInt8(5) & 0xC0 ) << std::endl;
    r << std::string(indent+1, ' ') << "Timescale " << getInt64(10) << std::endl;
    r << std::string(indent+1, ' ') << "CurrMediaTime " << getInt32(6) << std::endl;
    r << std::string(indent+1, ' ') << "Segment Run Tables " << segmentTables.size() << std::endl;
    for( uint32_t i = 0; i < segmentTables.size(); i++ ) {
      r += segmentTables[i]->toPrettyString(indent+2);
    }
    r += std::string(indent+1, ' ')+"Fragment Run Tables "+JSON::Value((long long int)fragmentTables.size()).asString()+"\n";
    for( uint32_t i = 0; i < fragmentTables.size(); i++ ) {
      r += fragmentTables[i]->toPrettyString(indent+2);
    }
    return r.str();
  }
  
  AFRT::AFRT() : Box("afrt"){
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
    isUpdated = true;
  }
  
  void AFRT::addFragmentRun( long firstFragment, long long int firstTimestamp, long duration, char discontinuity ) {
    fragmentRun newRun;
    newRun.firstFragment = firstFragment;
    newRun.firstTimestamp = firstTimestamp;
    newRun.duration = duration;
    newRun.discontinuity = discontinuity;
    fragmentRunTable.push_back( newRun );
    isUpdated = true;
  }
  
  void AFRT::regenerate( ) {
    if (!isUpdated){return;}//skip if already up to date
    data.resize( 8 );
    int myOffset = 8;
    setInt8( qualityModifiers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = qualityModifiers.begin();it != qualityModifiers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    setInt32( fragmentRunTable.size(), myOffset );
    myOffset += 4;
    //table values for each entry
    for( std::deque<fragmentRun>::iterator it = fragmentRunTable.begin();it != fragmentRunTable.end(); it++ ) {
      setInt32( (*it).firstFragment, myOffset );
      myOffset += 4;
      setInt64( (*it).firstTimestamp, myOffset );
      myOffset += 8;
      setInt32( (*it).duration, myOffset );
      myOffset += 4;
      setInt8( (*it).discontinuity, myOffset );
      myOffset += 1;
    }
    isUpdated = false;
  }

  std::string AFRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Fragment Run Table\n";
    if (getInt24(1)){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Timescale "+JSON::Value((long long int)getInt32(4)).asString()+"\n";
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)qualityModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < qualityModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+qualityModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Fragments "+JSON::Value((long long int)fragmentRunTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < fragmentRunTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+"Duration "+JSON::Value((long long int)fragmentRunTable[i].duration).asString()+", starting at "+JSON::Value((long long int)fragmentRunTable[i].firstFragment).asString()+" @ "+JSON::Value((long long int)fragmentRunTable[i].firstTimestamp).asString()+"\n";
    }
    return r;
  }
  
  ASRT::ASRT() : Box("asrt") {
    setVersion( 0 );
    setUpdate( 0 );
  }
  
  void ASRT::setVersion( char newVersion ) {
    setInt8( newVersion, 0 );
  }
  
  void ASRT::setUpdate( long newUpdate ) {
    setInt24( newUpdate, 1 );
  }
  
  void ASRT::addQualityEntry( std::string newQuality ) {
    qualityModifiers.push_back( newQuality );
    isUpdated = true;
  }
  
  void ASRT::addSegmentRun( long firstSegment, long fragmentsPerSegment ) {
    segmentRun newRun;
    newRun.firstSegment = firstSegment;
    newRun.fragmentsPerSegment = fragmentsPerSegment;
    segmentRunTable.push_back( newRun );
    isUpdated = true;
  }
  
  void ASRT::regenerate( ) {
    if (!isUpdated){return;}//skip if already up to date
    data.resize( 4 );
    int myOffset = 4;
    setInt8( qualityModifiers.size(), myOffset );
    myOffset ++;
    //0-terminated string for each entry
    for( std::deque<std::string>::iterator it = qualityModifiers.begin();it != qualityModifiers.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it).c_str(), (*it).size() + 1);
      myOffset += (*it).size() + 1;
    }
    setInt32( segmentRunTable.size(), myOffset );
    myOffset += 4;
    //table values for each entry
    for( std::deque<segmentRun>::iterator it = segmentRunTable.begin();it != segmentRunTable.end(); it++ ) {
      setInt32( (*it).firstSegment, myOffset );
      myOffset += 4;
      setInt32( (*it).fragmentsPerSegment, myOffset );
      myOffset += 4;
    }
    isUpdated = false;
  }

  std::string ASRT::toPrettyString(int indent){
    std::string r;
    r += std::string(indent, ' ')+"Segment Run Table\n";
    if (getInt24(1)){
      r += std::string(indent+1, ' ')+"Update\n";
    }else{
      r += std::string(indent+1, ' ')+"Replacement or new table\n";
    }
    r += std::string(indent+1, ' ')+"Qualities "+JSON::Value((long long int)qualityModifiers.size()).asString()+"\n";
    for( uint32_t i = 0; i < qualityModifiers.size(); i++ ) {
      r += std::string(indent+2, ' ')+"\""+qualityModifiers[i]+"\"\n";
    }
    r += std::string(indent+1, ' ')+"Segments "+JSON::Value((long long int)segmentRunTable.size()).asString()+"\n";
    for( uint32_t i = 0; i < segmentRunTable.size(); i ++ ) {
      r += std::string(indent+2, ' ')+JSON::Value((long long int)segmentRunTable[i].fragmentsPerSegment).asString()+" fragments per, starting at "+JSON::Value((long long int)segmentRunTable[i].firstSegment).asString()+"\n";
    }
    return r;
  }
  
  MFHD::MFHD() : Box("mfhd") {
    setInt32(0,0);
  }
  
  void MFHD::setSequenceNumber( long newSequenceNumber ) {
    setInt32( newSequenceNumber, 4 );
  }
  
  std::string MFHD::toPrettyString( int indent ) {
    std::string r;
    r += std::string(indent, ' ')+"Movie Fragment Header\n";
    r += std::string(indent+1, ' ')+"SequenceNumber: "+JSON::Value((long long int)getInt32(4)).asString()+"\n";
  }
  
  MOOF::MOOF() : Box("moof") {}
  
  void MOOF::addContent( Box* newContent ) {
    content.push_back( newContent );
    isUpdated = true;
  }
  
  void MOOF::regenerate() {
    if (!isUpdated){return;}//skip if already up to date
    data.resize( 0 );
    int myOffset = 0;
    //retrieve box for each entry
    for( std::deque<Box*>::iterator it = content.begin(); it != content.end(); it++ ) {
      memcpy( (char*)data.c_str() + myOffset, (*it)->asBox().c_str(), (*it)->boxedSize() + 1);
      myOffset += (*it)->boxedSize();
    }
    isUpdated = false;
  }
  
  std::string MOOF::toPrettyString( int indent ) {
    std::string r;
    r += std::string(indent, ' ')+"Movie Fragment\n";
    for( uint32_t i = 0; i < content.size(); i++ ) {
      r += content[i]->toPrettyString(indent+2);
    }
    return r;
  }
  
  TRUN::TRUN() : Box("trun") {
    setInt8(0,0);
  }
  
  void TRUN::setFlags( long newFlags ) {
    setInt24(newFlags,1);
    isUpdated = true;
  }
  
  void TRUN::setDataOffset( long newOffset ) {
    dataOffset = newOffset;
    isUpdated = true;
  }
  
  void TRUN::setFirstSampleFlags( char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy, char sampleIsDifferenceSample ) {
    firstSampleFlags = getSampleFlags( sampleDependsOn, sampleIsDependedOn, sampleHasRedundancy, sampleIsDifferenceSample );
    isUpdated = true;
  }
  
  void TRUN::addSampleInformation( long newDuration, long newSize, char sampleDependsOn, char sampleIsDependedOn, char sampleHasRedundancy,char sampleIsDifferenceSample, long newCompositionTimeOffset ) {
    trunSampleInformation newSample;
    newSample.sampleDuration = newDuration;
    newSample.sampleSize = newSize;
    newSample.sampleFlags = getSampleFlags( sampleDependsOn, sampleIsDependedOn, sampleHasRedundancy, sampleIsDifferenceSample );
    newSample.sampleCompositionTimeOffset = newCompositionTimeOffset;
    allSamples.push_back( newSample );
    isUpdated = true;
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
  
  void TRUN::regenerate( ) {
    if (!isUpdated){return;}//skip if already up to date
    data.resize( 4 );
    int myOffset = 4;
    setInt32( allSamples.size(), myOffset );
    myOffset += 4;
    if( getInt24( 1 ) & 0x000001 ) {
      setInt32( dataOffset, myOffset );
      myOffset += 4;
    }
    if( getInt24( 1 ) & 0x000004 ) {
      setInt32( firstSampleFlags, myOffset );
      myOffset += 4;
    }
    for( std::deque<trunSampleInformation>::iterator it = allSamples.begin(); it != allSamples.end(); it++ ) {
      if( getInt24( 1 ) & 0x000100 ) {
        setInt32( (*it).sampleDuration, myOffset );
        myOffset += 4;
      }
      if( getInt24( 1 ) & 0x000200 ) {
        setInt32( (*it).sampleSize, myOffset );
        myOffset += 4;
      }
      if( getInt24( 1 ) & 0x000400 ) {
        setInt32( (*it).sampleFlags, myOffset );
        myOffset += 4;
      }
      if( getInt24( 1 ) & 0x000800 ) {
        setInt32( (*it).sampleCompositionTimeOffset, myOffset );
        myOffset += 4;
      }
    }
    isUpdated = false;
  }
};
