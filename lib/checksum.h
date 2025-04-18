#include <stddef.h>
#include "defines.h"

namespace checksum{
  inline unsigned int crc32c(unsigned int crc, const char *data, size_t len){
    static const unsigned int table[256] ={
        0x00000000U, 0x04C11DB7U, 0x09823B6EU, 0x0D4326D9U, 0x130476DCU, 0x17C56B6BU, 0x1A864DB2U,
        0x1E475005U, 0x2608EDB8U, 0x22C9F00FU, 0x2F8AD6D6U, 0x2B4BCB61U, 0x350C9B64U, 0x31CD86D3U,
        0x3C8EA00AU, 0x384FBDBDU, 0x4C11DB70U, 0x48D0C6C7U, 0x4593E01EU, 0x4152FDA9U, 0x5F15ADACU,
        0x5BD4B01BU, 0x569796C2U, 0x52568B75U, 0x6A1936C8U, 0x6ED82B7FU, 0x639B0DA6U, 0x675A1011U,
        0x791D4014U, 0x7DDC5DA3U, 0x709F7B7AU, 0x745E66CDU, 0x9823B6E0U, 0x9CE2AB57U, 0x91A18D8EU,
        0x95609039U, 0x8B27C03CU, 0x8FE6DD8BU, 0x82A5FB52U, 0x8664E6E5U, 0xBE2B5B58U, 0xBAEA46EFU,
        0xB7A96036U, 0xB3687D81U, 0xAD2F2D84U, 0xA9EE3033U, 0xA4AD16EAU, 0xA06C0B5DU, 0xD4326D90U,
        0xD0F37027U, 0xDDB056FEU, 0xD9714B49U, 0xC7361B4CU, 0xC3F706FBU, 0xCEB42022U, 0xCA753D95U,
        0xF23A8028U, 0xF6FB9D9FU, 0xFBB8BB46U, 0xFF79A6F1U, 0xE13EF6F4U, 0xE5FFEB43U, 0xE8BCCD9AU,
        0xEC7DD02DU, 0x34867077U, 0x30476DC0U, 0x3D044B19U, 0x39C556AEU, 0x278206ABU, 0x23431B1CU,
        0x2E003DC5U, 0x2AC12072U, 0x128E9DCFU, 0x164F8078U, 0x1B0CA6A1U, 0x1FCDBB16U, 0x018AEB13U,
        0x054BF6A4U, 0x0808D07DU, 0x0CC9CDCAU, 0x7897AB07U, 0x7C56B6B0U, 0x71159069U, 0x75D48DDEU,
        0x6B93DDDBU, 0x6F52C06CU, 0x6211E6B5U, 0x66D0FB02U, 0x5E9F46BFU, 0x5A5E5B08U, 0x571D7DD1U,
        0x53DC6066U, 0x4D9B3063U, 0x495A2DD4U, 0x44190B0DU, 0x40D816BAU, 0xACA5C697U, 0xA864DB20U,
        0xA527FDF9U, 0xA1E6E04EU, 0xBFA1B04BU, 0xBB60ADFCU, 0xB6238B25U, 0xB2E29692U, 0x8AAD2B2FU,
        0x8E6C3698U, 0x832F1041U, 0x87EE0DF6U, 0x99A95DF3U, 0x9D684044U, 0x902B669DU, 0x94EA7B2AU,
        0xE0B41DE7U, 0xE4750050U, 0xE9362689U, 0xEDF73B3EU, 0xF3B06B3BU, 0xF771768CU, 0xFA325055U,
        0xFEF34DE2U, 0xC6BCF05FU, 0xC27DEDE8U, 0xCF3ECB31U, 0xCBFFD686U, 0xD5B88683U, 0xD1799B34U,
        0xDC3ABDEDU, 0xD8FBA05AU, 0x690CE0EEU, 0x6DCDFD59U, 0x608EDB80U, 0x644FC637U, 0x7A089632U,
        0x7EC98B85U, 0x738AAD5CU, 0x774BB0EBU, 0x4F040D56U, 0x4BC510E1U, 0x46863638U, 0x42472B8FU,
        0x5C007B8AU, 0x58C1663DU, 0x558240E4U, 0x51435D53U, 0x251D3B9EU, 0x21DC2629U, 0x2C9F00F0U,
        0x285E1D47U, 0x36194D42U, 0x32D850F5U, 0x3F9B762CU, 0x3B5A6B9BU, 0x0315D626U, 0x07D4CB91U,
        0x0A97ED48U, 0x0E56F0FFU, 0x1011A0FAU, 0x14D0BD4DU, 0x19939B94U, 0x1D528623U, 0xF12F560EU,
        0xF5EE4BB9U, 0xF8AD6D60U, 0xFC6C70D7U, 0xE22B20D2U, 0xE6EA3D65U, 0xEBA91BBCU, 0xEF68060BU,
        0xD727BBB6U, 0xD3E6A601U, 0xDEA580D8U, 0xDA649D6FU, 0xC423CD6AU, 0xC0E2D0DDU, 0xCDA1F604U,
        0xC960EBB3U, 0xBD3E8D7EU, 0xB9FF90C9U, 0xB4BCB610U, 0xB07DABA7U, 0xAE3AFBA2U, 0xAAFBE615U,
        0xA7B8C0CCU, 0xA379DD7BU, 0x9B3660C6U, 0x9FF77D71U, 0x92B45BA8U, 0x9675461FU, 0x8832161AU,
        0x8CF30BADU, 0x81B02D74U, 0x857130C3U, 0x5D8A9099U, 0x594B8D2EU, 0x5408ABF7U, 0x50C9B640U,
        0x4E8EE645U, 0x4A4FFBF2U, 0x470CDD2BU, 0x43CDC09CU, 0x7B827D21U, 0x7F436096U, 0x7200464FU,
        0x76C15BF8U, 0x68860BFDU, 0x6C47164AU, 0x61043093U, 0x65C52D24U, 0x119B4BE9U, 0x155A565EU,
        0x18197087U, 0x1CD86D30U, 0x029F3D35U, 0x065E2082U, 0x0B1D065BU, 0x0FDC1BECU, 0x3793A651U,
        0x3352BBE6U, 0x3E119D3FU, 0x3AD08088U, 0x2497D08DU, 0x2056CD3AU, 0x2D15EBE3U, 0x29D4F654U,
        0xC5A92679U, 0xC1683BCEU, 0xCC2B1D17U, 0xC8EA00A0U, 0xD6AD50A5U, 0xD26C4D12U, 0xDF2F6BCBU,
        0xDBEE767CU, 0xE3A1CBC1U, 0xE760D676U, 0xEA23F0AFU, 0xEEE2ED18U, 0xF0A5BD1DU, 0xF464A0AAU,
        0xF9278673U, 0xFDE69BC4U, 0x89B8FD09U, 0x8D79E0BEU, 0x803AC667U, 0x84FBDBD0U, 0x9ABC8BD5U,
        0x9E7D9662U, 0x933EB0BBU, 0x97FFAD0CU, 0xAFB010B1U, 0xAB710D06U, 0xA6322BDFU, 0xA2F33668U,
        0xBCB4666DU, 0xB8757BDAU, 0xB5365D03U, 0xB1F740B4U,
    };

    while (len > 0){
      crc = table[*data ^ ((crc >> 24) & 0xff)] ^ (crc << 8);
      data++;
      len--;
    }
    return crc;
  }

