struct Handshake {
  char Time[4];
  char Zero[4];
  char Random[1528];
};//Handshake

char * versionstring = "PLSRTMPServer";

void doHandshake(){
  srand(time(NULL));
  char Version;
  Handshake Client;
  Handshake Server;
  /** Read C0 **/
  fread(&(Version), 1, 1, stdin);
  /** Read C1 **/
  fread(Client.Time, 1, 4, stdin);
  fread(Client.Zero, 1, 4, stdin);
  fread(Client.Random, 1, 1528, stdin);
  rec_cnt+=1537;
  /** Build S1 Packet **/
  Server.Time[0] = 0; Server.Time[1] = 0; Server.Time[2] = 0; Server.Time[3] = 0;
  Server.Zero[0] = 0; Server.Zero[1] = 0; Server.Zero[2] = 0; Server.Zero[3] = 0;
  for (int i = 0; i < 1528; i++){
    Server.Random[i] = versionstring[i%13];
  }
  /** Send S0 **/
  fwrite(&(Version), 1, 1, stdout);
  /** Send S1 **/
  fwrite(Server.Time, 1, 4, stdout);
  fwrite(Server.Zero, 1, 4, stdout);
  fwrite(Server.Random, 1, 1528, stdout);
  /** Flush output, just for certainty **/
  fflush(stdout);
  snd_cnt+=1537;
  /** Send S2 **/
  fwrite(Client.Time, 1, 4, stdout);
  fwrite(Client.Time, 1, 4, stdout);
  fwrite(Client.Random, 1, 1528, stdout);
  snd_cnt+=1536;
  /** Flush, necessary in order to work **/
  fflush(stdout);
  /** Read and discard C2 **/
  fread(Client.Time, 1, 4, stdin);
  fread(Client.Zero, 1, 4, stdin);
  fread(Client.Random, 1, 1528, stdin);
  rec_cnt+=1536;
}//doHandshake
