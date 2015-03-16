set(lspSOURCES plugins/md5.js plugins/cattablesort.js mist.js)
set(lspSOURCESmin plugins/jquery.js plugins/jquery.flot.min.js plugins/jquery.flot.time.min.js plugins/jquery.qrcode.min.js)
set(lspDATA header.html main.css footer.html)

add_custom_target( lsp
  ALL
  ${CMAKE_CURRENT_SOURCE_DIR}/generateLSP.sh ${CMAKE_CURRENT_BINARY_DIR}/server.html
  DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/generateLSP.sh ${lspSOURCES} ${lspSOURCESmin} ${lspDATA}
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  VERBATIM
)
