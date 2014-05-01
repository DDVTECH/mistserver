#include OUTPUTTYPE
#include <mist/config.h>
#include <mist/socket.h>
#include <mist/defines.h>

/*LTS-START*/
#define GEOIPV4 "GeoIP.dat"
#define GEOIPV6 "GeoIPv6.dat"
/*LTS-END*/

int spawnForked(Socket::Connection & S){
  mistOut tmp(S);
  return tmp.run();
}

int main(int argc, char * argv[]) {
  Util::Config conf(argv[0], PACKAGE_VERSION);
  mistOut::init(&conf);
  /*LTS-START*/
  mistOut::geoIP4 = GeoIP_open("/usr/share/GeoIP/" GEOIPV4, GEOIP_STANDARD | GEOIP_CHECK_CACHE);
  if (!mistOut::geoIP4){
    mistOut::geoIP4 = GeoIP_open(GEOIPV4, GEOIP_STANDARD | GEOIP_CHECK_CACHE);
  }
  mistOut::geoIP6 = GeoIP_open("/usr/share/GeoIP/" GEOIPV6, GEOIP_STANDARD | GEOIP_CHECK_CACHE);
  if (!mistOut::geoIP6){
    mistOut::geoIP6 = GeoIP_open(GEOIPV6, GEOIP_STANDARD | GEOIP_CHECK_CACHE);
  }
  if (!mistOut::geoIP4 || !mistOut::geoIP6){
    DEBUG_MSG(DLVL_FAIL, "Could not load all GeoIP databases. %s: %s, %s: %s", GEOIPV4, mistOut::geoIP4?"success":"fail", GEOIPV6, mistOut::geoIP6?"success":"fail");
  }
  /*LTS-END*/
  if (conf.parseArgs(argc, argv)) {
    if (conf.getBool("json")) {
      std::cout << mistOut::capa.toString() << std::endl;
      return -1;
    }
    conf.serveForkedSocket(spawnForked);
  }
  /*LTS-START*/
  GeoIP_delete(mistOut::geoIP4);
  GeoIP_delete(mistOut::geoIP6);
  /*LTS-END*/
  return 0;
}