  inline uint32_t crc32LE(uint32_t crc, const char *data, size_t len){
    static const unsigned int table[256] ={
        0x00000000U, 0x77073096U, 0xee0e612cU, 0x990951baU, 0x076dc419U, 0x706af48fU, 0xe963a535U,
        0x9e6495a3U, 0x0edb8832U, 0x79dcb8a4U, 0xe0d5e91eU, 0x97d2d988U, 0x09b64c2bU, 0x7eb17cbdU,
        0xe7b82d07U, 0x90bf1d91U, 0x1db71064U, 0x6ab020f2U, 0xf3b97148U, 0x84be41deU, 0x1adad47dU,
        0x6ddde4ebU, 0xf4d4b551U, 0x83d385c7U, 0x136c9856U, 0x646ba8c0U, 0xfd62f97aU, 0x8a65c9ecU,
        0x14015c4fU, 0x63066cd9U, 0xfa0f3d63U, 0x8d080df5U, 0x3b6e20c8U, 0x4c69105eU, 0xd56041e4U,
        0xa2677172U, 0x3c03e4d1U, 0x4b04d447U, 0xd20d85fdU, 0xa50ab56bU, 0x35b5a8faU, 0x42b2986cU,
        0xdbbbc9d6U, 0xacbcf940U, 0x32d86ce3U, 0x45df5c75U, 0xdcd60dcfU, 0xabd13d59U, 0x26d930acU,
        0x51de003aU, 0xc8d75180U, 0xbfd06116U, 0x21b4f4b5U, 0x56b3c423U, 0xcfba9599U, 0xb8bda50fU,
        0x2802b89eU, 0x5f058808U, 0xc60cd9b2U, 0xb10be924U, 0x2f6f7c87U, 0x58684c11U, 0xc1611dabU,
        0xb6662d3dU, 0x76dc4190U, 0x01db7106U, 0x98d220bcU, 0xefd5102aU, 0x71b18589U, 0x06b6b51fU,
        0x9fbfe4a5U, 0xe8b8d433U, 0x7807c9a2U, 0x0f00f934U, 0x9609a88eU, 0xe10e9818U, 0x7f6a0dbbU,
        0x086d3d2dU, 0x91646c97U, 0xe6635c01U, 0x6b6b51f4U, 0x1c6c6162U, 0x856530d8U, 0xf262004eU,
        0x6c0695edU, 0x1b01a57bU, 0x8208f4c1U, 0xf50fc457U, 0x65b0d9c6U, 0x12b7e950U, 0x8bbeb8eaU,
        0xfcb9887cU, 0x62dd1ddfU, 0x15da2d49U, 0x8cd37cf3U, 0xfbd44c65U, 0x4db26158U, 0x3ab551ceU,
        0xa3bc0074U, 0xd4bb30e2U, 0x4adfa541U, 0x3dd895d7U, 0xa4d1c46dU, 0xd3d6f4fbU, 0x4369e96aU,
        0x346ed9fcU, 0xad678846U, 0xda60b8d0U, 0x44042d73U, 0x33031de5U, 0xaa0a4c5fU, 0xdd0d7cc9U,
        0x5005713cU, 0x270241aaU, 0xbe0b1010U, 0xc90c2086U, 0x5768b525U, 0x206f85b3U, 0xb966d409U,
        0xce61e49fU, 0x5edef90eU, 0x29d9c998U, 0xb0d09822U, 0xc7d7a8b4U, 0x59b33d17U, 0x2eb40d81U,
        0xb7bd5c3bU, 0xc0ba6cadU, 0xedb88320U, 0x9abfb3b6U, 0x03b6e20cU, 0x74b1d29aU, 0xead54739U,
        0x9dd277afU, 0x04db2615U, 0x73dc1683U, 0xe3630b12U, 0x94643b84U, 0x0d6d6a3eU, 0x7a6a5aa8U,
        0xe40ecf0bU, 0x9309ff9dU, 0x0a00ae27U, 0x7d079eb1U, 0xf00f9344U, 0x8708a3d2U, 0x1e01f268U,
        0x6906c2feU, 0xf762575dU, 0x806567cbU, 0x196c3671U, 0x6e6b06e7U, 0xfed41b76U, 0x89d32be0U,
        0x10da7a5aU, 0x67dd4accU, 0xf9b9df6fU, 0x8ebeeff9U, 0x17b7be43U, 0x60b08ed5U, 0xd6d6a3e8U,
        0xa1d1937eU, 0x38d8c2c4U, 0x4fdff252U, 0xd1bb67f1U, 0xa6bc5767U, 0x3fb506ddU, 0x48b2364bU,
        0xd80d2bdaU, 0xaf0a1b4cU, 0x36034af6U, 0x41047a60U, 0xdf60efc3U, 0xa867df55U, 0x316e8eefU,
        0x4669be79U, 0xcb61b38cU, 0xbc66831aU, 0x256fd2a0U, 0x5268e236U, 0xcc0c7795U, 0xbb0b4703U,
        0x220216b9U, 0x5505262fU, 0xc5ba3bbeU, 0xb2bd0b28U, 0x2bb45a92U, 0x5cb36a04U, 0xc2d7ffa7U,
        0xb5d0cf31U, 0x2cd99e8bU, 0x5bdeae1dU, 0x9b64c2b0U, 0xec63f226U, 0x756aa39cU, 0x026d930aU,
        0x9c0906a9U, 0xeb0e363fU, 0x72076785U, 0x05005713U, 0x95bf4a82U, 0xe2b87a14U, 0x7bb12baeU,
        0x0cb61b38U, 0x92d28e9bU, 0xe5d5be0dU, 0x7cdcefb7U, 0x0bdbdf21U, 0x86d3d2d4U, 0xf1d4e242U,
        0x68ddb3f8U, 0x1fda836eU, 0x81be16cdU, 0xf6b9265bU, 0x6fb077e1U, 0x18b74777U, 0x88085ae6U,
        0xff0f6a70U, 0x66063bcaU, 0x11010b5cU, 0x8f659effU, 0xf862ae69U, 0x616bffd3U, 0x166ccf45U,
        0xa00ae278U, 0xd70dd2eeU, 0x4e048354U, 0x3903b3c2U, 0xa7672661U, 0xd06016f7U, 0x4969474dU,
        0x3e6e77dbU, 0xaed16a4aU, 0xd9d65adcU, 0x40df0b66U, 0x37d83bf0U, 0xa9bcae53U, 0xdebb9ec5U,
        0x47b2cf7fU, 0x30b5ffe9U, 0xbdbdf21cU, 0xcabac28aU, 0x53b39330U, 0x24b4a3a6U, 0xbad03605U,
        0xcdd70693U, 0x54de5729U, 0x23d967bfU, 0xb3667a2eU, 0xc4614ab8U, 0x5d681b02U, 0x2a6f2b94U,
        0xb40bbe37U, 0xc30c8ea1U, 0x5a05df1bU, 0x2d02ef8dU};

    while (len > 0){
      crc = table[(*data ^ crc) & 0xff] ^ (crc >> 8);
      data++;
      len--;
    }
    return crc;
  }

