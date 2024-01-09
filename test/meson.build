
# Testing binaries that are not unit tests, but intended for manual use

logtest = executable('logtest', 'log.cpp', dependencies: libmist_dep)
convertertest = executable('convertertest', 'converter.cpp', dependencies: libmist_dep)
downloadertest = executable('downloadertest', 'downloader.cpp', dependencies: libmist_dep)
urireadertest = executable('urireadertest', 'urireader.cpp', dependencies: libmist_dep)
jsontest = executable('jsontest', 'json.cpp', dependencies: libmist_dep)
resolvetest = executable('resolvetest', 'resolve.cpp', dependencies: libmist_dep)
streamstatustest = executable('streamstatustest', 'status.cpp', dependencies: libmist_dep)
websockettest = executable('websockettest', 'websocket.cpp', dependencies: libmist_dep)

# Actual unit tests

urltest = executable('urltest', 'url.cpp', dependencies: libmist_dep)
urltest_vm = {'T_PATH':'', 'T_QUERY':'', 'T_FRAG':'', 'T_USER':'', 'T_PASS':'', 'T_NORM':'', 'T_EXT':''}
urltest_v = urltest_vm + {'T_PROTO':'', 'T_HOST':'', 'T_PORT':'0'}
test('HTTPS URL with parent directory', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PROTO':'https', 'T_HOST':'google.com', 'T_PORT':'443', 'T_PATH':'directory/', 'T_NORM':'https://google.com/directory/'},
      args: ['https://google.com/directory/subdirectory/..'])
test('Partially cancelled out path', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PROTO':'http', 'T_HOST':'root', 'T_PORT':'80', 'T_PATH':'home/mo', 'T_NORM':'http://root/home/mo'},
      args: ['http://root/home/schmerkel/../mo'])
urltest_relpath = urltest_v + {'T_PROTO':'https', 'T_HOST':'relpath.com', 'T_PORT':'443', 'T_NORM':'https://relpath.com/'}
test('Fully cancelled out path', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/lang/../..'])
test('Current directory reference stripping', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/./..'])
test('Empty directory stripping', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/////'])
test('Numerical path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/123/..'])
test('Leading single char path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/a/..'])
test('Trailing current dir path deletion', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/.'])
test('Trailing current dir path deletion', urltest, suite: 'URL parser',
      env: urltest_v + {'T_PATH':'bla/', 'T_NORM':'/bla/'},
      args: ['///bla/.'])
test('Extension', urltest, suite: 'URL parser',
      env: urltest_v + {'T_EXT':'mp4', 'T_PATH':'test.mp4', 'T_NORM':'/test.mp4'},
      args: ['///test.mp4'])
test('Parent of root directory', urltest, suite: 'URL parser',
      env: urltest_relpath,
      args: ['https://relpath.com/rel/../..'])
test('All URL components present', urltest, suite: 'URL parser',
      env: {'T_PROTO':'prot', 'T_HOST':'a.bc', 'T_PORT':'3000', 'T_PATH':'path/to/some/file.cpp', 'T_QUERY':'bool=true&int=3', 'T_FRAG':'frag', 'T_USER':'uname', 'T_PASS':'pwd', 'T_NORM':'prot://uname:pwd@a.bc:3000/path/to/some/file.cpp?bool=true&int=3#frag'},
      args: ['prot://uname:pwd@a.bc:3000/path/to/err/../some/file.cpp?bool=true&int=3#frag'])
test('Unknown port for custom protocol', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'path/', 'T_NORM':'prot://authority.nl/path/'},
      args : ['prot://authority.nl/path/weird/./..'])
test('Query/fragment chars before ? char (escaped)', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p&a=t#h/', 'T_NORM':'prot://authority.nl/p%26a=t%23h/'},
      args : ['prot://authority.nl/p%26a=t%23h/'])
test('Query/fragment chars before ? char (unescaped)', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p&a=t', 'T_FRAG':'h/', 'T_NORM':'prot://authority.nl/p%26a=t#h/'},
      args : ['prot://authority.nl/p&a=t#h/'])
test('Path with spaces', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'p at!h/', 'T_NORM':'prot://authority.nl/p+at!h/'},
      args : ['prot://authority.nl/p%20at%21h/'])
