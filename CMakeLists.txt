########################################
# Basic Setup                          #
########################################
cmake_minimum_required (VERSION 2.6)
project (MistServer)

if(COMMAND cmake_policy)
  cmake_policy(SET CMP0003 NEW)
endif(COMMAND cmake_policy)

SET(SOURCE_DIR ${PROJECT_SOURCE_DIR})
SET(BINARY_DIR ${PROJECT_BINARY_DIR})
include_directories(${SOURCE_DIR})
include_directories(${BINARY_DIR})

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
if (NOT DEFINED ${NOSHM} )
  add_definitions(-DSHM_ENABLED=1)
endif()

########################################
# Build Variables - Thread Names       #
########################################
if (DEFINED ${WITH_THREADNAMES} )
  add_definitions(-DWITH_THREADNAMES=1)
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
  ${SOURCE_DIR}/lib/amf.h
  ${SOURCE_DIR}/lib/auth.h
  ${SOURCE_DIR}/lib/base64.h
  ${SOURCE_DIR}/lib/bitfields.h
  ${SOURCE_DIR}/lib/bitstream.h
  ${SOURCE_DIR}/lib/checksum.h
  ${SOURCE_DIR}/lib/config.h
  ${SOURCE_DIR}/lib/defines.h
  ${SOURCE_DIR}/lib/dtsc.h
  ${SOURCE_DIR}/lib/flv_tag.h
  ${SOURCE_DIR}/lib/http_parser.h
  ${SOURCE_DIR}/lib/json.h
  ${SOURCE_DIR}/lib/mp4_adobe.h
  ${SOURCE_DIR}/lib/mp4_generic.h
  ${SOURCE_DIR}/lib/mp4.h
  ${SOURCE_DIR}/lib/mp4_ms.h
  ${SOURCE_DIR}/lib/nal.h
  ${SOURCE_DIR}/lib/ogg.h
  ${SOURCE_DIR}/lib/procs.h
  ${SOURCE_DIR}/lib/rtmpchunks.h
  ${SOURCE_DIR}/lib/shared_memory.h
  ${SOURCE_DIR}/lib/socket.h
  ${SOURCE_DIR}/lib/stream.h
  ${SOURCE_DIR}/lib/theora.h
  ${SOURCE_DIR}/lib/timing.h
  ${SOURCE_DIR}/lib/tinythread.h
  ${SOURCE_DIR}/lib/ts_packet.h
  ${SOURCE_DIR}/lib/vorbis.h
)