  inline unsigned int crc32(unsigned int crc, const char *data, size_t len){
    static const unsigned int table[256] ={
        0x00000000U, 0xB71DC104U, 0x6E3B8209U, 0xD926430DU, 0xDC760413U, 0x6B6BC517U, 0xB24D861AU,
        0x0550471EU, 0xB8ED0826U, 0x0FF0C922U, 0xD6D68A2FU, 0x61CB4B2BU, 0x649B0C35U, 0xD386CD31U,
        0x0AA08E3CU, 0xBDBD4F38U, 0x70DB114CU, 0xC7C6D048U, 0x1EE09345U, 0xA9FD5241U, 0xACAD155FU,
        0x1BB0D45BU, 0xC2969756U, 0x758B5652U, 0xC836196AU, 0x7F2BD86EU, 0xA60D9B63U, 0x11105A67U,
        0x14401D79U, 0xA35DDC7DU, 0x7A7B9F70U, 0xCD665E74U, 0xE0B62398U, 0x57ABE29CU, 0x8E8DA191U,
        0x39906095U, 0x3CC0278BU, 0x8BDDE68FU, 0x52FBA582U, 0xE5E66486U, 0x585B2BBEU, 0xEF46EABAU,
        0x3660A9B7U, 0x817D68B3U, 0x842D2FADU, 0x3330EEA9U, 0xEA16ADA4U, 0x5D0B6CA0U, 0x906D32D4U,
        0x2770F3D0U, 0xFE56B0DDU, 0x494B71D9U, 0x4C1B36C7U, 0xFB06F7C3U, 0x2220B4CEU, 0x953D75CAU,
        0x28803AF2U, 0x9F9DFBF6U, 0x46BBB8FBU, 0xF1A679FFU, 0xF4F63EE1U, 0x43EBFFE5U, 0x9ACDBCE8U,
        0x2DD07DECU, 0x77708634U, 0xC06D4730U, 0x194B043DU, 0xAE56C539U, 0xAB068227U, 0x1C1B4323U,
        0xC53D002EU, 0x7220C12AU, 0xCF9D8E12U, 0x78804F16U, 0xA1A60C1BU, 0x16BBCD1FU, 0x13EB8A01U,
        0xA4F64B05U, 0x7DD00808U, 0xCACDC90CU, 0x07AB9778U, 0xB0B6567CU, 0x69901571U, 0xDE8DD475U,
        0xDBDD936BU, 0x6CC0526FU, 0xB5E61162U, 0x02FBD066U, 0xBF469F5EU, 0x085B5E5AU, 0xD17D1D57U,
        0x6660DC53U, 0x63309B4DU, 0xD42D5A49U, 0x0D0B1944U, 0xBA16D840U, 0x97C6A5ACU, 0x20DB64A8U,
        0xF9FD27A5U, 0x4EE0E6A1U, 0x4BB0A1BFU, 0xFCAD60BBU, 0x258B23B6U, 0x9296E2B2U, 0x2F2BAD8AU,
        0x98366C8EU, 0x41102F83U, 0xF60DEE87U, 0xF35DA999U, 0x4440689DU, 0x9D662B90U, 0x2A7BEA94U,
        0xE71DB4E0U, 0x500075E4U, 0x892636E9U, 0x3E3BF7EDU, 0x3B6BB0F3U, 0x8C7671F7U, 0x555032FAU,
        0xE24DF3FEU, 0x5FF0BCC6U, 0xE8ED7DC2U, 0x31CB3ECFU, 0x86D6FFCBU, 0x8386B8D5U, 0x349B79D1U,
        0xEDBD3ADCU, 0x5AA0FBD8U, 0xEEE00C69U, 0x59FDCD6DU, 0x80DB8E60U, 0x37C64F64U, 0x3296087AU,
        0x858BC97EU, 0x5CAD8A73U, 0xEBB04B77U, 0x560D044FU, 0xE110C54BU, 0x38368646U, 0x8F2B4742U,
        0x8A7B005CU, 0x3D66C158U, 0xE4408255U, 0x535D4351U, 0x9E3B1D25U, 0x2926DC21U, 0xF0009F2CU,
        0x471D5E28U, 0x424D1936U, 0xF550D832U, 0x2C769B3FU, 0x9B6B5A3BU, 0x26D61503U, 0x91CBD407U,
        0x48ED970AU, 0xFFF0560EU, 0xFAA01110U, 0x4DBDD014U, 0x949B9319U, 0x2386521DU, 0x0E562FF1U,
        0xB94BEEF5U, 0x606DADF8U, 0xD7706CFCU, 0xD2202BE2U, 0x653DEAE6U, 0xBC1BA9EBU, 0x0B0668EFU,
        0xB6BB27D7U, 0x01A6E6D3U, 0xD880A5DEU, 0x6F9D64DAU, 0x6ACD23C4U, 0xDDD0E2C0U, 0x04F6A1CDU,
        0xB3EB60C9U, 0x7E8D3EBDU, 0xC990FFB9U, 0x10B6BCB4U, 0xA7AB7DB0U, 0xA2FB3AAEU, 0x15E6FBAAU,
        0xCCC0B8A7U, 0x7BDD79A3U, 0xC660369BU, 0x717DF79FU, 0xA85BB492U, 0x1F467596U, 0x1A163288U,
        0xAD0BF38CU, 0x742DB081U, 0xC3307185U, 0x99908A5DU, 0x2E8D4B59U, 0xF7AB0854U, 0x40B6C950U,
        0x45E68E4EU, 0xF2FB4F4AU, 0x2BDD0C47U, 0x9CC0CD43U, 0x217D827BU, 0x9660437FU, 0x4F460072U,
        0xF85BC176U, 0xFD0B8668U, 0x4A16476CU, 0x93300461U, 0x242DC565U, 0xE94B9B11U, 0x5E565A15U,
        0x87701918U, 0x306DD81CU, 0x353D9F02U, 0x82205E06U, 0x5B061D0BU, 0xEC1BDC0FU, 0x51A69337U,
        0xE6BB5233U, 0x3F9D113EU, 0x8880D03AU, 0x8DD09724U, 0x3ACD5620U, 0xE3EB152DU, 0x54F6D429U,
        0x7926A9C5U, 0xCE3B68C1U, 0x171D2BCCU, 0xA000EAC8U, 0xA550ADD6U, 0x124D6CD2U, 0xCB6B2FDFU,
        0x7C76EEDBU, 0xC1CBA1E3U, 0x76D660E7U, 0xAFF023EAU, 0x18EDE2EEU, 0x1DBDA5F0U, 0xAAA064F4U,
        0x738627F9U, 0xC49BE6FDU, 0x09FDB889U, 0xBEE0798DU, 0x67C63A80U, 0xD0DBFB84U, 0xD58BBC9AU,
        0x62967D9EU, 0xBBB03E93U, 0x0CADFF97U, 0xB110B0AFU, 0x060D71ABU, 0xDF2B32A6U, 0x6836F3A2U,
        0x6D66B4BCU, 0xDA7B75B8U, 0x035D36B5U, 0xB440F7B1U};

    const char *tmpData = data;
    const char *end = tmpData + len;
    while (tmpData < end){crc = table[((unsigned char)crc) ^ *tmpData++] ^ (crc >> 8);}
    return crc;
  }

