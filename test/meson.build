
# Testing binaries that are not unit tests, but intended for manual use

urltest = executable('urltest', 'url.cpp', include_directories: incroot, link_with: libmist)
logtest = executable('logtest', 'log.cpp', include_directories: incroot, link_with: libmist)
downloadertest = executable('downloadertest', 'downloader.cpp', include_directories: incroot, link_with: libmist)
urireadertest = executable('urireadertest', 'urireader.cpp', include_directories: incroot, link_with: libmist, dependencies: ssl_deps)
jsontest = executable('jsontest', 'json.cpp', include_directories: incroot, link_with: libmist)
resolvetest = executable('resolvetest', 'resolve.cpp', include_directories: incroot, link_with: libmist, dependencies: ssl_deps)
streamstatustest = executable('streamstatustest', 'status.cpp', include_directories: incroot, link_with: libmist)
websockettest = executable('websockettest', 'websocket.cpp', include_directories: incroot, link_with: libmist)

# Actual unit tests

dtsc_sizing_test = executable('dtsc_sizing_test', 'dtsc_sizing.cpp', include_directories: incroot, link_with: libmist)
test('DTSC Sizing Test', dtsc_sizing_test)

bitwritertest = executable('bitwritertest', 'bitwriter.cpp', include_directories: incroot, link_with: libmist)
test('bitWriter Test', bitwritertest)

#abst_test = executable('abst_test', 'abst_test.cpp', include_directories: incroot, link_with: libmist)
#test('MP4::ABST Test', abst_test)

