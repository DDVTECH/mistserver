#undef OLDHANDSHAKE //change to #define for old handshake method

char versionstring[] = "WWW.DDVTECH.COM ";

#ifdef OLDHANDSHAKE
struct Handshake {
  char Time[4];
  char Zero[4];
  char Random[1528];
};//Handshake

bool doHandshake(){
  char Version;
  Handshake Client;
  Handshake Server;
  /** Read C0 **/
  DDV_read(&(Version), 1, 1, CONN_fd);
  /** Read C1 **/
  DDV_read(Client.Time, 1, 4, CONN_fd);
  DDV_read(Client.Zero, 1, 4, CONN_fd);
  DDV_read(Client.Random, 1, 1528, CONN_fd);
  rec_cnt+=1537;
  /** Build S1 Packet **/
  Server.Time[0] = 0; Server.Time[1] = 0; Server.Time[2] = 0; Server.Time[3] = 0;
  Server.Zero[0] = 0; Server.Zero[1] = 0; Server.Zero[2] = 0; Server.Zero[3] = 0;
  for (int i = 0; i < 1528; i++){
    Server.Random[i] = versionstring[i%13];
  }
  /** Send S0 **/
  DDV_write(&(Version), 1, 1, CONN_fd);
  /** Send S1 **/
  DDV_write(Server.Time, 1, 4, CONN_fd);
  DDV_write(Server.Zero, 1, 4, CONN_fd);
  DDV_write(Server.Random, 1, 1528, CONN_fd);
  /** Flush output, just for certainty **/
  //fflush(CONN_fd);
  snd_cnt+=1537;
  /** Send S2 **/
  DDV_write(Client.Time, 1, 4, CONN_fd);
  DDV_write(Client.Time, 1, 4, CONN_fd);
  DDV_write(Client.Random, 1, 1528, CONN_fd);
  snd_cnt+=1536;
  /** Flush, necessary in order to work **/
  //fflush(CONN_fd);
  /** Read and discard C2 **/
  DDV_read(Client.Time, 1, 4, CONN_fd);
  DDV_read(Client.Zero, 1, 4, CONN_fd);
  DDV_read(Client.Random, 1, 1528, CONN_fd);
  rec_cnt+=1536;
  return true;
}//doHandshake

#else

#include "crypto.cpp" //cryptography for handshaking

bool doHandshake(){
  char Version;
  /** Read C0 **/
  DDV_read(&Version, 1, 1, CONN_fd);
  uint8_t Client[1536];
  uint8_t Server[3072];
  DDV_read(&Client, 1, 1536, CONN_fd);
  rec_cnt+=1537;
  
  /** Build S1 Packet **/
  *((uint32_t*)Server) = 0;//time zero
  *(((uint32_t*)(Server+4))) = htonl(0x01020304);//version 1 2 3 4
  for (int i = 8; i < 3072; ++i){Server[i] = versionstring[i%13];}//"random" data

  bool encrypted = (Version == 6);
  #if DEBUG >= 4
  fprintf(stderr, "Handshake version is %hhi\n", Version);
  #endif
  uint8_t _validationScheme = 5;
  if (ValidateClientScheme(Client, 0)) _validationScheme = 0;
  if (ValidateClientScheme(Client, 1)) _validationScheme = 1;

  #if DEBUG >= 4
  fprintf(stderr, "Handshake type is %hhi, encryption is %s\n", _validationScheme, encrypted?"on":"off");
  #endif

  //**** FIRST 1536 bytes from server response ****//
  //compute DH key position
  uint32_t serverDHOffset = GetDHOffset(Server, _validationScheme);
  uint32_t clientDHOffset = GetDHOffset(Client, _validationScheme);

  //generate DH key
  DHWrapper dhWrapper(1024);
  if (!dhWrapper.Initialize()) return false;
  if (!dhWrapper.CreateSharedKey(Client + clientDHOffset, 128)) return false;
  if (!dhWrapper.CopyPublicKey(Server + serverDHOffset, 128)) return false;

  if (encrypted) {
    uint8_t secretKey[128];
    if (!dhWrapper.CopySharedKey(secretKey, sizeof (secretKey))) return false;
    RC4_KEY _pKeyIn;
    RC4_KEY _pKeyOut;
    InitRC4Encryption(secretKey, (uint8_t*) & Client[clientDHOffset], (uint8_t*) & Server[serverDHOffset], &_pKeyIn, &_pKeyOut);
    uint8_t data[1536];
    RC4(&_pKeyIn, 1536, data, data);
    RC4(&_pKeyOut, 1536, data, data);
  }
  //generate the digest
  uint32_t serverDigestOffset = GetDigestOffset(Server, _validationScheme);
  uint8_t *pTempBuffer = new uint8_t[1536 - 32];
  memcpy(pTempBuffer, Server, serverDigestOffset);
  memcpy(pTempBuffer + serverDigestOffset, Server + serverDigestOffset + 32, 1536 - serverDigestOffset - 32);
  uint8_t *pTempHash = new uint8_t[512];
  HMACsha256(pTempBuffer, 1536 - 32, genuineFMSKey, 36, pTempHash);
  memcpy(Server + serverDigestOffset, pTempHash, 32);
  delete[] pTempBuffer;
  delete[] pTempHash;

  //**** SECOND 1536 bytes from server response ****//
  uint32_t keyChallengeIndex = GetDigestOffset(Client, _validationScheme);
  pTempHash = new uint8_t[512];
  HMACsha256(Client + keyChallengeIndex, 32, genuineFMSKey, 68, pTempHash);
  uint8_t *pLastHash = new uint8_t[512];
  HMACsha256(Server + 1536, 1536 - 32, pTempHash, 32, pLastHash);
  memcpy(Server + 1536 * 2 - 32, pLastHash, 32);
  delete[] pTempHash;
  delete[] pLastHash;
  //***** DONE BUILDING THE RESPONSE ***//
  /** Send response **/
  DDV_write(&Version, 1, 1, CONN_fd);
  DDV_write(&Server, 1, 3072, CONN_fd);
  snd_cnt+=3073;
  /** Flush, necessary in order to work **/
  //fflush(CONN_fd);
  /** Read and discard C2 **/
  DDV_read(Client, 1, 1536, CONN_fd);
  rec_cnt+=1536;
  return true;
}

#endif
