
# Testing binaries that are not unit tests, but intended for manual use

logtest = executable('logtest', 'log.cpp', dependencies: libmist_dep)
downloadertest = executable('downloadertest', 'downloader.cpp', dependencies: libmist_dep)
urireadertest = executable('urireadertest', 'urireader.cpp', dependencies: libmist_dep)
jsontest = executable('jsontest', 'json.cpp', dependencies: libmist_dep)
resolvetest = executable('resolvetest', 'resolve.cpp', dependencies: libmist_dep)
streamstatustest = executable('streamstatustest', 'status.cpp', dependencies: libmist_dep)
websockettest = executable('websockettest', 'websocket.cpp', dependencies: libmist_dep)

# Actual unit tests

urltest = executable('urltest', 'url.cpp', dependencies: libmist_dep)
test('url test google', urltest, 
      env : ['Protocol=https', 'Host=google.com', 'Local=No', 'Port=443', 'Path=directory/', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['https://google.com/directory/subdirectory/..'])
test('url test schmerkel', urltest, 
      env : ['Protocol=http', 'Host=root', 'Local=No', 'Port=80', 'Path=home/mo','Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['http://root/home/schmerkel/../mo'])
test('url test relpath', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=','Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/rel/lang/../..'])
test('url test relpath missing dot', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/rel/./..'])
test('url test relpath 1', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=','Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/1/..'])
test('url test relpath 123', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=','Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/123/..'])
test('url test relpath 2', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=','Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/rel/../..'])
test('url test relpath langer pad', urltest, 
      env : ['Protocol=https', 'Host=relpath.com', 'Local=No', 'Port=443', 'Path=langer/', 'Query=', 'Fragment=', 'Username=', 'Password='],
      args : ['https://relpath.com/langer/pad/..'])
test('url test authority 3000', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=3000', 'Path=path/to/some/file.cpp', 'Query=bool=true&int=3', 'Fragment=frag', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl:3000/path/to/err/../some/file.cpp?bool=true&int=3#frag'])
test('url test authority path', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=path/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/path/weird/./..'])
test('url test authority path wierd', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=path/weird/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/path/weird/a/..'])
test('url test authority p&at#h/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=p&at#h/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/p%26at%23h/'])
test('url test authority p at!h/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=p at!h/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/p%20at%21h/'])
# KATRI TODO Cannot test pass as env ' %27 , need escape char for meson string ''
test('url test authority  !"#$%&()/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path= !"#$%&()/','Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/%20%21%22%23%24%25%26%28%29/'])
test('url test authority *+,-.//', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=*+,-.//', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/%2A%2B%2C%2D%2E%2F/'])
test('url test authority 0123456789/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=0123456789/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/%30%31%32%33%34%35%36%37%38%39/'])
test('url test authority _;<=>?@/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=:;<=>?@/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/%3A%3B%3C%3D%3E%3F%40/'])
test('url test authority ABCDEFGHI/', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=ABCDEFGHI/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/%41%42%43%44%45%46%47%48%49/'])
test('url test doe/iets', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=doe/iets', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/doe/iets'])
test('url test sub1/sub2/', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=sub1/sub2/', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/sub1/sub2/'])
test('url test sub1/sub2/ anders', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=sub1/sub2/www.wiki.com/anders', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/sub1/sub2/www.wiki.com/anders'])
test('url test sub1/sub2/ relatief', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=sub1/sub2/www.wiki.com/relatief/', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/sub1/sub2/www.wiki.com/relatief/'])
test('url test absoluut', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=absoluut/', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/absoluut/'])
test('url test absoluut relatiefFile', urltest, 
      env : ['Protocol=file', 'Host=google.com', 'Local=No', 'Port=0', 'Path=absoluut/relatiefFile', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['file://google.com/absoluut/relatiefFile'])
test('url test path empty', urltest, 
      env : ['Protocol=http', 'Host=www.wiki.com', 'Local=No', 'Port=80', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['http://www.wiki.com/'])
test('url test path emoticons', urltest, 
      env : ['Protocol=protocol', 'Host=authority.nl', 'Local=No', 'Port=0', 'Path=😋😄😡😵/', 'Query=', 'Fragment=', 'Username=uname', 'Password=pwd'], 
      args : ['protocol://uname:pwd@authority.nl/😋😄😡😵/'])
test('url test rtmp 1', urltest, 
      env : ['Protocol=https', 'Host=2001:db8::1', 'Local=No', 'Port=159', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['https://[2001:db8::1]:159/'])
test('url test rtpm 2', urltest, 
      env : ['Protocol=rtmp', 'Host=2001:db8::1', 'Local=No', 'Port=1935', 'Path=','Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['rtmp://[2001:db8::1]/'])
test('url test rtpms', urltest, 
      env : ['Protocol=rtmps', 'Host=2001:db8::1', 'Local=No', 'Port=443', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['rtmps://[2001:db8::1]/'])
test('url test rtpm 2', urltest, 
      env : ['Protocol=dtsc', 'Host=2001:db8::1', 'Local=No', 'Port=4200', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['dtsc://[2001:db8::1]/'])
test('url test rtsp', urltest, 
      env : ['Protocol=rtsp', 'Host=2001:db8::1', 'Local=No', 'Port=554', 'Path=', 'Query=', 'Fragment=', 'Username=', 'Password='], 
      args : ['rtsp://[2001:db8::1]/'])



dtsc_sizing_test = executable('dtsc_sizing_test', 'dtsc_sizing.cpp', dependencies: libmist_dep)
test('DTSC Sizing Test', dtsc_sizing_test)

bitwritertest = executable('bitwritertest', 'bitwriter.cpp', dependencies: libmist_dep)
test('bitWriter Test', bitwritertest)

#abst_test = executable('abst_test', 'abst_test.cpp', dependencies: libmist_dep)
#test('MP4::ABST Test', abst_test)