test('Escaped characters', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':' !"#$%&()/*+,-.//0123456789/:;<=>?@/', 'T_NORM':'prot://authority.nl/+!%22%23$%25%26()/*%2b,-.//0123456789/:;%3c=%3e%3f@/'},
      args : ['prot://authority.nl/%20%21%22%23%24%25%26%28%29/%2A%2B%2C%2D%2E%2F/%30%31%32%33%34%35%36%37%38%39/%3A%3B%3C%3D%3E%3F%40/'])
test('Linking / Absolute', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/', 'T_NORM':'/sub1/sub2/'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/'])
test('Linking / Relative', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/www.wiki.com/anders', 'T_NORM':'/sub1/sub2/www.wiki.com/anders'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders'])
test('Linking / Relative with current and parent dir', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'sub1/sub2/relatief/', 'T_NORM':'/sub1/sub2/relatief/'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders', './../relatief/'])
test('Linking / Relative file in absolute directory', urltest, suite: 'URL parser',
      env : urltest_vm + {'T_PATH':'absoluut/relatiefFile', 'T_NORM':'/absoluut/relatiefFile'},
      args : ['file://google.com/doe/iets', '/sub1/sub2/', 'www.wiki.com/anders', './../relatief/', '/absoluut/', 'relatiefFile'])
test('Linking / Protocol switch', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'www.wiki.com', 'T_PORT':'80', 'T_NORM':'http://www.wiki.com/'},
      args : ['file://google.com/doe/iets', '/absoluut/', 'relatiefFile', 'http://www.wiki.com'])
test('Linking / Protocol absolute', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'example.com', 'T_PORT':'80', 'T_NORM':'http://example.com/'},
      args : ['http://www.wiki.com', '//example.com'])
test('UTF-8 emoji in path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'authority.nl', 'T_PATH':'😋😄😡😵/', 'T_NORM':'prot://authority.nl/%f0%9f%98%8b%f0%9f%98%84%f0%9f%98%a1%f0%9f%98%b5/'},
      args : ['prot://authority.nl/😋😄😡😵/'])
test('UTF-8 reverse char in path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'prot', 'T_HOST':'a.nl', 'T_PATH':'reverse‏reverse/', 'T_NORM':'prot://a.nl/reverse%e2%80%8freverse/'},
      args : ['prot://a.nl/reverse%E2%80%8Freverse/'])
test('IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::1', 'T_PORT':'159', 'T_NORM':'https://[2001:db8::1]:159/'},
      args : ['https://[2001:db8::1]:159/'])
test('IPv6 address with non-numeric port', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::aa', 'T_PORT':'443', 'T_NORM':'https://[2001:db8::aa]/'},
      args : ['https://[2001:db8:]:aa/'])
test('Bare IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::1', 'T_PORT':'159', 'T_NORM':'https://[2001:db8::1]:159/'},
      args : ['https://2001:db8::1:159/'])
test('Bare hexadecimal IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'https', 'T_HOST':'2001:db8::aa', 'T_PORT':'443', 'T_NORM':'https://[2001:db8::aa]/'},
      args : ['https://2001:db8::aa/'])
test('Username but no password', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'b', 'T_PORT':'80', 'T_USER':'a', 'T_NORM':'http://a@b/'},
      args : ['http://a@b'])
test('Username and password linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'c', 'T_PORT':'80', 'T_PASS':'b', 'T_USER':'a', 'T_NORM':'http://a:b@c/test', 'T_PATH':'test'},
      args : ['http://a:b@c', 'test'])
test('Username but no password linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'http', 'T_HOST':'c', 'T_PORT':'80', 'T_USER':'a', 'T_NORM':'http://a@c/test', 'T_PATH':'test'},
      args : ['http://a@c', 'test'])
test('Protocol-absolute base', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'', 'T_HOST':'test', 'T_NORM':'//test/'},
      args : ['//test'])
