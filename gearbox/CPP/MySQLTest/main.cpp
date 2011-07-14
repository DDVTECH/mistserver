#include <iostream>
#include <cstdio>
#include <cstdlib>
#include <mysql/mysql.h>
#include <string>

int main( ) {
  if( mysql_library_init( 0, NULL, NULL ) ) {
    std::cerr << "Couldn't connect\n";
  }
  MYSQL * Connection;
  Connection = mysql_init( NULL );
  if( ! Connection ) { std::cout << mysql_error( Connection ) << "\n"; }

std::cout << "Connecting\n";
if( ! ( mysql_real_connect( Connection, "localhost", "pls" , "bitterkoekjespudding", "pls_gearbox", 0, 
NULL, ( CLIENT_MULTI_STATEMENTS | CLIENT_MULTI_RESULTS ) ) ) ) {
  std::cout << mysql_error( Connection ) << "\n";
  exit( 1 );
}


  mysql_query( Connection, "SELECT * FROM pls_gearbox.servers" );

  MYSQL_RES * Result = mysql_store_result( Connection );

  MYSQL_ROW TempRow;

  int i, num_fields;
  num_fields = mysql_num_fields(Result);

  while( (TempRow = mysql_fetch_row( Result)) ) {
    unsigned long *lengths;
    lengths = mysql_fetch_lengths(Result);
    for(i = 0; i < num_fields; i++) {
      printf("[%.*s] ", (int) lengths[i], TempRow[i] ? TempRow[i] : "NULL");
    }
   std::cout << "\n";
  }

  std::cout << "Prepare Statement\n";
  std::string query = "INSERT INTO servers(`name`,`host`,`owner`) VALUES ( '', '', 1 ); SELECT * from servers";
  std::cout << "Querying\n";
  mysql_query( Connection, query.c_str() );
  std::cout << "Trying for result\n";
  Result = mysql_store_result( Connection );
  if( !Result ) { std::cout << mysql_error( Connection ); exit( 1 ); }
  std::cout << "Trying for row\n";
  if ( TempRow = mysql_fetch_row( Result ) ) {
    num_fields = mysql_num_fields(Result);
    unsigned long *lengths;
    lengths = mysql_fetch_lengths(Result);
    if( num_fields > 0 )  {
      std::cout << "Row added at position:";
      printf("%.*s\n", (int) lengths[0], TempRow[0] ? TempRow[0] : "NULL");
    }
  }

  mysql_close( Connection );

  mysql_library_end( );

  return 0;
}
