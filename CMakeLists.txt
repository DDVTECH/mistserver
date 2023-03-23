########################################
# Basic Setup                          #
########################################
cmake_minimum_required (VERSION 2.6)
SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Usr install prefix")
project (MistServer)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=gnu++98")

SET(SOURCE_DIR ${PROJECT_SOURCE_DIR})
SET(BINARY_DIR ${PROJECT_BINARY_DIR})
set( CMAKE_EXPORT_COMPILE_COMMANDS ON ) #For YCM support
include_directories(${SOURCE_DIR})
include_directories(${BINARY_DIR} ${BINARY_DIR}/generated)

option(BUILD_SHARED_LIBS "Build the libraries as shared (default = static)")

########################################
# Testing - Enable Tests               #
########################################
enable_testing()
include(CTest)

########################################
# Build Variables - Install Prefix     #
########################################
if (NOT CMAKE_INSTALL_PREFIX)
  set (CMAKE_INSTALL_PREFIX /usr)
endif()

########################################
# Build Variables - Release            #
########################################
if (RELEASE)
  set (RELEASE_RAW ${RELEASE})
else()
#get the bitlength of this system
  execute_process(COMMAND getconf LONG_BIT OUTPUT_VARIABLE RELEASE_RAW )
  set(RELEASE_RAW "Generic_${RELEASE_RAW}" ) 
