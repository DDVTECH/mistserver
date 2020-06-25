#pragma once

#include <srtp2/srtp.h>
#include <stdint.h>
#include <string>
#include <vector>

#define SRTP_PARSER_MASTER_KEY_LEN 16
#define SRTP_PARSER_MASTER_SALT_LEN 14
#define SRTP_PARSER_MASTER_LEN (SRTP_PARSER_MASTER_KEY_LEN + SRTP_PARSER_MASTER_SALT_LEN)

/* --------------------------------------- */

class SRTPReader{
public:
  SRTPReader();
  int init(const std::string &cipher, const std::string &key, const std::string &salt);
  int shutdown();
  int unprotectRtp(uint8_t *data, int *nbytes); /* `nbytes` should contain the number of bytes in `data`. On success `nbytes`
                                                   will hold the number of bytes of the decoded RTP packet. */
  int unprotectRtcp(uint8_t *data, int *nbytes); /* `nbytes` should contains the number of bytes in `data`. On success `nbytes`
                                                    will hold the number of bytes the decoded RTCP packet. */

private:
  srtp_t session;
  srtp_policy_t policy;
  std::vector<uint8_t> key_salt; /* Combination of key + salt which is used to unprotect the SRTP/SRTCP data. */
};

/* --------------------------------------- */

class SRTPWriter{
public:
  SRTPWriter();
  int init(const std::string &cipher, const std::string &key, const std::string &salt);
  int shutdown();
  int protectRtp(uint8_t *data, int *nbytes);
  int protectRtcp(uint8_t *data, int *nbytes);

private:
  srtp_t session;
  srtp_policy_t policy;
  std::vector<uint8_t> key_salt; /* Combination of key + salt which is used to protect the SRTP/SRTCP data. */
};

/* --------------------------------------- */
