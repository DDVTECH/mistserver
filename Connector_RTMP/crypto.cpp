#define STR(x) (((std::string)(x)).c_str())

#include "crypto.h"

#define P768 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A63A3620FFFFFFFFFFFFFFFF"

#define P1024 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381" \
"FFFFFFFFFFFFFFFF"

#define Q1024 \
"7FFFFFFFFFFFFFFFE487ED5110B4611A62633145C06E0E68" \
"948127044533E63A0105DF531D89CD9128A5043CC71A026E" \
"F7CA8CD9E69D218D98158536F92F8A1BA7F09AB6B6A8E122" \
"F242DABB312F3F637A262174D31BF6B585FFAE5B7A035BF6" \
"F71C35FDAD44CFD2D74F9208BE258FF324943328F67329C0" \
"FFFFFFFFFFFFFFFF"

#define P1536 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF"

#define P2048 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B" \
"E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9" \
"DE2BCBF6955817183995497CEA956AE515D2261898FA0510" \
"15728E5A8AACAA68FFFFFFFFFFFFFFFF"

#define P3072 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B" \
"E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9" \
"DE2BCBF6955817183995497CEA956AE515D2261898FA0510" \
"15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64" \
"ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7" \
"ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B" \
"F12FFA06D98A0864D87602733EC86A64521F2B18177B200C" \
"BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31" \
"43DB5BFCE0FD108E4B82D120A93AD2CAFFFFFFFFFFFFFFFF"

#define P4096 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B" \
"E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9" \
"DE2BCBF6955817183995497CEA956AE515D2261898FA0510" \
"15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64" \
"ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7" \
"ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B" \
"F12FFA06D98A0864D87602733EC86A64521F2B18177B200C" \
"BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31" \
"43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7" \
"88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA" \
"2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6" \
"287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED" \
"1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9" \
"93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934063199" \
"FFFFFFFFFFFFFFFF"

#define P6144 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B" \
"E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9" \
"DE2BCBF6955817183995497CEA956AE515D2261898FA0510" \
"15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64" \
"ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7" \
"ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B" \
"F12FFA06D98A0864D87602733EC86A64521F2B18177B200C" \
"BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31" \
"43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7" \
"88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA" \
"2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6" \
"287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED" \
"1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9" \
"93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934028492" \
"36C3FAB4D27C7026C1D4DCB2602646DEC9751E763DBA37BD" \
"F8FF9406AD9E530EE5DB382F413001AEB06A53ED9027D831" \
"179727B0865A8918DA3EDBEBCF9B14ED44CE6CBACED4BB1B" \
"DB7F1447E6CC254B332051512BD7AF426FB8F401378CD2BF" \
"5983CA01C64B92ECF032EA15D1721D03F482D7CE6E74FEF6" \
"D55E702F46980C82B5A84031900B1C9E59E7C97FBEC7E8F3" \
"23A97A7E36CC88BE0F1D45B7FF585AC54BD407B22B4154AA" \
"CC8F6D7EBF48E1D814CC5ED20F8037E0A79715EEF29BE328" \
"06A1D58BB7C5DA76F550AA3D8A1FBFF0EB19CCB1A313D55C" \
"DA56C9EC2EF29632387FE8D76E3C0468043E8F663F4860EE" \
"12BF2D5B0B7474D6E694F91E6DCC4024FFFFFFFFFFFFFFFF"