########################################
# MistLib - Source Files               #
########################################
set(libSources 
  ${SOURCE_DIR}/lib/amf.cpp
  ${SOURCE_DIR}/lib/auth.cpp
  ${SOURCE_DIR}/lib/base64.cpp
  ${SOURCE_DIR}/lib/bitfields.cpp
  ${SOURCE_DIR}/lib/bitstream.cpp
  ${SOURCE_DIR}/lib/config.cpp
  ${SOURCE_DIR}/lib/dtsc.cpp
  ${SOURCE_DIR}/lib/dtscmeta.cpp
  ${SOURCE_DIR}/lib/flv_tag.cpp
  ${SOURCE_DIR}/lib/http_parser.cpp
  ${SOURCE_DIR}/lib/json.cpp
  ${SOURCE_DIR}/lib/mp4_adobe.cpp
  ${SOURCE_DIR}/lib/mp4.cpp
  ${SOURCE_DIR}/lib/mp4_generic.cpp
  ${SOURCE_DIR}/lib/mp4_ms.cpp
  ${SOURCE_DIR}/lib/nal.cpp
  ${SOURCE_DIR}/lib/ogg.cpp
  ${SOURCE_DIR}/lib/procs.cpp
  ${SOURCE_DIR}/lib/rtmpchunks.cpp
  ${SOURCE_DIR}/lib/shared_memory.cpp
  ${SOURCE_DIR}/lib/socket.cpp
  ${SOURCE_DIR}/lib/stream.cpp
  ${SOURCE_DIR}/lib/theora.cpp
  ${SOURCE_DIR}/lib/timing.cpp
  ${SOURCE_DIR}/lib/tinythread.cpp
  ${SOURCE_DIR}/lib/ts_packet.cpp
  ${SOURCE_DIR}/lib/vorbis.cpp
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

########################################
# MistServer - Inputs                  #
########################################
macro(makeInput inputName format)
  add_executable(MistIn${inputName} 
    src/input/mist_in.cpp 
    src/input/input.cpp 
    src/input/input_${format}.cpp 
    src/io.cpp
  )
  set_target_properties(MistIn${inputName} 
    PROPERTIES COMPILE_DEFINITIONS INPUTTYPE=\"input_${format}.h\"
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
makeInput(MP3 mp3)
makeInput(FLV flv)
makeInput(OGG ogg)
makeInput(Buffer buffer)

########################################
# MistServer - Outputs                 #
########################################
macro(makeOutput outputName format)
  #Parse all extra arguments, for http and ts flags
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput src/output/output_http.cpp)
    SET(tsBaseClass Output)
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
makeOutput(OGG progressive_ogg http)
makeOutput(FLV progressive_flv http)
makeOutput(MP4 progressive_mp4 http)
makeOutput(MP3 progressive_mp3 http)
makeOutput(HSS hss             http)
makeOutput(HDS hds             http)
makeOutput(SRT srt             http)
makeOutput(JSON json           http)
makeOutput(TS ts                    ts)
makeOutput(HTTPTS httpts       http ts)
makeOutput(HLS hls             http ts)
add_executable(MistOutHTTP 
  src/output/mist_out.cpp
  src/output/output.cpp
  src/output/output_http.cpp 
  src/output/output_http_internal.cpp
  src/io.cpp
)
set_target_properties(MistOutHTTP 
  PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_http_internal.h\""
)
add_dependencies(MistOutHTTP 
  embedcode
)
target_link_libraries(MistOutHTTP
  mist 
)
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
if (EXISTS "${SOURCE_DIR}/sourcery")
  if (EXISTS "${BINARY_DIR}/sourcery")
    add_custom_target(sourcery )
  else()
    add_custom_target(sourcery 
      COMMAND cp ${SOURCE_DIR}/sourcery ${BINARY_DIR}/sourcery
    )
  endif()
else()
  add_executable(sourcery
    src/sourcery.cpp
  )
endif()

########################################
# Embed Code                           #
########################################
add_custom_target(embedcode
  ./sourcery ${SOURCE_DIR}/src/embed.js embed_js ${BINARY_DIR}/embed.js.h
  DEPENDS sourcery ${SOURCE_DIR}/src/embed.js 
  VERBATIM
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
set(lspCode "")
foreach (f ${lspSOURCES} )
  file (READ ${f} s)
  set (lspCode "${lspCode}${s}")
endforeach()
file(READ ${SOURCE_DIR}/lsp/main.css lspCSS)
file(READ ${SOURCE_DIR}/lsp/header.html lspHeader)
file(READ ${SOURCE_DIR}/lsp/footer.html lspFooter)
file(WRITE ${BINARY_DIR}/server.html "${lspHeader}<script>${lspCode}</script><style>${lspCSS}</style>${lspFooter}")
set (lspCSS "")
set (lspHeader "")
set (lspFooter "")

add_custom_target(localSettingsPage
  ./sourcery ${BINARY_DIR}/server.html server_html ${BINARY_DIR}/server.html.h
  DEPENDS sourcery
  VERBATIM
)

########################################
# MistController - Header Files        #
########################################
set(controllerHeaders
  ${SOURCE_DIR}/src/controller/controller_api.h
  ${SOURCE_DIR}/src/controller/controller_statistics.h
  ${SOURCE_DIR}/src/controller/controller_connectors.h
  ${SOURCE_DIR}/src/controller/controller_storage.h
  ${SOURCE_DIR}/src/controller/controller_capabilities.h
  ${SOURCE_DIR}/src/controller/controller_streams.h
)

########################################
# MistController - Source Files        #
########################################
set(controllerSources
  ${SOURCE_DIR}/src/controller/controller.cpp
  ${SOURCE_DIR}/src/controller/controller_streams.cpp
  ${SOURCE_DIR}/src/controller/controller_storage.cpp
  ${SOURCE_DIR}/src/controller/controller_connectors.cpp
  ${SOURCE_DIR}/src/controller/controller_statistics.cpp
  ${SOURCE_DIR}/src/controller/controller_capabilities.cpp
  ${SOURCE_DIR}/src/controller/controller_api.cpp
)

########################################
# MistController - Build               #
########################################
add_executable(MistController
  ${controllerHeaders}
  ${controllerSources}
)
set_target_properties(MistController
  PROPERTIES COMPILE_DEFINITIONS RELEASE=${RELEASE}
)
target_link_libraries(MistController
  mist
)
add_dependencies(MistController
  localSettingsPage
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
  COMMAND rm -rf ${BINARY_DIR}/server.html
  COMMAND rm -rf ${BINARY_DIR}/server.html.h
  COMMAND rm -rf ${BINARY_DIR}/embed.js.h
  COMMAND rm -rf ${BINARY_DIR}/mist
)