test('Bare port number', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PORT':'42', 'T_NORM':'//:42/'},
      args : ['42'])
test('Bare port number linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PORT':'42', 'T_NORM':'//:42/test', 'T_PATH':'test'},
      args : ['42', 'test'])
test('Query string without path', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'', 'T_HOST':'test', 'T_QUERY':'test=test', 'T_NORM':'//test/?test=test'},
      args : ['//test?test=test'])
test('RTMP IPv6 address', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtmp', 'T_HOST':'2001:db8::1', 'T_PORT':'1935', 'T_NORM':'rtmp://[2001:db8::1]/'},
      args : ['rtmp://[2001:db8::1]/'])
test('RTMPS', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtmps', 'T_HOST':'2001:db8::1', 'T_PORT':'443', 'T_NORM':'rtmps://[2001:db8::1]/'},
      args : ['rtmps://[2001:db8::1]/'])
test('DTSC', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'dtsc', 'T_HOST':'2001:db8::1', 'T_PORT':'4200', 'T_NORM':'dtsc://[2001:db8::1]/'},
      args : ['dtsc://[2001:db8::1]/'])
test('RTSP', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtsp', 'T_HOST':'2001:db8::1', 'T_PORT':'554', 'T_NORM':'rtsp://[2001:db8::1]/'},
      args : ['rtsp://[2001:db8::1]/'])
test('RTSP linking', urltest, suite: 'URL parser',
      env : urltest_v + {'T_PROTO':'rtsp', 'T_HOST':'2001:db8::1', 'T_PORT':'554', 'T_NORM':'rtsp://[2001:db8::1]/test', 'T_PATH':'test'},
      args : ['rtsp://[2001:db8::1]/', 'test'])



dtsc_sizing_test = executable('dtsc_sizing_test', 'dtsc_sizing.cpp', dependencies: libmist_dep)
test('DTSC Sizing Test', dtsc_sizing_test)

bitwritertest = executable('bitwritertest', 'bitwriter.cpp', dependencies: libmist_dep)
test('bitWriter Test', bitwritertest)

httpparsertest = executable('httpparsertest', 'http_parser.cpp', dependencies: libmist_dep)
test('GET request for /', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'GET / HTTP/1.1\n\n', 'T_COUNT':'1'})
test('GET request for / with carriage returns', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'GET / HTTP/1.1\r\n\r\n', 'T_COUNT':'1'})
test('POST request to /, raw body', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'POST / HTTP/1.1\nContent-Length: 4\nContent-Type: text/plain\n\ntest', 'T_COUNT':'1'})
test('POST request to /, urlencoded body', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'POST / HTTP/1.1\nContent-Length: 28\nContent-Type: application/x-www-form-urlencoded\n\nfoo=bar&banana=sauce&cookies', 'T_COUNT':'1'})
test('Blank HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 0\n\n', 'T_COUNT':'1'})
test('Blank HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 0\n\n', 'T_LINGER':'1', 'T_COUNT':'1'})
test('Simple HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 4\n\ntest', 'T_COUNT':'1'})
test('Simple HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\nContent-Length: 4\n\ntest', 'T_LINGER':'1', 'T_COUNT':'1'})
test('Simple HTTP response, no length, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\n\ntest', 'T_COUNT':'1'})
test('Simple HTTP response, no length, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nDate: Thu, 15 Jun 2023 21:34:06 GMT\n\ntest', 'T_LINGER':'1', 'T_COUNT':'0'})
test('Chunked HTTP response, closed connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n1\nt\n3\nest\n0\n\n', 'T_COUNT':'1'})
test('Chunked HTTP response, lingering connection', httpparsertest, suite: 'HTTP parser', env: {'T_HTTP':'HTTP/1.1 200 OK\nTransfer-Encoding: chunked\n\n1\nt\n3\nest\n0\n\n', 'T_LINGER':'1', 'T_COUNT':'1'})


#abst_test = executable('abst_test', 'abst_test.cpp', dependencies: libmist_dep)
#test('MP4::ABST Test', abst_test)