#define P8192 \
"FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD1" \
"29024E088A67CC74020BBEA63B139B22514A08798E3404DD" \
"EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245" \
"E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED" \
"EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3D" \
"C2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F" \
"83655D23DCA3AD961C62F356208552BB9ED529077096966D" \
"670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B" \
"E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9" \
"DE2BCBF6955817183995497CEA956AE515D2261898FA0510" \
"15728E5A8AAAC42DAD33170D04507A33A85521ABDF1CBA64" \
"ECFB850458DBEF0A8AEA71575D060C7DB3970F85A6E1E4C7" \
"ABF5AE8CDB0933D71E8C94E04A25619DCEE3D2261AD2EE6B" \
"F12FFA06D98A0864D87602733EC86A64521F2B18177B200C" \
"BBE117577A615D6C770988C0BAD946E208E24FA074E5AB31" \
"43DB5BFCE0FD108E4B82D120A92108011A723C12A787E6D7" \
"88719A10BDBA5B2699C327186AF4E23C1A946834B6150BDA" \
"2583E9CA2AD44CE8DBBBC2DB04DE8EF92E8EFC141FBECAA6" \
"287C59474E6BC05D99B2964FA090C3A2233BA186515BE7ED" \
"1F612970CEE2D7AFB81BDD762170481CD0069127D5B05AA9" \
"93B4EA988D8FDDC186FFB7DC90A6C08F4DF435C934028492" \
"36C3FAB4D27C7026C1D4DCB2602646DEC9751E763DBA37BD" \
"F8FF9406AD9E530EE5DB382F413001AEB06A53ED9027D831" \
"179727B0865A8918DA3EDBEBCF9B14ED44CE6CBACED4BB1B" \
"DB7F1447E6CC254B332051512BD7AF426FB8F401378CD2BF" \
"5983CA01C64B92ECF032EA15D1721D03F482D7CE6E74FEF6" \
"D55E702F46980C82B5A84031900B1C9E59E7C97FBEC7E8F3" \
"23A97A7E36CC88BE0F1D45B7FF585AC54BD407B22B4154AA" \
"CC8F6D7EBF48E1D814CC5ED20F8037E0A79715EEF29BE328" \
"06A1D58BB7C5DA76F550AA3D8A1FBFF0EB19CCB1A313D55C" \
"DA56C9EC2EF29632387FE8D76E3C0468043E8F663F4860EE" \
"12BF2D5B0B7474D6E694F91E6DBE115974A3926F12FEE5E4" \
"38777CB6A932DF8CD8BEC4D073B931BA3BC832B68D9DD300" \
"741FA7BF8AFC47ED2576F6936BA424663AAB639C5AE4F568" \
"3423B4742BF1C978238F16CBE39D652DE3FDB8BEFC848AD9" \
"22222E04A4037C0713EB57A81A23F0C73473FC646CEA306B" \
"4BCBC8862F8385DDFA9D4B7FA2C087E879683303ED5BDD3A" \
"062B3CF5B3A278A66D2A13F83F44F82DDF310EE074AB6A36" \
"4597E899A0255DC164F31CC50846851DF9AB48195DED7EA1" \
"B1D510BD7EE74D73FAF36BC31ECFA268359046F4EB879F92" \
"4009438B481C6CD7889A002ED5EE382BC9190DA6FC026E47" \
"9558E4475677E9AA9E3050E2765694DFC81F56E880B96E71" \
"60C980DD98EDD3DFFFFFFFFFFFFFFFFF"


uint8_t genuineFMSKey[] = {
  0x47, 0x65, 0x6e, 0x75, 0x69, 0x6e, 0x65, 0x20,
  0x41, 0x64, 0x6f, 0x62, 0x65, 0x20, 0x46, 0x6c,
  0x61, 0x73, 0x68, 0x20, 0x4d, 0x65, 0x64, 0x69,
  0x61, 0x20, 0x53, 0x65, 0x72, 0x76, 0x65, 0x72,
  0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Media Server 001
  0xf0, 0xee, 0xc2, 0x4a, 0x80, 0x68, 0xbe, 0xe8,
  0x2e, 0x00, 0xd0, 0xd1, 0x02, 0x9e, 0x7e, 0x57,
  0x6e, 0xec, 0x5d, 0x2d, 0x29, 0x80, 0x6f, 0xab,
  0x93, 0xb8, 0xe6, 0x36, 0xcf, 0xeb, 0x31, 0xae
}; // 68

uint8_t genuineFPKey[] = {
  0x47, 0x65, 0x6E, 0x75, 0x69, 0x6E, 0x65, 0x20,
  0x41, 0x64, 0x6F, 0x62, 0x65, 0x20, 0x46, 0x6C,
  0x61, 0x73, 0x68, 0x20, 0x50, 0x6C, 0x61, 0x79,
  0x65, 0x72, 0x20, 0x30, 0x30, 0x31, // Genuine Adobe Flash Player 001
  0xF0, 0xEE, 0xC2, 0x4A, 0x80, 0x68, 0xBE, 0xE8,
  0x2E, 0x00, 0xD0, 0xD1, 0x02, 0x9E, 0x7E, 0x57,
  0x6E, 0xEC, 0x5D, 0x2D, 0x29, 0x80, 0x6F, 0xAB,
  0x93, 0xB8, 0xE6, 0x36, 0xCF, 0xEB, 0x31, 0xAE
}; // 62


void replace(std::string &target, std::string search, std::string replacement) {
  if (search == replacement)
    return;
  if (search == "")
    return;
  std::string::size_type i = std::string::npos;
  while ((i = target.find(search)) != std::string::npos) {
    target.replace(i, search.length(), replacement);
  }
}


DHWrapper::DHWrapper(int32_t bitsCount) {
    _bitsCount = bitsCount;
    _pDH = NULL;
    _pSharedKey = NULL;
    _sharedKeyLength = 0;
    _peerPublickey = NULL;
}

DHWrapper::~DHWrapper() {
    Cleanup();
}