  inline unsigned int crc16(unsigned int crc, const char *data, size_t len){
    static const unsigned short table[] = {
      0x0000, 0x8005, 0x800F, 0x000A, 0x801B, 0x001E, 0x0014, 0x8011,
      0x8033, 0x0036, 0x003C, 0x8039, 0x0028, 0x802D, 0x8027, 0x0022,
      0x8063, 0x0066, 0x006C, 0x8069, 0x0078, 0x807D, 0x8077, 0x0072,
      0x0050, 0x8055, 0x805F, 0x005A, 0x804B, 0x004E, 0x0044, 0x8041,
      0x80C3, 0x00C6, 0x00CC, 0x80C9, 0x00D8, 0x80DD, 0x80D7, 0x00D2,
      0x00F0, 0x80F5, 0x80FF, 0x00FA, 0x80EB, 0x00EE, 0x00E4, 0x80E1,
      0x00A0, 0x80A5, 0x80AF, 0x00AA, 0x80BB, 0x00BE, 0x00B4, 0x80B1,
      0x8093, 0x0096, 0x009C, 0x8099, 0x0088, 0x808D, 0x8087, 0x0082,
      0x8183, 0x0186, 0x018C, 0x8189, 0x0198, 0x819D, 0x8197, 0x0192,
      0x01B0, 0x81B5, 0x81BF, 0x01BA, 0x81AB, 0x01AE, 0x01A4, 0x81A1,
      0x01E0, 0x81E5, 0x81EF, 0x01EA, 0x81FB, 0x01FE, 0x01F4, 0x81F1,
      0x81D3, 0x01D6, 0x01DC, 0x81D9, 0x01C8, 0x81CD, 0x81C7, 0x01C2,
      0x0140, 0x8145, 0x814F, 0x014A, 0x815B, 0x015E, 0x0154, 0x8151,
      0x8173, 0x0176, 0x017C, 0x8179, 0x0168, 0x816D, 0x8167, 0x0162,
      0x8123, 0x0126, 0x012C, 0x8129, 0x0138, 0x813D, 0x8137, 0x0132,
      0x0110, 0x8115, 0x811F, 0x011A, 0x810B, 0x010E, 0x0104, 0x8101,
      0x8303, 0x0306, 0x030C, 0x8309, 0x0318, 0x831D, 0x8317, 0x0312,
      0x0330, 0x8335, 0x833F, 0x033A, 0x832B, 0x032E, 0x0324, 0x8321,
      0x0360, 0x8365, 0x836F, 0x036A, 0x837B, 0x037E, 0x0374, 0x8371,
      0x8353, 0x0356, 0x035C, 0x8359, 0x0348, 0x834D, 0x8347, 0x0342,
      0x03C0, 0x83C5, 0x83CF, 0x03CA, 0x83DB, 0x03DE, 0x03D4, 0x83D1,
      0x83F3, 0x03F6, 0x03FC, 0x83F9, 0x03E8, 0x83ED, 0x83E7, 0x03E2,
      0x83A3, 0x03A6, 0x03AC, 0x83A9, 0x03B8, 0x83BD, 0x83B7, 0x03B2,
      0x0390, 0x8395, 0x839F, 0x039A, 0x838B, 0x038E, 0x0384, 0x8381,
      0x0280, 0x8285, 0x828F, 0x028A, 0x829B, 0x029E, 0x0294, 0x8291,
      0x82B3, 0x02B6, 0x02BC, 0x82B9, 0x02A8, 0x82AD, 0x82A7, 0x02A2,
      0x82E3, 0x02E6, 0x02EC, 0x82E9, 0x02F8, 0x82FD, 0x82F7, 0x02F2,
      0x02D0, 0x82D5, 0x82DF, 0x02DA, 0x82CB, 0x02CE, 0x02C4, 0x82C1,
      0x8243, 0x0246, 0x024C, 0x8249, 0x0258, 0x825D, 0x8257, 0x0252,
      0x0270, 0x8275, 0x827F, 0x027A, 0x826B, 0x026E, 0x0264, 0x8261,
      0x0220, 0x8225, 0x822F, 0x022A, 0x823B, 0x023E, 0x0234, 0x8231,
      0x8213, 0x0216, 0x021C, 0x8219, 0x0208, 0x820D, 0x8207, 0x0202
  };


  const char *tmpData= data;
  const char *end = tmpData+len;
  while(tmpData < end){
    crc = (crc << 8) ^ table[((crc >> 8) ^ *(char*)tmpData++) & 0x00ff];
  }
 
  return crc;
}

