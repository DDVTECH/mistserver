########################################
# Basic Setup                          #
########################################
cmake_minimum_required (VERSION 2.6)
SET(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "Usr install prefix")
project (MistServer)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

SET(SOURCE_DIR ${PROJECT_SOURCE_DIR})
SET(BINARY_DIR ${PROJECT_BINARY_DIR})
include_directories(${SOURCE_DIR})
include_directories(${BINARY_DIR} ${BINARY_DIR}/generated)

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
# Build Variables - Debug              #
########################################
if (NOT DEBUG)
  set(DEBUG 4)
endif()

########################################
# Build Variables - Shared Memory      #
########################################
if (NOT DEFINED NOSHM )
  add_definitions(-DSHM_ENABLED=1)
endif()


if (NOT DEFINED FILLER_DATA OR NOT DEFINED SHARED_SECRET OR NOT DEFINED SUPER_SECRET)#LTS
  message(WARNING "Not all LTS variables have been set and this is an LTS build - are you sure about this?")#LTS
endif()#LTS
add_definitions(-DFILLER_DATA="${FILLER_DATA}" -DSHARED_SECRET="${SHARED_SECRET}" -DSUPER_SECRET="${SUPER_SECRET}")#LTS
if (DEFINED GEOIP )
  add_definitions(-DGEOIP=1)
endif()
if (DEFINED BIGMETA )
  add_definitions(-DBIGMETA=1)
endif()
if (DEFINED DATASIZE )
  add_definitions(-DSHM_DATASIZE=${DATASIZE})
endif()
if (NOT DEFINED NOUPDATE )
  add_definitions(-DUPDATER=1)
endif()
if (NOT DEFINED PERPETUAL )
  add_definitions(-DLICENSING=1)
endif()
if (DEFINED NOAUTH )
  add_definitions(-DNOAUTH=1)
endif()
if (DEFINED KILLONEXIT )
  add_definitions(-DKILLONEXIT=true)
endif()

########################################
# Build Variables - Thread Names       #
########################################
if (DEFINED WITH_THREADNAMES )
  add_definitions(-DWITH_THREADNAMES=1)
endif()

########################################
# Build Variables - No Crash Check     #
########################################
if (DEFINED NOCRASHCHECK )
  add_definitions(-DNOCRASHCHECK=1)
endif()

########################################
# Build Variables - Stats delay overrid#
########################################
if (DEFINED STATS_DELAY )
  add_definitions(-DSTATS_DELAY=${STATS_DELAY})
endif()

########################################
# Build Variables - Prepare for Build  #
########################################
message("Builing release ${RELEASE} for version ${PACKAGE_VERSION} @ debug level ${DEBUG}")
add_definitions(-g -funsigned-char -D_LARGEFILE_SOURCE -D_FILE_OFFSET_BITS=64 -DDEBUG=${DEBUG} -DPACKAGE_VERSION=${PACKAGE_VERSION} -DRELEASE=${RELEASE})

########################################
# MistLib - Header Files               #
########################################
set(libHeaders
  ${SOURCE_DIR}/lib/adts.h
  ${SOURCE_DIR}/lib/amf.h
  ${SOURCE_DIR}/lib/auth.h
  ${SOURCE_DIR}/lib/encode.h
  ${SOURCE_DIR}/lib/bitfields.h
  ${SOURCE_DIR}/lib/bitstream.h
  ${SOURCE_DIR}/lib/checksum.h
  ${SOURCE_DIR}/lib/config.h
  ${SOURCE_DIR}/lib/defines.h
  ${SOURCE_DIR}/lib/dtsc.h
  ${SOURCE_DIR}/lib/encryption.h
  ${SOURCE_DIR}/lib/flv_tag.h
  ${SOURCE_DIR}/lib/h264.h
  ${SOURCE_DIR}/lib/h265.h
  ${SOURCE_DIR}/lib/http_parser.h
  ${SOURCE_DIR}/lib/json.h
  ${SOURCE_DIR}/lib/langcodes.h
  ${SOURCE_DIR}/lib/mp4_adobe.h
  ${SOURCE_DIR}/lib/mp4_dash.cpp
  ${SOURCE_DIR}/lib/mp4_dash.h
  ${SOURCE_DIR}/lib/mp4_encryption.h
  ${SOURCE_DIR}/lib/mp4_generic.h
  ${SOURCE_DIR}/lib/mp4.h
  ${SOURCE_DIR}/lib/mp4_ms.h
  ${SOURCE_DIR}/lib/nal.h
  ${SOURCE_DIR}/lib/ogg.h
  ${SOURCE_DIR}/lib/procs.h
  ${SOURCE_DIR}/lib/rijndael.h
  ${SOURCE_DIR}/lib/rtmpchunks.h
  ${SOURCE_DIR}/lib/rtp.h
  ${SOURCE_DIR}/lib/shared_memory.h
  ${SOURCE_DIR}/lib/socket.h
  ${SOURCE_DIR}/lib/stream.h
  ${SOURCE_DIR}/lib/theora.h
  ${SOURCE_DIR}/lib/timing.h
  ${SOURCE_DIR}/lib/tinythread.h
  ${SOURCE_DIR}/lib/ts_packet.h
  ${SOURCE_DIR}/lib/ts_stream.h
  ${SOURCE_DIR}/lib/util.h
  ${SOURCE_DIR}/lib/vorbis.h
  ${SOURCE_DIR}/lib/triggers.h
)

