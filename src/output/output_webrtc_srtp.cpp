#include <mist/defines.h>
#include "output_webrtc_srtp.h"
#include <algorithm>
#include <string.h>

/* --------------------------------------- */

static std::string srtp_status_to_string(uint32_t status);

/* --------------------------------------- */

SRTPReader::SRTPReader(){
  memset((void *)&session, 0x00, sizeof(session));
  memset((void *)&policy, 0x00, sizeof(policy));
}

/*
  Before initializing the srtp library we shut it down first
  because initializing the library twice results in an error.
*/
int SRTPReader::init(const std::string &cipher, const std::string &key, const std::string &salt){

  int r = 0;
  srtp_err_status_t status = srtp_err_status_ok;
  srtp_profile_t profile;
  memset((void *)&profile, 0x00, sizeof(profile));

  /* validate input */
  if (cipher.empty()){
    FAIL_MSG("Given `cipher` is empty.");
    r = -1;
    goto error;
  }
  if (key.empty()){
    FAIL_MSG("Given `key` is empty.");
    r = -2;
    goto error;
  }
  if (salt.empty()){
    FAIL_MSG("Given `salt` is empty.");
    r = -3;
    goto error;
  }

  /* re-initialize the srtp library. */
  status = srtp_shutdown();
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to shutdown the srtp lib %s", srtp_status_to_string(status).c_str());
    r = -1;
    goto error;
  }

  status = srtp_init();
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to initialize the SRTP library. %s", srtp_status_to_string(status).c_str());
    r = -2;
    goto error;
  }

  /* select the right profile from exchanged cipher */
  if ("SRTP_AES128_CM_SHA1_80" == cipher){
    profile = srtp_profile_aes128_cm_sha1_80;
  }else if ("SRTP_AES128_CM_SHA1_32" == cipher){
    profile = srtp_profile_aes128_cm_sha1_32;
  }else{
    ERROR_MSG("Unsupported SRTP cipher used: %s.", cipher.c_str());
    r = -2;
    goto error;
  }

  /* set the crypto policy using the profile. */
  status = srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, profile);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to set the crypto policy for RTP for cipher %s.", cipher.c_str());
    r = -3;
    goto error;
  }

  status = srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp, profile);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to set the crypto policy for RTCP for cipher %s.", cipher.c_str());
    r = -4;
    goto error;
  }

  /* set the keying material. */
  std::copy(key.begin(), key.end(), std::back_inserter(key_salt));
  std::copy(salt.begin(), salt.end(), std::back_inserter(key_salt));
  policy.key = (unsigned char *)&key_salt[0];

  /* only unprotecting data for now, so using inbound; and some other settings. */
  policy.ssrc.type = ssrc_any_inbound;
  policy.window_size = 1024;
  policy.allow_repeat_tx = 1;

  /* create the srtp session. */
  status = srtp_create(&session, &policy);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to initialize our SRTP session. Status: %s. ", srtp_status_to_string(status).c_str());
    r = -3;
    goto error;
  }

error:
  if (r < 0){shutdown();}

  return r;
}

int SRTPReader::shutdown(){

  int r = 0;

  if (session){
    srtp_err_status_t status = srtp_dealloc(session);
    if (srtp_err_status_ok != status){
      ERROR_MSG("Failed to cleanly shutdown the SRTP session. Status: %s",
                srtp_status_to_string(status).c_str());
      r -= 5;
    }
  }

  memset((void *)&policy, 0x00, sizeof(policy));
  memset((char *)&session, 0x00, sizeof(session));

  return r;
}

/* --------------------------------------- */

int SRTPReader::unprotectRtp(uint8_t *data, int *nbytes){

  if (NULL == data){
    ERROR_MSG("Cannot unprotect the given SRTP, because data is NULL.");
    return -1;
  }

  if (NULL == nbytes){
    ERROR_MSG("Cannot unprotect the given SRTP, becuase nbytes is NULL.");
    return -2;
  }

  if (0 == (*nbytes)){
    ERROR_MSG("Cannot unprotect the given SRTP, because nbytes is 0.");
    return -3;
  }

  if (NULL == policy.key){
    ERROR_MSG("Cannot unprotect the SRTP packet, it seems we're not initialized.");
    return -4;
  }

  srtp_err_status_t status = srtp_unprotect(session, data, nbytes);
  if (srtp_err_status_ok != status && status != srtp_err_status_replay_fail){
    ERROR_MSG("Failed to unprotect the given SRTP. %s.", srtp_status_to_string(status).c_str());
    return -5;
  }
  //For replayed packets, set bytes to zero
  if (status == srtp_err_status_replay_fail){*nbytes = 0;}

  DONTEVEN_MSG("Unprotected SRTP into %d bytes.", *nbytes);

  return 0;
}