bool DHWrapper::Initialize() {
    Cleanup();

    //1. Create the DH
    _pDH = DH_new();
    if (_pDH == NULL) {
        Cleanup();
        return false;
    }

    //2. Create his internal p and g
    _pDH->p = BN_new();
    if (_pDH->p == NULL) {
        Cleanup();
        return false;
    }
    _pDH->g = BN_new();
    if (_pDH->g == NULL) {
        Cleanup();
        return false;
    }

    //3. initialize p, g and key length
    if (BN_hex2bn(&_pDH->p, P1024) == 0) {
        Cleanup();
        return false;
    }
    if (BN_set_word(_pDH->g, 2) != 1) {
        Cleanup();
        return false;
    }

    //4. Set the key length
    _pDH->length = _bitsCount;

    //5. Generate private and public key
    if (DH_generate_key(_pDH) != 1) {
        Cleanup();
        return false;
    }

    return true;
}

bool DHWrapper::CopyPublicKey(uint8_t *pDst, int32_t dstLength) {
    if (_pDH == NULL) {
        return false;
    }

    return CopyKey(_pDH->pub_key, pDst, dstLength);
}

bool DHWrapper::CopyPrivateKey(uint8_t *pDst, int32_t dstLength) {
    if (_pDH == NULL) {
        return false;
    }

    return CopyKey(_pDH->priv_key, pDst, dstLength);
}

bool DHWrapper::CreateSharedKey(uint8_t *pPeerPublicKey, int32_t length) {
    if (_pDH == NULL) {
        return false;
    }

    if (_sharedKeyLength != 0 || _pSharedKey != NULL) {
        return false;
    }

    _sharedKeyLength = DH_size(_pDH);
    if (_sharedKeyLength <= 0 || _sharedKeyLength > 1024) {
        return false;
    }
    _pSharedKey = new uint8_t[_sharedKeyLength];

    _peerPublickey = BN_bin2bn(pPeerPublicKey, length, 0);
    if (_peerPublickey == NULL) {
        return false;
    }

    if (DH_compute_key(_pSharedKey, _peerPublickey, _pDH) != _sharedKeyLength) {
        return false;
    }

    return true;
}

bool DHWrapper::CopySharedKey(uint8_t *pDst, int32_t dstLength) {
    if (_pDH == NULL) {
        return false;
    }

    if (dstLength != _sharedKeyLength) {
        return false;
    }

    memcpy(pDst, _pSharedKey, _sharedKeyLength);

    return true;
}

void DHWrapper::Cleanup() {
    if (_pDH != NULL) {
        if (_pDH->p != NULL) {
            BN_free(_pDH->p);
            _pDH->p = NULL;
        }
        if (_pDH->g != NULL) {
            BN_free(_pDH->g);
            _pDH->g = NULL;
        }
        DH_free(_pDH);
        _pDH = NULL;
    }

    if (_pSharedKey != NULL) {
        delete[] _pSharedKey;
        _pSharedKey = NULL;
    }
    _sharedKeyLength = 0;

    if (_peerPublickey != NULL) {
        BN_free(_peerPublickey);
        _peerPublickey = NULL;
    }
}

bool DHWrapper::CopyKey(BIGNUM *pNum, uint8_t *pDst, int32_t dstLength) {
    int32_t keySize = BN_num_bytes(pNum);
    if ((keySize <= 0) || (dstLength <= 0) || (keySize > dstLength)) {
        return false;
    }

    if (BN_bn2bin(pNum, pDst) != keySize) {
        return false;
    }

    return true;
}

void InitRC4Encryption(uint8_t *secretKey, uint8_t *pubKeyIn, uint8_t *pubKeyOut, RC4_KEY *rc4keyIn, RC4_KEY *rc4keyOut) {
    uint8_t digest[SHA256_DIGEST_LENGTH];
    unsigned int digestLen = 0;

    HMAC_CTX ctx;
    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, secretKey, 128, EVP_sha256(), 0);
    HMAC_Update(&ctx, pubKeyIn, 128);
    HMAC_Final(&ctx, digest, &digestLen);
    HMAC_CTX_cleanup(&ctx);

    RC4_set_key(rc4keyOut, 16, digest);

    HMAC_CTX_init(&ctx);
    HMAC_Init_ex(&ctx, secretKey, 128, EVP_sha256(), 0);
    HMAC_Update(&ctx, pubKeyOut, 128);
    HMAC_Final(&ctx, digest, &digestLen);
    HMAC_CTX_cleanup(&ctx);

    RC4_set_key(rc4keyIn, 16, digest);
}

std::string md5(std::string source, bool textResult) {
    EVP_MD_CTX mdctx;
    unsigned char md_value[EVP_MAX_MD_SIZE];
    unsigned int md_len;

    EVP_DigestInit(&mdctx, EVP_md5());
    EVP_DigestUpdate(&mdctx, STR(source), source.length());
    EVP_DigestFinal_ex(&mdctx, md_value, &md_len);
    EVP_MD_CTX_cleanup(&mdctx);

    if (textResult) {
        std::string result = "";
        char tmp[3];
        for (uint32_t i = 0; i < md_len; i++) {
            sprintf(tmp, "%02x", md_value[i]);
            result += tmp;
        }
        return result;
    } else {
        return std::string((char *) md_value, md_len);
    }
}