########################################
# MistLib - Source Files               #
########################################
set(libSources 
  ${SOURCE_DIR}/lib/adts.cpp
  ${SOURCE_DIR}/lib/amf.cpp
  ${SOURCE_DIR}/lib/auth.cpp
  ${SOURCE_DIR}/lib/encode.cpp
  ${SOURCE_DIR}/lib/bitfields.cpp
  ${SOURCE_DIR}/lib/bitstream.cpp
  ${SOURCE_DIR}/lib/config.cpp
  ${SOURCE_DIR}/lib/dtsc.cpp
  ${SOURCE_DIR}/lib/dtscmeta.cpp
  ${SOURCE_DIR}/lib/encryption.cpp
  ${SOURCE_DIR}/lib/flv_tag.cpp
  ${SOURCE_DIR}/lib/h264.cpp
  ${SOURCE_DIR}/lib/h265.cpp
  ${SOURCE_DIR}/lib/http_parser.cpp
  ${SOURCE_DIR}/lib/json.cpp
  ${SOURCE_DIR}/lib/langcodes.cpp
  ${SOURCE_DIR}/lib/mp4_adobe.cpp
  ${SOURCE_DIR}/lib/mp4.cpp
  ${SOURCE_DIR}/lib/mp4_dash.cpp
  ${SOURCE_DIR}/lib/mp4_encryption.cpp
  ${SOURCE_DIR}/lib/mp4_generic.cpp
  ${SOURCE_DIR}/lib/mp4_ms.cpp
  ${SOURCE_DIR}/lib/nal.cpp
  ${SOURCE_DIR}/lib/ogg.cpp
  ${SOURCE_DIR}/lib/procs.cpp
  ${SOURCE_DIR}/lib/rijndael.cpp
  ${SOURCE_DIR}/lib/rtmpchunks.cpp
  ${SOURCE_DIR}/lib/rtp.cpp
  ${SOURCE_DIR}/lib/shared_memory.cpp
  ${SOURCE_DIR}/lib/socket.cpp
  ${SOURCE_DIR}/lib/stream.cpp
  ${SOURCE_DIR}/lib/theora.cpp
  ${SOURCE_DIR}/lib/timing.cpp
  ${SOURCE_DIR}/lib/tinythread.cpp
  ${SOURCE_DIR}/lib/ts_packet.cpp
  ${SOURCE_DIR}/lib/ts_stream.cpp
  ${SOURCE_DIR}/lib/util.cpp
  ${SOURCE_DIR}/lib/vorbis.cpp
  ${SOURCE_DIR}/lib/triggers.cpp
)
########################################
# MistLib - Build                      #
########################################
add_library (mist 
  ${libHeaders}
  ${libSources} 
)
if (NOT APPLE)
  set (LIBRT -lrt)
endif()
target_link_libraries(mist 
  -lpthread 
  ${LIBRT}
)
install(
  FILES ${libHeaders}
  DESTINATION include/mist
)
install(
  TARGETS mist
  DESTINATION lib
)

########################################
# MistLib - Local Header Install       #
########################################
add_custom_command(TARGET mist
  POST_BUILD
  COMMAND mkdir -p ${BINARY_DIR}/mist
  COMMAND cp ${libHeaders} ${BINARY_DIR}/mist
  DEPENDS ${libHeaders}
)