int SRTPReader::unprotectRtcp(uint8_t *data, int *nbytes){

  if (NULL == data){
    ERROR_MSG("Cannot unprotect the given SRTCP, because data is NULL.");
    return -1;
  }

  if (NULL == nbytes){
    ERROR_MSG("Cannot unprotect the given SRTCP, becuase nbytes is NULL.");
    return -2;
  }

  if (0 == (*nbytes)){
    ERROR_MSG("Cannot unprotect the given SRTCP, because nbytes is 0.");
    return -3;
  }

  if (NULL == policy.key){
    ERROR_MSG("Cannot unprotect the SRTCP packet, it seems we're not initialized.");
    return -4;
  }

  srtp_err_status_t status = srtp_unprotect_rtcp(session, data, nbytes);
  if (srtp_err_status_ok != status && status != srtp_err_status_replay_fail){
    ERROR_MSG("Failed to unprotect the given SRTCP. %s.", srtp_status_to_string(status).c_str());
    return -5;
  }
  //For replayed packets, set bytes to zero
  if (status == srtp_err_status_replay_fail){*nbytes = 0;}

  return 0;
}

/* --------------------------------------- */

SRTPWriter::SRTPWriter(){
  memset((void *)&session, 0x00, sizeof(session));
  memset((void *)&policy, 0x00, sizeof(policy));
}

/*
  Before initializing the srtp library we shut it down first
  because initializing the library twice results in an error.
*/
int SRTPWriter::init(const std::string &cipher, const std::string &key, const std::string &salt){

  int r = 0;
  srtp_err_status_t status = srtp_err_status_ok;
  srtp_profile_t profile;
  memset((void *)&profile, 0x00, sizeof(profile));

  /* validate input */
  if (cipher.empty()){
    FAIL_MSG("Given `cipher` is empty.");
    r = -1;
    goto error;
  }
  if (key.empty()){
    FAIL_MSG("Given `key` is empty.");
    r = -2;
    goto error;
  }
  if (salt.empty()){
    FAIL_MSG("Given `salt` is empty.");
    r = -3;
    goto error;
  }

  /* re-initialize the srtp library. */
  status = srtp_shutdown();
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to shutdown the srtp lib %s", srtp_status_to_string(status).c_str());
    r = -1;
    goto error;
  }

  status = srtp_init();
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to initialize the SRTP library. %s", srtp_status_to_string(status).c_str());
    r = -2;
    goto error;
  }

  /* select the exchanged cipher */
  if ("SRTP_AES128_CM_SHA1_80" == cipher){
    profile = srtp_profile_aes128_cm_sha1_80;
  }else if ("SRTP_AES128_CM_SHA1_32" == cipher){
    profile = srtp_profile_aes128_cm_sha1_32;
  }else{
    ERROR_MSG("Unsupported SRTP cipher used: %s.", cipher.c_str());
    r = -2;
    goto error;
  }

  /* set the crypto policy using the profile. */
  status = srtp_crypto_policy_set_from_profile_for_rtp(&policy.rtp, profile);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to set the crypto policy for RTP for cipher %s.", cipher.c_str());
    r = -3;
    goto error;
  }

  status = srtp_crypto_policy_set_from_profile_for_rtcp(&policy.rtcp, profile);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to set the crypto policy for RTCP for cipher %s.", cipher.c_str());
    r = -4;
    goto error;
  }

  /* set the keying material. */
  std::copy(key.begin(), key.end(), std::back_inserter(key_salt));
  std::copy(salt.begin(), salt.end(), std::back_inserter(key_salt));
  policy.key = (unsigned char *)&key_salt[0];

  /* only unprotecting data for now, so using inbound; and some other settings. */
  policy.ssrc.type = ssrc_any_outbound;
  policy.window_size = 128;
  policy.allow_repeat_tx = 0;

  /* create the srtp session. */
  status = srtp_create(&session, &policy);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to initialize our SRTP session. Status: %s. ", srtp_status_to_string(status).c_str());
    r = -3;
    goto error;
  }

error:
  if (r < 0){shutdown();}

  return r;
}

int SRTPWriter::shutdown(){

  int r = 0;
  
  if (session){
    srtp_err_status_t status = srtp_dealloc(session);
    if (srtp_err_status_ok != status){
      ERROR_MSG("Failed to cleanly shutdown the SRTP session. Status: %s",
                srtp_status_to_string(status).c_str());
      r -= 5;
    }
  }

  memset((char *)&policy, 0x00, sizeof(policy));
  memset((char *)&session, 0x00, sizeof(session));

  return r;
}

/* --------------------------------------- */