std::string b64(std::string source) {
    return b64((uint8_t *) STR(source), source.size());
}

std::string b64(uint8_t *pBuffer, uint32_t length) {
    BIO *bmem;
    BIO *b64;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());

    b64 = BIO_push(b64, bmem);
    BIO_write(b64, pBuffer, length);
    std::string result = "";
    if (BIO_flush(b64) == 1) {
        BIO_get_mem_ptr(b64, &bptr);
        result = std::string(bptr->data, bptr->length);
    }

    BIO_free_all(b64);


    replace(result, "\n", "");
    replace(result, "\r", "");

    return result;
}

std::string unb64(std::string source) {
	return unb64((uint8_t *)STR(source),source.length());
}

std::string unb64(uint8_t *pBuffer, uint32_t length){
	// create a memory buffer containing base64 encoded data
    //BIO* bmem = BIO_new_mem_buf((void*) STR(source), source.length());
	BIO* bmem = BIO_new_mem_buf((void *)pBuffer, length);

    // push a Base64 filter so that reading from buffer decodes it
    BIO *bioCmd = BIO_new(BIO_f_base64());
    // we don't want newlines
    BIO_set_flags(bioCmd, BIO_FLAGS_BASE64_NO_NL);
    bmem = BIO_push(bioCmd, bmem);

    char *pOut = new char[length];

    int finalLen = BIO_read(bmem, (void*) pOut, length);
    BIO_free_all(bmem);
    std::string result(pOut, finalLen);
    delete[] pOut;
    return result;
}

void HMACsha256(const void *pData, uint32_t dataLength, const void *pKey, uint32_t keyLength, void *pResult) {
  unsigned int digestLen;
  HMAC_CTX ctx;
  HMAC_CTX_init(&ctx);
  HMAC_Init_ex(&ctx, (unsigned char*) pKey, keyLength, EVP_sha256(), NULL);
  HMAC_Update(&ctx, (unsigned char *) pData, dataLength);
  HMAC_Final(&ctx, (unsigned char *) pResult, &digestLen);
  HMAC_CTX_cleanup(&ctx);
}

uint32_t GetDigestOffset0(uint8_t *pBuffer) {
  uint32_t offset = pBuffer[8] + pBuffer[9] + pBuffer[10] + pBuffer[11];
  return (offset % 728) + 12;
}
uint32_t GetDigestOffset1(uint8_t *pBuffer) {
  uint32_t offset = pBuffer[772] + pBuffer[773] + pBuffer[774] + pBuffer[775];
  return (offset % 728) + 776;
}
uint32_t GetDigestOffset(uint8_t *pBuffer, uint8_t scheme){
  if (scheme == 0){return GetDigestOffset0(pBuffer);}else{return GetDigestOffset1(pBuffer);}
}
uint32_t GetDHOffset0(uint8_t *pBuffer) {
  uint32_t offset = pBuffer[1532] + pBuffer[1533] + pBuffer[1534] + pBuffer[1535];
  return (offset % 632) + 772;
}
uint32_t GetDHOffset1(uint8_t *pBuffer) {
  uint32_t offset = pBuffer[768] + pBuffer[769] + pBuffer[770] + pBuffer[771];
  return (offset % 632) + 8;
}
uint32_t GetDHOffset(uint8_t *pBuffer, uint8_t scheme){
  if (scheme == 0){return GetDHOffset0(pBuffer);}else{return GetDHOffset1(pBuffer);}
}


bool ValidateClientScheme(uint8_t * pBuffer, uint8_t scheme) {
  uint32_t clientDigestOffset = GetDigestOffset(pBuffer, scheme);
  uint8_t *pTempBuffer = new uint8_t[1536 - 32];
  memcpy(pTempBuffer, pBuffer, clientDigestOffset);
  memcpy(pTempBuffer + clientDigestOffset, pBuffer + clientDigestOffset + 32, 1536 - clientDigestOffset - 32);
  uint8_t *pTempHash = new uint8_t[512];
  HMACsha256(pTempBuffer, 1536 - 32, genuineFPKey, 30, pTempHash);
  bool result = (memcmp(pBuffer+clientDigestOffset, pTempHash, 32) == 0);
  #ifdef DEBUG
  fprintf(stderr, "Client scheme validation %hhi %s\n", scheme, result?"success":"failed");
  #endif
  delete[] pTempBuffer;
  delete[] pTempHash;
  return result;
}