########################################
# MistServer - Analysers        `      #
########################################
macro(makeAnalyser analyserName format)
  add_executable(MistAnalyser${analyserName}
    src/analysers/${format}_analyser.cpp
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
makeAnalyser(AMF amf)
makeAnalyser(MP4 mp4)
makeAnalyser(OGG ogg)
makeAnalyser(RAX rax)
#makeAnalyser(RTP rtp) #LTS
makeAnalyser(RTSP rtsp_rtp) #LTS
makeAnalyser(TS ts) #LTS
makeAnalyser(TSStream tsstream) #LTS
makeAnalyser(Stats stats) #LTS


if (DEFINED LOAD_BALANCE )
  makeAnalyser(Load load) #LTS
endif()

########################################
# MistServer - Inputs                  #
########################################
macro(makeInput inputName format)
  if (";${ARGN};" MATCHES ";folder;")
    set(mainScript src/input/mist_in_folder.cpp)
  else()
    set(mainScript src/input/mist_in.cpp)
  endif()
  add_executable(MistIn${inputName} 
    ${mainScript}
    src/input/input.cpp 
    src/input/input_${format}.cpp 
    src/io.cpp
  )

  #Set compile definitions
  unset(my_definitions)
  list(APPEND my_definitions "INPUTTYPE=\"input_${format}.h\"")

  set_target_properties(MistIn${inputName} 
    PROPERTIES COMPILE_DEFINITIONS "${my_definitions}"
  )

  target_link_libraries(MistIn${inputName} 
    mist 
  )
  install(
    TARGETS MistIn${inputName} 
    DESTINATION bin
  )
endmacro()

makeInput(DTSC dtsc)
makeInput(DTSCCrypt dtsccrypt)
makeInput(MP3 mp3)
makeInput(FLV flv)
if (DEFINED WITH_AV )
  makeInput(AV av)
  target_link_libraries(MistInAV avformat avcodec avutil)
endif()
makeInput(OGG ogg)
makeInput(Buffer buffer)
makeInput(ISMV ismv)#LTS
makeInput(MP4 mp4)#LTS
makeInput(TS ts)#LTS
makeInput(Folder folder folder)#LTS

########################################
# MistServer - Outputs                 #
########################################
macro(makeOutput outputName format)
  #Parse all extra arguments, for http and ts flags
  SET (tsBaseClass Output)
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput src/output/output_http.cpp)
    if (";${ARGN};" MATCHES ";ts;")
      SET(tsBaseClass HTTPOutput)
    endif()
  endif()
  if (";${ARGN};" MATCHES ";ts;")
    SET(tsOutput src/output/output_ts_base.cpp)
  endif()
  add_executable(MistOut${outputName}
    src/output/mist_out.cpp
    src/output/output.cpp
    src/output/output_${format}.cpp 
    src/io.cpp
    ${httpOutput}
    ${tsOutput} 
  )
  set_target_properties(MistOut${outputName} 
    PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_${format}.h\";TS_BASECLASS=${tsBaseClass}"
  )
  target_link_libraries(MistOut${outputName} 
    mist 
  )
  install( 
    TARGETS MistOut${outputName}
    DESTINATION bin
  )
endmacro()

makeOutput(RTMP rtmp)
makeOutput(DTSC dtsc)
makeOutput(OGG progressive_ogg http)
makeOutput(FLV progressive_flv http)
makeOutput(HTTPMinimalServer http_minimalserver http)
makeOutput(MP4 progressive_mp4 http)
makeOutput(MP3 progressive_mp3 http)
makeOutput(HSS hss             http)
makeOutput(HDS hds             http)
makeOutput(SRT srt             http)
makeOutput(JSON json           http)
makeOutput(TS ts                    ts)
makeOutput(HTTPTS httpts       http ts)
makeOutput(HLS hls             http ts)
makeOutput(Push push)#LTS
makeOutput(RTSP rtsp)#LTS
makeOutput(HTTPS https)#LTS
target_link_libraries(MistOutHTTPS mbedtls mbedcrypto mbedx509)
makeOutput(DASH dash_mp4       http)#LTS

if (DEFINED WITH_SANITY )
  makeOutput(SanityCheck sanitycheck)#LTS
endif()