int SRTPWriter::protectRtp(uint8_t *data, int *nbytes){

  if (NULL == data){
    ERROR_MSG("Cannot protect the RTP packet because given data is NULL.");
    return -1;
  }

  if (NULL == nbytes){
    ERROR_MSG("Cannot protect the RTP packet because the given nbytes is NULL.");
    return -2;
  }

  if ((*nbytes) <= 0){
    ERROR_MSG("Cannot protect the RTP packet because the given nbytes has a value <= 0.");
    return -3;
  }

  if (NULL == policy.key){
    ERROR_MSG("Cannot protect the RTP packet because we're not initialized.");
    return -4;
  }

  srtp_err_status_t status = srtp_protect(session, (void *)data, nbytes);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to protect the RTP packet. %s.", srtp_status_to_string(status).c_str());
    return -5;
  }

  return 0;
}

/*
   Make sure that `data` has `SRTP_MAX_TRAILER_LEN + 4` number
   of bytes at the into which libsrtp can write the
   authentication tag
*/
int SRTPWriter::protectRtcp(uint8_t *data, int *nbytes){

  if (NULL == data){
    ERROR_MSG("Cannot protect the RTCP packet because given data is NULL.");
    return -1;
  }

  if (NULL == nbytes){
    ERROR_MSG("Cannot protect the RTCP packet because nbytes is NULL.");
    return -2;
  }

  if ((*nbytes) <= 0){
    ERROR_MSG("Cannot protect the RTCP packet because *nbytes is <= 0.");
    return -3;
  }

  if (NULL == policy.key){
    ERROR_MSG("Not initialized cannot protect the RTCP packet.");
    return -4;
  }

  srtp_err_status_t status = srtp_protect_rtcp(session, (void *)data, nbytes);
  if (srtp_err_status_ok != status){
    ERROR_MSG("Failed to protect the RTCP packet. %s.", srtp_status_to_string(status).c_str());
    return -3;
  }

  return 0;
}

/* --------------------------------------- */

static std::string srtp_status_to_string(uint32_t status){

  switch (status){
  case srtp_err_status_ok:{
    return "srtp_err_status_ok";
  }
  case srtp_err_status_fail:{
    return "srtp_err_status_fail";
  }
  case srtp_err_status_bad_param:{
    return "srtp_err_status_bad_param";
  }
  case srtp_err_status_alloc_fail:{
    return "srtp_err_status_alloc_fail";
  }
  case srtp_err_status_dealloc_fail:{
    return "srtp_err_status_dealloc_fail";
  }
  case srtp_err_status_init_fail:{
    return "srtp_err_status_init_fail";
  }
  case srtp_err_status_terminus:{
    return "srtp_err_status_terminus";
  }
  case srtp_err_status_auth_fail:{
    return "srtp_err_status_auth_fail";
  }
  case srtp_err_status_cipher_fail:{
    return "srtp_err_status_cipher_fail";
  }
  case srtp_err_status_replay_fail:{
    return "srtp_err_status_replay_fail";
  }
  case srtp_err_status_replay_old:{
    return "srtp_err_status_replay_old";
  }
  case srtp_err_status_algo_fail:{
    return "srtp_err_status_algo_fail";
  }
  case srtp_err_status_no_such_op:{
    return "srtp_err_status_no_such_op";
  }
  case srtp_err_status_no_ctx:{
    return "srtp_err_status_no_ctx";
  }
  case srtp_err_status_cant_check:{
    return "srtp_err_status_cant_check";
  }
  case srtp_err_status_key_expired:{
    return "srtp_err_status_key_expired";
  }
  case srtp_err_status_socket_err:{
    return "srtp_err_status_socket_err";
  }
  case srtp_err_status_signal_err:{
    return "srtp_err_status_signal_err";
  }
  case srtp_err_status_nonce_bad:{
    return "srtp_err_status_nonce_bad";
  }
  case srtp_err_status_read_fail:{
    return "srtp_err_status_read_fail";
  }
  case srtp_err_status_write_fail:{
    return "srtp_err_status_write_fail";
  }
  case srtp_err_status_parse_err:{
    return "srtp_err_status_parse_err";
  }
  case srtp_err_status_encode_err:{
    return "srtp_err_status_encode_err";
  }
  case srtp_err_status_semaphore_err:{
    return "srtp_err_status_semaphore_err";
  }
  case srtp_err_status_pfkey_err:{
    return "srtp_err_status_pfkey_err";
  }
  case srtp_err_status_bad_mki:{
    return "srtp_err_status_bad_mki";
  }
  case srtp_err_status_pkt_idx_old:{
    return "srtp_err_status_pkt_idx_old";
  }
  case srtp_err_status_pkt_idx_adv:{
    return "srtp_err_status_pkt_idx_adv";
  }
  default:{
    return "UNKNOWN";
  }
  }
}

/* --------------------------------------- */
