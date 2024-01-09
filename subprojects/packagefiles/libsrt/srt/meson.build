
versionfile = configure_file(format: 'cmake@', output: 'version.h', input: files('../srtcore/version.h.in'), configuration: {
  'SRT_VERSION_MAJOR': 1,
  'SRT_VERSION_MINOR' : 5,
  'SRT_VERSION_PATCH': 1,
  'CI_BUILD_NUMBER_STRING': '"1.5.1"',
  'SRT_VERSION': '1.5.1',
})

header_tgts += configure_file(copy:true, input: files('../srtcore/srt.h'), output: 'srt.h')
header_tgts += configure_file(copy:true, input: files('../srtcore/logging_api.h'), output: 'logging_api.h')
header_tgts += configure_file(copy:true, input: files('../srtcore/access_control.h'), output: 'access_control.h')
header_tgts += configure_file(copy:true, input: files('../srtcore/platform_sys.h'), output: 'platform_sys.h')
header_tgts += configure_file(copy:true, input: files('../srtcore/udt.h'), output: 'udt.h')