endif()
string(STRIP ${RELEASE_RAW} RELEASE)
set(RELEASE \"${RELEASE}\" )

########################################
# Build Variables - Package Version    #
########################################
#See if we have a git repo to get the version from
execute_process(COMMAND git describe --tags OUTPUT_VARIABLE PACKAGE_VERSION_RAW ERROR_QUIET)
if (NOT PACKAGE_VERSION_RAW)
  execute_process(COMMAND cat VERSION OUTPUT_VARIABLE PACKAGE_VERSION_RAW ERROR_QUIET)
endif()
if (NOT PACKAGE_VERSION_RAW)
  set(PACKAGE_VERSION_RAW Unknown)
endif()
#strip off the trailing spaces and newline
string(STRIP "${PACKAGE_VERSION_RAW}" PACKAGE_VERSION)
set(PACKAGE_VERSION \"${PACKAGE_VERSION}\" ) 

########################################
# Build Variables - Everything else    #
########################################
if (NOT DEBUG)
  set(DEBUG 4)
endif()

option(NOSHM "Disabled shared memory (falling back to shared temporary files)")
if (NOT NOSHM)
  add_definitions(-DSHM_ENABLED=1)
else()
  message("Shared memory use is turned OFF")
endif()

if (FILLER_DATA AND SHARED_SECRET AND SUPER_SECRET)
add_definitions(-DFILLER_DATA="${FILLER_DATA}" -DSHARED_SECRET="${SHARED_SECRET}" -DSUPER_SECRET="${SUPER_SECRET}")#LTS
endif()

option(NOSSL "Disable SSL/TLS support")
if (NOT NOSSL)
  add_definitions(-DSSL=1)
else()
  message("SSL/TLS support is turned OFF")
endif()

if (DEFINED DATASIZE )
  add_definitions(-DSHM_DATASIZE=${DATASIZE})
endif()

if (DEFINED STAT_CUTOFF )
  add_definitions(-DSTAT_CUTOFF=${STAT_CUTOFF})
endif()

option(NOUPDATE "Disable the updater")
if (NOT NOUPDATE)
  add_definitions(-DUPDATER=1)
endif()

option(NOAUTH "Disable API authentication entirely (insecure!)")
if (NOAUTH)
  add_definitions(-DNOAUTH=1)
endif()

if (DEFINED UDP_API_HOST )
  add_definitions(-DUDP_API_HOST=${UDP_API_HOST})
endif()

if (DEFINED UDP_API_PORT )
  add_definitions(-DUDP_API_PORT=${UDP_API_PORT})
endif()

set(APPNAME "MistServer" CACHE STRING "Name of the application, as used in user agent strings and the like")
add_definitions(-DAPPNAME="${APPNAME}")

set(DISKSERIAL "" CACHE STRING "Lock application to given disk serial number")
if (DISKSERIAL)
  add_definitions(-DDISKSERIAL="${DISKSERIAL}")
endif()

option(WITH_THREADNAMES "Enable fancy names for threads (not supported on all platforms)")
if (WITH_THREADNAMES)
  add_definitions(-DWITH_THREADNAMES=1)
endif()

if (DEFINED STATS_DELAY)
  add_definitions(-DSTATS_DELAY=${STATS_DELAY})
endif()

option(NOLLHLS "Disable LLHLS")
if (NOLLHLS)
  add_definitions(-DNOLLHLS=1)
endif()

########################################
# Build Variables - Prepare for Build  #
########################################
message("Builing release ${RELEASE} for version ${PACKAGE_VERSION} @ debug level ${DEBUG}")
add_definitions(-g -funsigned-char -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DDEBUG=${DEBUG} -DPACKAGE_VERSION=${PACKAGE_VERSION} -DRELEASE=${RELEASE})
add_definitions(-Wall -Wno-sign-compare -Wparentheses)

option(NOSRT "Disable building native SRT support, regardless of library being present (by default SRT is enabled if libraries are installed)")
if (NOT NOSRT)
  find_library(SRT_LIB srt)
  if(SRT_LIB)
    add_definitions(-DWITH_SRT=1)
    message("Building with SRT")
  else()
    message("Building without native SRT support")
  endif()
else()
  message("Building without native SRT support")
endif()

option(NORIST "Disable building RIST support, regardless of library being present (by default RIST is enabled if libraries are installed)")
if (NOT NORIST)
  find_library(RIST_LIB rist)
  if(RIST_LIB)
    add_definitions(-DWITH_RIST=1)
    message("Building with RIST")
  else()
    message("Building without RIST support")
  endif()
else()
  message("Building without RIST support")
endif()


########################################
# MistLib - Header Files               #
########################################
set(libHeaders
  lib/adts.h
  lib/amf.h
  lib/auth.h
  lib/encode.h
  lib/bitfields.h
  lib/bitstream.h
  lib/certificate.h
  lib/checksum.h
  lib/cmaf.h
  lib/comms.h
  lib/config.h
  lib/defines.h
  lib/dtls_srtp_handshake.h
  lib/dtsc.h
  lib/encryption.h
  lib/flac.h
  lib/flv_tag.h
  lib/h264.h
  lib/h265.h
  lib/hls_support.h
  lib/http_parser.h
  lib/downloader.h
  lib/json.h
  lib/langcodes.h
  lib/mp4_adobe.h
  lib/mp4_dash.cpp
  lib/mp4_dash.h
  lib/mp4_encryption.h
  lib/mp4_generic.h
  lib/mp4.h
  lib/mp4_ms.h
  lib/mpeg.h
  lib/nal.h
  lib/ogg.h
  lib/procs.h
  lib/rtmpchunks.h
  lib/rtp_fec.h
  lib/rtp.h
  lib/sdp.h
  lib/sdp_media.h
  lib/shared_memory.h
  lib/socket.h
  lib/stream.h
  lib/stun.h
  lib/theora.h
  lib/timing.h
  lib/tinythread.h
  lib/ts_packet.h
  lib/ts_stream.h
  lib/util.h
  lib/vorbis.h
  lib/triggers.h
  lib/opus.h
  lib/riff.h
  lib/ebml.h
  lib/ebml_socketglue.h
  lib/websocket.h
  lib/url.h
  lib/urireader.h
)

if(SRT_LIB)
  list(APPEND libHeaders lib/socket_srt.h)
endif()

########################################
# MistLib - Build                      #
########################################
add_library (mist 
  ${libHeaders}
  lib/adts.cpp
  lib/amf.cpp
  lib/auth.cpp
  lib/encode.cpp
  lib/bitfields.cpp
  lib/bitstream.cpp
  lib/cmaf.cpp
  lib/comms.cpp
  lib/certificate.cpp
  lib/config.cpp
  lib/dtls_srtp_handshake.cpp
  lib/dtsc.cpp
  lib/encryption.cpp
  lib/flac.cpp
  lib/flv_tag.cpp
  lib/h264.cpp
  lib/h265.cpp
  lib/hls_support.cpp
  lib/http_parser.cpp
  lib/downloader.cpp
  lib/json.cpp
  lib/langcodes.cpp
  lib/mp4_adobe.cpp
  lib/mp4.cpp
  lib/mp4_dash.cpp
  lib/mp4_encryption.cpp
  lib/mp4_generic.cpp
  lib/mp4_ms.cpp
  lib/mpeg.cpp
  lib/nal.cpp
  lib/ogg.cpp
  lib/procs.cpp
  lib/rtmpchunks.cpp
  lib/rtp_fec.cpp
  lib/rtp.cpp
  lib/sdp.cpp
  lib/sdp_media.cpp
  lib/shared_memory.cpp
  lib/socket.cpp
  lib/stream.cpp
  lib/stun.cpp
  lib/theora.cpp
  lib/timing.cpp
  lib/tinythread.cpp
  lib/ts_packet.cpp
  lib/ts_stream.cpp
  lib/util.cpp
  lib/vorbis.cpp
  lib/triggers.cpp
  lib/opus.cpp
  lib/riff.cpp
  lib/ebml.cpp
  lib/ebml_socketglue.cpp
  lib/websocket.cpp
  lib/url.cpp
  lib/urireader.cpp
)
if (NOT APPLE)
  set (LIBRT -lrt)
endif()
target_link_libraries(mist 
  -lpthread 
  ${LIBRT}
)
if (NOT NOSSL)
  target_link_libraries(mist mbedtls mbedx509 mbedcrypto)
endif()
install(
  FILES ${libHeaders}
  DESTINATION include/mist
)
install(
  TARGETS mist
  DESTINATION lib
)


if(SRT_LIB)
  add_library(mist_srt lib/socket_srt.h lib/socket_srt.cpp)
  target_link_libraries(mist_srt mist srt)
  install(
    TARGETS mist_srt
    DESTINATION lib
  )
endif()

########################################
# MistLib - Local Header Install       #
########################################
if (${CMAKE_VERSION} VERSION_LESS 3.3.0)
add_custom_command(OUTPUT ${BINARY_DIR}/mist/.headers
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E make_directory ${BINARY_DIR}/mist
  COMMAND cp
  ARGS    ${libHeaders} ${BINARY_DIR}/mist
  COMMAND touch
  ARGS    ${BINARY_DIR}/mist/.headers
  WORKING_DIRECTORY ${SOURCE_DIR}
  DEPENDS ${libHeaders}
)
else()
add_custom_command(OUTPUT ${BINARY_DIR}/mist/.headers
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E make_directory ${BINARY_DIR}/mist
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E copy ${libHeaders} ${BINARY_DIR}/mist
  COMMAND ${CMAKE_COMMAND}
  ARGS    -E touch ${BINARY_DIR}/mist/.headers
  WORKING_DIRECTORY ${SOURCE_DIR}
  DEPENDS ${libHeaders}
)
endif()

########################################
# MistServer - Analysers               #
########################################
macro(makeAnalyser analyserName format)
  add_executable(MistAnalyser${analyserName}
    src/analysers/mist_analyse.cpp
    src/analysers/analyser.cpp
    src/analysers/analyser_${format}.cpp
    ${BINARY_DIR}/mist/.headers
  )
  set_target_properties(MistAnalyser${analyserName} 
    PROPERTIES COMPILE_DEFINITIONS "ANALYSERHEADER=\"analyser_${format}.h\"; ANALYSERTYPE=Analyser${analyserName}"
  )
  target_link_libraries(MistAnalyser${analyserName}
    mist
  )
  install(
    TARGETS MistAnalyser${analyserName}
    DESTINATION bin
  )
endmacro()

makeAnalyser(RTMP rtmp)
makeAnalyser(FLV flv)
makeAnalyser(DTSC dtsc)
makeAnalyser(OGG ogg)
makeAnalyser(EBML ebml)
#makeAnalyser(RTSP rtsp) #LTS #Currently broken. Horribly.
makeAnalyser(TS ts) #LTS
makeAnalyser(MP4 mp4) #LTS
makeAnalyser(H264 h264) #LTS
makeAnalyser(HLS hls) #LTS
makeAnalyser(RIFF riff) #LTS
makeAnalyser(RTSP rtsp) #LTS
makeAnalyser(FLAC flac)

#LTS_START
########################################
# MistServer - Utilities               #
########################################
macro(makeUtil utilName utilFile)
  add_executable(MistUtil${utilName}
    src/utils/util_${utilFile}.cpp
    ${BINARY_DIR}/mist/.headers
  )
  target_link_libraries(MistUtil${utilName}
    mist
  )
  install(
    TARGETS MistUtil${utilName}
    DESTINATION bin
  )
endmacro()

#makeUtil(Stats stats)
makeUtil(META meta)
makeUtil(RAX rax)
makeUtil(AMF amf)
makeUtil(Certbot certbot)
makeUtil(Nuke nuke)
option(LOAD_BALANCE "Build the load balancer")
if (LOAD_BALANCE)
  makeUtil(Load load)
endif()
#LTS_END

add_executable(MistTranslateH264
  src/analysers/h264_translate.cpp
  ${BINARY_DIR}/mist/.headers
)
target_link_libraries(MistTranslateH264
  mist
)

########################################
# MistServer - Inputs                  #
########################################
macro(makeInput inputName format)
  add_executable(MistIn${inputName} 
    src/input/mist_in.cpp
    src/input/input.cpp 
    src/input/input_${format}.cpp 
    src/io.cpp
    ${BINARY_DIR}/mist/.headers
  )
  if (";${ARGN};" MATCHES ";with_srt;")
    target_link_libraries(MistIn${inputName} mist_srt )
  endif()
  if (";${ARGN};" MATCHES ";with_rist;")
    target_link_libraries(MistIn${inputName} rist cjson)
  endif()

  #Set compile definitions
  unset(my_definitions)
  list(APPEND my_definitions "INPUTTYPE=\"input_${format}.h\"")

  set_target_properties(MistIn${inputName} 
    PROPERTIES COMPILE_DEFINITIONS "${my_definitions}"
  )

  target_link_libraries(MistIn${inputName} mist)
  install(
    TARGETS MistIn${inputName} 
    DESTINATION bin
  )
endmacro()

makeInput(HLS hls)
makeInput(DTSC dtsc)
makeInput(MP3 mp3)
makeInput(FLV flv)
makeInput(FLAC flac)

option(WITH_AV "Build a generic libav-based input (not distributable!)")
if (WITH_AV)
  makeInput(AV av)
  target_link_libraries(MistInAV avformat avcodec avutil)
endif()
makeInput(OGG ogg)
makeInput(Buffer buffer)
makeInput(H264 h264)
makeInput(EBML ebml)
makeInput(ISMV ismv)#LTS
makeInput(MP4 mp4)
makeInput(TS ts)#LTS
makeInput(Folder folder)#LTS
makeInput(Playlist playlist)#LTS
makeInput(Balancer balancer)#LTS
makeInput(RTSP rtsp)#LTS
makeInput(SubRip srt)#LTS
makeInput(SDP sdp)

if(SRT_LIB)
  makeInput(TSSRT tssrt with_srt)#LTS
endif()
if(RIST_LIB)
  makeInput(TSRIST tsrist with_rist)#LTS
endif()

makeInput(AAC aac)

########################################
# MistServer - Outputs                 #
########################################
macro(makeOutput outputName format)
  #Parse all extra arguments, for http and ts flags
  SET (tsBaseClass Output)
  SET (outBaseFile src/output/mist_out.cpp)
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput src/output/output_http.cpp)
    if (";${ARGN};" MATCHES ";ts;")
      SET(tsBaseClass HTTPOutput)
    endif()
  endif()
  if (";${ARGN};" MATCHES ";debased;")
    SET(outBaseFile "")
  endif()
  if (";${ARGN};" MATCHES ";ts;")
    SET(tsOutput src/output/output_ts_base.cpp)
  endif()
  if (";${ARGN};" MATCHES ";jpg;")
    SET(tsOutput generated/noffmpeg.h generated/noh264.h)
  endif()
  if (";${ARGN};" MATCHES ";srtp;")
    SET(tsOutput src/output/output_webrtc_srtp.h src/output/output_webrtc_srtp.cpp)
  endif()
  add_executable(MistOut${outputName}
    ${outBaseFile}
    src/output/output.cpp
    src/output/output_${format}.cpp 
    src/io.cpp
    ${httpOutput}
    ${tsOutput} 
    ${mp4Output}
    ${BINARY_DIR}/mist/.headers
  )
  set_target_properties(MistOut${outputName} 
    PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_${format}.h\";TS_BASECLASS=${tsBaseClass}"
  )
  if (";${ARGN};" MATCHES ";with_srt;")
    target_link_libraries(MistOut${outputName} mist_srt)
  endif()
  if (";${ARGN};" MATCHES ";with_rist;")
    target_link_libraries(MistOut${outputName} rist cjson)
  endif()
  if (";${ARGN};" MATCHES ";srtp;")
    target_link_libraries(MistOut${outputName} srtp2)
  endif()
  target_link_libraries(MistOut${outputName} mist )
  install( 
    TARGETS MistOut${outputName}
    DESTINATION bin
  )
endmacro()

makeOutput(RTMP rtmp)
makeOutput(DTSC dtsc)
makeOutput(OGG ogg             http)
makeOutput(FLV flv             http)
makeOutput(HTTPMinimalServer   http_minimalserver http)
makeOutput(MP4 mp4             http)
makeOutput(AAC aac             http)
makeOutput(MP3 mp3             http)
makeOutput(H264 h264           http)
makeOutput(HDS hds             http)
makeOutput(SubRip srt             http)
makeOutput(JSON json           http)
option(WITH_JPG "Build JPG thumbnailer output support")
if (WITH_JPG)
  makeOutput(JPG jpg             http jpg)
endif()
makeOutput(TS ts                    ts)
if(SRT_LIB)
  makeOutput(TSSRT tssrt              ts debased with_srt)
endif()
if(RIST_LIB)
  makeOutput(TSRIST tsrist              ts debased with_rist)
endif()
makeOutput(HTTPTS httpts       http ts)
makeOutput(HLS hls             http ts)
makeOutput(CMAF cmaf           http)#LTS
makeOutput(EBML ebml)
makeOutput(RTSP rtsp)#LTS
makeOutput(WAV wav)#LTS
makeOutput(SDP sdp             http)
makeOutput(FLAC flac           http)

add_executable(MistSession
  ${BINARY_DIR}/mist/.headers
  src/session.cpp
)
install(
  TARGETS MistSession
  DESTINATION bin
)
target_link_libraries(MistSession mist)


add_executable(MistProcFFMPEG
  ${BINARY_DIR}/mist/.headers
  src/process/process_ffmpeg.cpp
  src/output/output_ebml.cpp
  src/input/input_ebml.cpp
  src/input/input.cpp
  src/output/output_http.cpp
  src/output/output.cpp
  src/io.cpp
)
target_link_libraries(MistProcFFMPEG mist)

add_executable(MistProcMKVExec
  ${BINARY_DIR}/mist/.headers
  src/process/process_exec.cpp
  src/output/output_ebml.cpp
  src/input/input_ebml.cpp
  src/input/input.cpp
  src/output/output_http.cpp
  src/output/output.cpp
  src/io.cpp
)
target_link_libraries(MistProcMKVExec mist)

add_executable(MistProcLivepeer
  ${BINARY_DIR}/mist/.headers
  src/process/process_livepeer.cpp
  src/input/input.cpp
  src/output/output_http.cpp
  src/output/output_ts_base.cpp
  src/output/output.cpp
  src/io.cpp
)
target_link_libraries(MistProcLivepeer mist)

if (NOT NOSSL)
  makeOutput(HTTPS https)#LTS
  makeOutput(WebRTC webrtc http srtp)#LTS
endif()

option(WITH_SANITY "Enable MistOutSanityCheck output for testing purposes")
if (WITH_SANITY)
  makeOutput(SanityCheck sanitycheck)#LTS
endif()

add_executable(MistOutHTTP 
  ${BINARY_DIR}/mist/.headers
  src/output/mist_out.cpp
  src/output/output.cpp
  src/output/output_http.cpp 
  src/output/output_http_internal.cpp
  src/io.cpp
  generated/player.js.h
  generated/html5.js.h
  generated/flash_strobe.js.h
  generated/dashjs.js.h
  generated/videojs.js.h
  generated/webrtc.js.h
  generated/mews.js.h
  generated/flv.js.h
  generated/hlsjs.js.h
  generated/rawws.js.h
  generated/player_dash.js.h
  generated/player_dash_lic.js.h
  generated/player_video.js.h
  generated/player_webrtc.js.h
  generated/player_flv.js.h
  generated/player_hlsjs.js.h
  generated/player_libde265.js.h
  generated/skin_default.css.h
  generated/skin_dev.css.h
  generated/skin_videojs.css.h
)
set_target_properties(MistOutHTTP 
  PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_http_internal.h\""
)
target_link_libraries(MistOutHTTP mist)
install(
  TARGETS MistOutHTTP
  DESTINATION bin
)

########################################
# Documentation                        #
########################################
find_package(Doxygen)
if(DOXYGEN_FOUND)
  set(DOXY_LAYOUT "${SOURCE_DIR}/DoxygenLayout.xml")
  set(INPUT_DIRS "${SOURCE_DIR}/src ${SOURCE_DIR}/lib")
  configure_file(${SOURCE_DIR}/Doxyfile.in ${BINARY_DIR}/Doxyfile @ONLY)
  add_custom_target( docs
    ${DOXYGEN_EXECUTABLE} ${BINARY_DIR}/Doxyfile
    COMMAND rm -rf ${BINARY_DIR}/docs
    COMMAND mv ${SOURCE_DIR}/docs ${BINARY_DIR}
    WORKING_DIRECTORY ${SOURCE_DIR}
    COMMENT "Generating API documentation with Doxygen"
    VERBATIM
  )
endif(DOXYGEN_FOUND)

########################################
# Sourcery                             #
########################################
#If cross compiling, assume failure.
if (CMAKE_CROSSCOMPILING)
  set(RUNA "1")
else()
  try_run(RUNA RUNB ${BINARY_DIR}/CMakeTmp ${SOURCE_DIR}/src/sourcery.cpp )
endif()

if("${RUNA}" EQUAL "42")
  message("Not cross compiling - building sourcery")
  add_executable(sourcery
    src/sourcery.cpp
  )
else()
  message("Detected cross compiling")
  if (EXISTS "${SOURCE_DIR}/sourcery")
    if (EXISTS "${BINARY_DIR}/sourcery")
      add_custom_target(sourcery )
    else()
      add_custom_target(sourcery 
        COMMAND cp ${SOURCE_DIR}/sourcery ${BINARY_DIR}/sourcery
      )
    endif()
  else()
    message(FATAL_ERROR "Cross compiling and no usable sourcery binary found - please supply it")
  endif()
endif()

########################################
# Embed Code                           #
########################################
# main
add_custom_command(OUTPUT generated/player.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/player.js player_js generated/player.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/player.js
)
# wrappers
add_custom_command(OUTPUT generated/html5.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/html5.js html5_js generated/html5.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/html5.js
)
add_custom_command(OUTPUT generated/flash_strobe.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/flash_strobe.js flash_strobe_js generated/flash_strobe.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/flash_strobe.js
)
add_custom_command(OUTPUT generated/dashjs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/dashjs.js dash_js generated/dashjs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/dashjs.js
)
add_custom_command(OUTPUT generated/videojs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/videojs.js video_js generated/videojs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/videojs.js
)
add_custom_command(OUTPUT generated/webrtc.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/webrtc.js webrtc_js generated/webrtc.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/webrtc.js
)
add_custom_command(OUTPUT generated/mews.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/mews.js mews_js generated/mews.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/mews.js
)
add_custom_command(OUTPUT generated/flv.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/flv.js flv_js generated/flv.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/flv.js
)
add_custom_command(OUTPUT generated/hlsjs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/hlsjs.js hlsjs_js generated/hlsjs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/hlsjs.js
)
add_custom_command(OUTPUT generated/rawws.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/wrappers/rawws.js rawws_js generated/rawws.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/wrappers/rawws.js
)# players
add_custom_command(OUTPUT generated/player_dash_lic.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/dash.js.license.js player_dash_lic_js generated/player_dash_lic.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/dash.js.license.js
)
add_custom_command(OUTPUT generated/player_dash.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/dash.all.min.js player_dash_js generated/player_dash.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/dash.all.min.js
)
add_custom_command(OUTPUT generated/player_video.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/video.min.js player_video_js generated/player_video.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/video.min.js
)
add_custom_command(OUTPUT generated/player_webrtc.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/webrtc.js player_webrtc_js generated/player_webrtc.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/webrtc.js
)
add_custom_command(OUTPUT generated/player_flv.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/flv.min.js player_flv_js generated/player_flv.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/flv.min.js
)
add_custom_command(OUTPUT generated/player_hlsjs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/hls.js player_hlsjs_js generated/player_hlsjs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/hls.js
)
add_custom_command(OUTPUT generated/player_libde265.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/libde265.min.js player_libde265_js generated/player_libde265.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/libde265.min.js
)
# css
add_custom_command(OUTPUT generated/skin_default.css.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/skins/default.css skin_default_css generated/skin_default.css.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/skins/default.css
)
add_custom_command(OUTPUT generated/skin_dev.css.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/min/skins/dev.css skin_dev_css generated/skin_dev.css.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/min/skins/dev.css
)
add_custom_command(OUTPUT generated/skin_videojs.css.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/skins/video-js.css skin_videojs_css generated/skin_videojs.css.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/skins/video-js.css
)

########################################
# JPG output                           #
########################################
add_custom_command(OUTPUT generated/noffmpeg.h
  COMMAND ./sourcery ${SOURCE_DIR}/src/output/noffmpeg.jpg noffmpeg generated/noffmpeg.h
  DEPENDS sourcery ${SOURCE_DIR}/src/output/noffmpeg.jpg
)
add_custom_command(OUTPUT generated/noh264.h
  COMMAND ./sourcery ${SOURCE_DIR}/src/output/noh264.jpg noh264 generated/noh264.h
  DEPENDS sourcery ${SOURCE_DIR}/src/output/noh264.jpg
)

########################################
# Local Settings Page                  #
########################################
set(lspSOURCES
  ${SOURCE_DIR}/lsp/plugins/jquery.js
  ${SOURCE_DIR}/lsp/plugins/jquery.flot.min.js
  ${SOURCE_DIR}/lsp/plugins/jquery.flot.time.min.js
  ${SOURCE_DIR}/lsp/plugins/jquery.qrcode.min.js
  ${SOURCE_DIR}/lsp/minified.js
)


option(NOGA "Disables Google Analytics entirely in the LSP")
if (NOT NOGA)
  list(APPEND lspSOURCES ${SOURCE_DIR}/lsp/analytics.js)
endif()

add_custom_command(OUTPUT ${BINARY_DIR}/generated/server.html 
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR="${SOURCE_DIR}" -DlspSOURCES="${lspSOURCES}" -P ${SOURCE_DIR}/CMakeServHtml.txt
  DEPENDS ${lspSOURCES} ${SOURCE_DIR}/CMakeServHtml.txt ${SOURCE_DIR}/lsp/main.css ${SOURCE_DIR}/lsp/header.html ${SOURCE_DIR}/lsp/footer.html
)
add_custom_command(OUTPUT generated/server.html.h
  COMMAND ./sourcery generated/server.html server_html generated/server.html.h
  DEPENDS sourcery ${BINARY_DIR}/generated/server.html
)

########################################
# MistController - Build               #
########################################
add_executable(MistController
  src/controller/controller_external_writers.h
  src/controller/controller_limits.h
  src/controller/controller_uplink.h
  src/controller/controller_api.h
  src/controller/controller_statistics.h
  src/controller/controller_connectors.h
  src/controller/controller_storage.h
  src/controller/controller_updater.h
  src/controller/controller_capabilities.h
  src/controller/controller_streams.h
  src/controller/controller_push.h
  src/controller/controller_variables.h
  src/controller/controller.cpp
  src/controller/controller_external_writers.cpp
  src/controller/controller_updater.cpp
  src/controller/controller_streams.cpp
  src/controller/controller_storage.cpp
  src/controller/controller_connectors.cpp
  src/controller/controller_statistics.cpp
  src/controller/controller_limits.cpp
  src/controller/controller_capabilities.cpp
  src/controller/controller_uplink.cpp
  src/controller/controller_api.cpp
  src/controller/controller_push.cpp
  src/controller/controller_variables.cpp
  generated/server.html.h
  ${BINARY_DIR}/mist/.headers
)
set_target_properties(MistController
  PROPERTIES COMPILE_DEFINITIONS RELEASE=${RELEASE}
)
target_link_libraries(MistController
  mist
)
install(
  TARGETS MistController
  DESTINATION bin
)

########################################
# Make Clean                           #
########################################
add_custom_target(clean-all
  COMMAND ${CMAKE_BUILD_TOOL} clean
  COMMAND rm -rf ${BINARY_DIR}/CMakeCache.txt
  COMMAND rm -rf ${BINARY_DIR}/cmake_install.cmake
  COMMAND rm -rf ${BINARY_DIR}/Makefile
  COMMAND rm -rf ${BINARY_DIR}/CMakeFiles
  COMMAND rm -rf ${BINARY_DIR}/Testing
  COMMAND rm -rf ${BINARY_DIR}/CTestTestfile.cmake
  COMMAND rm -rf ${BINARY_DIR}/DartConfiguration.tcl
  COMMAND rm -rf ${BINARY_DIR}/mist
)

########################################
# Tests                                #
########################################
add_executable(urltest test/url.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(urltest mist)
add_test(URLTest COMMAND urltest)
add_executable(logtest test/log.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(logtest mist)
add_test(LOGTest COMMAND logtest)
add_executable(logconvertertest test/converter.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(logconvertertest mist)
add_test(LOGConverterTest COMMAND logconvertertest)
add_executable(downloadertest test/downloader.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(downloadertest mist)
add_test(DownloaderTest COMMAND downloadertest)
add_executable(urireadertest test/urireader.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(urireadertest mist)
add_test(URIReaderTest COMMAND urireadertest)
add_executable(jsontest test/json.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(jsontest mist)
add_test(JSONTest COMMAND jsontest)
add_executable(resolvetest test/resolve.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(resolvetest mist)
add_executable(bitwritertest test/bitwriter.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(bitwritertest mist)
add_test(BitWriterTest COMMAND bitwritertest)
add_executable(streamstatustest test/status.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(streamstatustest mist)
add_executable(websockettest test/websocket.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(websockettest mist)
add_executable(dtsc_sizing_test test/dtsc_sizing.cpp ${BINARY_DIR}/mist/.headers)
target_link_libraries(dtsc_sizing_test mist)
