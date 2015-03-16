macro(makeAnalyser analyserName format)
  add_executable( MistAnalyser${analyserName} analysers/${format}_analyser.cpp )
  target_link_libraries( MistAnalyser${analyserName} mist )
endmacro()

macro(makeInput inputName format)
  add_executable( MistIn${inputName} input/mist_in.cpp input/input.cpp input/input_${format}.cpp )
  set_target_properties( MistIn${inputName} PROPERTIES COMPILE_DEFINITIONS INPUTTYPE=\"input_${format}.h\")
  target_link_libraries( MistIn${inputName} mist )
endmacro()

macro(makeOutput outputName format)
#check if 'http' is one of the argyments, if yes, this is an http output
  if (";${ARGN};" MATCHES ";http;")
    SET(httpOutput output/output_http.cpp)
    if (";${ARGN};" MATCHES ";ts;")
      SET(tsBaseClass HTTPOutput)
    else()
      SET(tsBaseClass Output)
    endif()
  endif()
  if (";${ARGN};" MATCHES ";ts;")
    SET(tsOutput output/output_ts_base.cpp)
  endif()
  add_executable( MistOut${outputName} output/mist_out.cpp output/output.cpp ${httpOutput} ${tsOutput} output/output_${format}.cpp )
  set_target_properties( MistOut${outputName} PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_${format}.h\";TS_BASECLASS=${tsBaseClass}")
  target_link_libraries( MistOut${outputName} mist )
endmacro()

makeAnalyser(RTMP rtmp)
makeAnalyser(FLV flv)
makeAnalyser(DTSC dtsc)
makeAnalyser(AMF amf)
makeAnalyser(MP4 mp4)
makeAnalyser(OGG ogg)

makeInput(DTSC dtsc)
makeInput(MP3 mp3)
makeInput(FLV flv)
makeInput(OGG ogg)
makeInput(Buffer buffer)

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

#get the bitlength of this system
execute_process(COMMAND getconf LONG_BIT OUTPUT_VARIABLE RELEASE_RAW )
#strip off the trailing spaces and newline
string(STRIP ${RELEASE_RAW} RELEASE)
set(RELEASE \"${RELEASE}\" ) 

include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_executable( sourcery sourcery.cpp )

add_custom_target( embedcode
  ALL
  ./sourcery ${CMAKE_CURRENT_SOURCE_DIR}/embed.js embed_js ${CMAKE_CURRENT_BINARY_DIR}/embed.js.h
  DEPENDS sourcery ${CMAKE_CURRENT_SOURCE_DIR}/embed.js 
  VERBATIM
)

add_custom_target( localSettingsPage
  ALL
  ./sourcery ${BINARY_DIR}/lsp/server.html server_html ${CMAKE_CURRENT_BINARY_DIR}/server.html.h
  DEPENDS sourcery lsp
  VERBATIM
)

add_executable( MistOutHTTP output/mist_out.cpp output/output.cpp output/output_http.cpp output/output_http_internal.cpp)
set_target_properties( MistOutHTTP PROPERTIES COMPILE_DEFINITIONS "OUTPUTTYPE=\"output_http_internal.h\"")
add_dependencies(MistOutHTTP embedcode)
target_link_libraries( MistOutHTTP mist )

add_executable( MistController
  controller/controller.cpp
  controller/controller_api.h
  controller/controller_api.cpp
  controller/controller_capabilities.h
  controller/controller_capabilities.cpp
  controller/controller_connectors.h
  controller/controller_connectors.cpp
  controller/controller_statistics.h
  controller/controller_statistics.cpp
  controller/controller_storage.h
  controller/controller_storage.cpp
  controller/controller_streams.h
  controller/controller_streams.cpp
)
set_target_properties( MistController PROPERTIES COMPILE_DEFINITIONS RELEASE=${RELEASE})
target_link_libraries( MistController mist )
add_dependencies(MistController localSettingsPage)