  inline unsigned int crc8(unsigned int crc, const char *data, size_t len){

  // crc table for x8+ x2+ x1+ x0 polynomial
  static const uint8_t crc_table[] = {
   0x00, 0x07, 0x0e, 0x09, 0x1c, 0x1b, 0x12, 0x15, 0x38, 0x3f, 0x36, 0x31,
   0x24, 0x23, 0x2a, 0x2d, 0x70, 0x77, 0x7e, 0x79, 0x6c, 0x6b, 0x62, 0x65,
   0x48, 0x4f, 0x46, 0x41, 0x54, 0x53, 0x5a, 0x5d, 0xe0, 0xe7, 0xee, 0xe9,
   0xfc, 0xfb, 0xf2, 0xf5, 0xd8, 0xdf, 0xd6, 0xd1, 0xc4, 0xc3, 0xca, 0xcd,
   0x90, 0x97, 0x9e, 0x99, 0x8c, 0x8b, 0x82, 0x85, 0xa8, 0xaf, 0xa6, 0xa1,
   0xb4, 0xb3, 0xba, 0xbd, 0xc7, 0xc0, 0xc9, 0xce, 0xdb, 0xdc, 0xd5, 0xd2,
   0xff, 0xf8, 0xf1, 0xf6, 0xe3, 0xe4, 0xed, 0xea, 0xb7, 0xb0, 0xb9, 0xbe,
   0xab, 0xac, 0xa5, 0xa2, 0x8f, 0x88, 0x81, 0x86, 0x93, 0x94, 0x9d, 0x9a,
   0x27, 0x20, 0x29, 0x2e, 0x3b, 0x3c, 0x35, 0x32, 0x1f, 0x18, 0x11, 0x16,
   0x03, 0x04, 0x0d, 0x0a, 0x57, 0x50, 0x59, 0x5e, 0x4b, 0x4c, 0x45, 0x42,
   0x6f, 0x68, 0x61, 0x66, 0x73, 0x74, 0x7d, 0x7a, 0x89, 0x8e, 0x87, 0x80,
   0x95, 0x92, 0x9b, 0x9c, 0xb1, 0xb6, 0xbf, 0xb8, 0xad, 0xaa, 0xa3, 0xa4,
   0xf9, 0xfe, 0xf7, 0xf0, 0xe5, 0xe2, 0xeb, 0xec, 0xc1, 0xc6, 0xcf, 0xc8,
   0xdd, 0xda, 0xd3, 0xd4, 0x69, 0x6e, 0x67, 0x60, 0x75, 0x72, 0x7b, 0x7c,
   0x51, 0x56, 0x5f, 0x58, 0x4d, 0x4a, 0x43, 0x44, 0x19, 0x1e, 0x17, 0x10,
   0x05, 0x02, 0x0b, 0x0c, 0x21, 0x26, 0x2f, 0x28, 0x3d, 0x3a, 0x33, 0x34,
   0x4e, 0x49, 0x40, 0x47, 0x52, 0x55, 0x5c, 0x5b, 0x76, 0x71, 0x78, 0x7f,
   0x6a, 0x6d, 0x64, 0x63, 0x3e, 0x39, 0x30, 0x37, 0x22, 0x25, 0x2c, 0x2b,
   0x06, 0x01, 0x08, 0x0f, 0x1a, 0x1d, 0x14, 0x13, 0xae, 0xa9, 0xa0, 0xa7,
   0xb2, 0xb5, 0xbc, 0xbb, 0x96, 0x91, 0x98, 0x9f, 0x8a, 0x8d, 0x84, 0x83,
   0xde, 0xd9, 0xd0, 0xd7, 0xc2, 0xc5, 0xcc, 0xcb, 0xe6, 0xe1, 0xe8, 0xef,
   0xfa, 0xfd, 0xf4, 0xf3
  };

  const char *tmpData = data;
  const char *end = tmpData + len;
  while (tmpData < end){
    crc = crc_table[(*tmpData++) ^ (unsigned char)crc] ^ ((crc << 8) & 0xff);
  }
  return crc;
}



}// namespace checksum
