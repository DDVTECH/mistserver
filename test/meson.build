
# Testing binaries that are not unit tests, but intended for manual use

urltest = executable('urltest', 'url.cpp', dependencies: libmist_dep)
logtest = executable('logtest', 'log.cpp', dependencies: libmist_dep)
downloadertest = executable('downloadertest', 'downloader.cpp', dependencies: libmist_dep)
urireadertest = executable('urireadertest', 'urireader.cpp', dependencies: libmist_dep)
jsontest = executable('jsontest', 'json.cpp', dependencies: libmist_dep)
resolvetest = executable('resolvetest', 'resolve.cpp', dependencies: libmist_dep)
streamstatustest = executable('streamstatustest', 'status.cpp', dependencies: libmist_dep)
websockettest = executable('websockettest', 'websocket.cpp', dependencies: libmist_dep)

# Actual unit tests

dtsc_sizing_test = executable('dtsc_sizing_test', 'dtsc_sizing.cpp', dependencies: libmist_dep)
test('DTSC Sizing Test', dtsc_sizing_test)

bitwritertest = executable('bitwritertest', 'bitwriter.cpp', dependencies: libmist_dep)
test('bitWriter Test', bitwritertest)

#abst_test = executable('abst_test', 'abst_test.cpp', dependencies: libmist_dep)
#test('MP4::ABST Test', abst_test)

