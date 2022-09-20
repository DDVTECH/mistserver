project('mistserver', 'cpp', default_options: ['cpp_std=gnu++03'])

ccpp = meson.get_compiler('cpp')

incroot = include_directories('.')

release = get_option('RELEASE')
if release.contains('DEFAULT')
    release = 'Generic_'+target_machine.cpu_family()
endif
release = release.strip()

rv = run_command('git', 'describe', '--tags', check: false)
version = rv.stdout().strip()
if rv.returncode() != 0
  version = 'Unknown'
endif

add_project_arguments(['-funsigned-char', '-D_LARGEFILE_SOURCE', '-D_FILE_OFFSET_BITS=64','-Wno-sign-compare', '-Wparentheses', '-Wno-non-virtual-dtor', '-Wno-non-virtual-dtor', '-Wno-strict-aliasing'], language: 'cpp')


string_opt = '-D@0@="@1@"'
int_opt = '-D@0@=@1@'

option_defines = [
    string_opt.format('APPNAME', get_option('APPNAME')),
    int_opt.format('DEBUG', get_option('DEBUG')),
    string_opt.format('RELEASE' ,release),
    string_opt.format('PACKAGE_VERSION' ,version),
    int_opt.format('SHM_DATASIZE', get_option('DATASIZE')),
    int_opt.format('STAT_CUTOFF', get_option('STAT_CUTOFF')),
    int_opt.format('STATS_DELAY', get_option('STATS_DELAY')),
    string_opt.format('UDP_API_HOST' ,get_option('UDP_API_HOST')),
    int_opt.format('UDP_API_PORT', get_option('UDP_API_PORT')),
]

if not get_option('NOSHM')
  option_defines += '-DSHM_ENABLED=1'
else
  message('Shared memory use is turned OFF')
endif

if not get_option('FILLER_DATA').contains('DEFAULT') and not get_option('SHARED_SECRET').contains('DEFAULT') and not get_option('SUPER_SECRET').contains('DEFAULT')
  option_defines += [
    string_opt.format('FILLER_DATA' ,get_option('FILLER_DATA')),
    string_opt.format('SHARED_SECRET' ,get_option('SHARED_SECRET')),
    string_opt.format('SUPER_SECRET',get_option('SUPER_SECRET')),
  ]
endif

if get_option('GEOIP')
  option_defines += '-DGEOIP=1'
endif

usessl = true
if get_option('NOSSL')
  message('SSL/TLS support is turned OFF')
  usessl = false
else
  option_defines += '-DSSL=1'
endif

if not get_option('NOUPDATE')
  option_defines += '-DUPDATER=1'
endif

if not get_option('PERPETUAL')
  option_defines += '-DLICENSING=1'
endif

if get_option('NOAUTH')
  option_defines += '-DNOAUTH=1'
endif

if get_option('KILLONEXIT')
  option_defines += '-DKILLONEXIT=true'
endif

if not get_option('DISKSERIAL').contains('DEFAULT')
  option_defines +=string_opt.format('DISKSERIAL',get_option('DISKSERIAL'))
endif

if get_option('WITH_THREADNAMES')
  option_defines += '-DWITH_THREADNAMES=1'
endif

if get_option('NOCRASHCHECK')
  option_defines += '-DNOCRASHCHECK=1'
endif

if get_option('NOLLHLS')
  option_defines += '-DNOLLHLS=1'
endif

message('Builing release @0@ for version @1@ @ debug level @2@'.format(release, version, get_option('DEBUG')))



libsrt = false
if not get_option('NOSRT')
  libsrt = dependency('srt', required: false)
endif
librist = false
if not get_option('NORIST')
  librist = dependency('librist', required: false)
endif

ssl_deps = []

have_librist = not get_option('NORIST') and librist.found()
have_srt = not get_option('NOSRT') and libsrt.found()

if usessl
  mbedtls = ccpp.find_library('mbedtls')
  mbedx509 = ccpp.find_library('mbedx509')
  mbedcrypto = ccpp.find_library('mbedcrypto')
  srtp2 = dependency('libsrtp2')
  ssl_deps = [mbedtls, mbedx509, mbedcrypto, srtp2]
endif

add_project_arguments(option_defines, language: 'cpp')
subdir('lib')
subdir('mist')#This "generates" the headers..
subdir('generated')
subdir('src')
subdir('test')
#doxygen = find_program('doxygen', required: false)

#docs = custom_target('docs', )