add_executable(MistOutHTTP 
  src/output/mist_out.cpp
  src/output/output.cpp
  src/output/output_http.cpp 
  src/output/output_http_internal.cpp
  src/io.cpp
  generated/silverlight.js.h
  generated/embed.js.h
  generated/html5.js.h
  generated/flash_strobe.js.h
  generated/theoplayer.js.h
  generated/jwplayer.js.h
  generated/polytrope.js.h
  generated/dashjs.js.h
  generated/videojs.js.h
  generated/playerdash.js.h
  generated/playervideo.js.h
  generated/core.js.h
  generated/mist.css.h
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
  try_run(RUNA RUNB ${BINARY_DIR}/CMakeTmp ${SOURCE_DIR}/src/sourcery.cpp)
endif()

if("${RUNA}" EQUAL "0")
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
add_custom_command(OUTPUT generated/silverlight.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/silverlight.js silverlight_js generated/silverlight.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/silverlight.js
)
add_custom_command(OUTPUT generated/embed.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/src/embed.js embed_js generated/embed.js.h
  DEPENDS sourcery ${SOURCE_DIR}/src/embed.js
)
add_custom_command(OUTPUT generated/html5.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/html5.js html5_js generated/html5.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/html5.js
)
add_custom_command(OUTPUT generated/flash_strobe.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/flash_strobe.js flash_strobe_js generated/flash_strobe.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/flash_strobe.js
)
add_custom_command(OUTPUT generated/theoplayer.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/theoplayer.js theoplayer_js generated/theoplayer.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/theoplayer.js
)
add_custom_command(OUTPUT generated/jwplayer.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/jwplayer.js jwplayer_js generated/jwplayer.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/jwplayer.js
)
add_custom_command(OUTPUT generated/polytrope.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/polytrope.js polytrope_js generated/polytrope.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/polytrope.js
)
add_custom_command(OUTPUT generated/dashjs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/dashjs.js dash_js generated/dashjs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/dashjs.js
)
add_custom_command(OUTPUT generated/videojs.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/wrappers/videojs.js video_js generated/videojs.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/wrappers/videojs.js
)
add_custom_command(OUTPUT generated/playerdash.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/dash.js playerdash_js generated/playerdash.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/dash.js
)
add_custom_command(OUTPUT generated/playervideo.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/players/videojs.js playervideo_js generated/playervideo.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/players/videojs.js
)
add_custom_command(OUTPUT generated/core.js.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/core.js core_js generated/core.js.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/core.js
)
add_custom_command(OUTPUT generated/mist.css.h
  COMMAND ./sourcery ${SOURCE_DIR}/embed/mist.css mist_css generated/mist.css.h
  DEPENDS sourcery ${SOURCE_DIR}/embed/mist.css
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

add_custom_command(OUTPUT ${BINARY_DIR}/generated/server.html 
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR="${SOURCE_DIR}" -DlspSOURCES="${lspSOURCES}" -P ${SOURCE_DIR}/CMakeServHtml.txt
  DEPENDS ${lspSOURCES} ${SOURCE_DIR}/CMakeServHtml.txt ${SOURCE_DIR}/lsp/main.css ${SOURCE_DIR}/lsp/header.html ${SOURCE_DIR}/lsp/footer.html
)
add_custom_command(OUTPUT generated/server.html.h
  COMMAND ./sourcery generated/server.html server_html generated/server.html.h
  DEPENDS sourcery ${BINARY_DIR}/generated/server.html
)

########################################
# MistController - Header Files        #
########################################
set(controllerHeaders
  ${SOURCE_DIR}/src/controller/controller_limits.h
  ${SOURCE_DIR}/src/controller/controller_uplink.h
  ${SOURCE_DIR}/src/controller/controller_api.h
  ${SOURCE_DIR}/src/controller/controller_statistics.h
  ${SOURCE_DIR}/src/controller/controller_connectors.h
  ${SOURCE_DIR}/src/controller/controller_storage.h
  ${SOURCE_DIR}/src/controller/controller_updater.h
  ${SOURCE_DIR}/src/controller/controller_capabilities.h
  ${SOURCE_DIR}/src/controller/controller_streams.h
  ${SOURCE_DIR}/src/controller/controller_push.h
  ${SOURCE_DIR}/src/controller/controller_license.h
)

########################################
# MistController - Source Files        #
########################################
set(controllerSources
  ${SOURCE_DIR}/src/controller/controller.cpp
  ${SOURCE_DIR}/src/controller/controller_updater.cpp
  ${SOURCE_DIR}/src/controller/controller_streams.cpp
  ${SOURCE_DIR}/src/controller/controller_storage.cpp
  ${SOURCE_DIR}/src/controller/controller_connectors.cpp
  ${SOURCE_DIR}/src/controller/controller_statistics.cpp
  ${SOURCE_DIR}/src/controller/controller_limits.cpp
  ${SOURCE_DIR}/src/controller/controller_capabilities.cpp
  ${SOURCE_DIR}/src/controller/controller_uplink.cpp
  ${SOURCE_DIR}/src/controller/controller_api.cpp
  ${SOURCE_DIR}/src/controller/controller_push.cpp
  ${SOURCE_DIR}/src/controller/controller_license.cpp
)
########################################
# MistController - Build               #
########################################
add_executable(MistController
  ${controllerHeaders}
  ${controllerSources}
  generated/server.html.h
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
add_executable(aes_ctr128
  ${SOURCE_DIR}/test/aes_ctr128.cpp
)
target_link_libraries(aes_ctr128
  mist
)
add_test(AESTest COMMAND aes_ctr128)
