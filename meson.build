project('mistserver', 'cpp', default_options: ['cpp_std=c++98'])
add_project_arguments(['-funsigned-char', '-D_LARGEFILE_SOURCE','-Wno-sign-compare', '-Wparentheses', '-Wno-non-virtual-dtor', '-Wno-strict-aliasing'], language: 'cpp')

# Ensures the "mist" directory is in the include path
incroot = include_directories('.')

# Sets the RELEASE, if not set externally
release = get_option('RELEASE')
if release.contains('DEFAULT')
    release = 'Generic_'+target_machine.cpu_family()
endif
release = release.strip()

# Grab version number from git, if available
# Falls back to a file called "VERSION" or the string "Unknown" otherwise
git = find_program('git', required: false)
if git.found()
  rv = run_command(git, 'describe', '--tags', check: false)
  version = rv.stdout().strip()
  if rv.returncode() != 0
    fs = import('fs') 
    if fs.is_file('VERSION')
      version = fs.read('VERSION').strip()
    else 
      version = 'Unknown'
    endif
  endif
else
  fs = import('fs') 
  if fs.is_file('VERSION')
    version = fs.read('VERSION').strip()
  else 
    version = 'Unknown'
  endif
endif

# Handle all options
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

if not get_option('NOSHM') and host_machine.system() != 'darwin'
  option_defines += '-DSHM_ENABLED=1'
else
  message('Shared memory use is turned OFF')
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

if get_option('NOAUTH')
  option_defines += '-DNOAUTH=1'
endif

if not get_option('DISKSERIAL').contains('DEFAULT')
  option_defines += string_opt.format('DISKSERIAL',get_option('DISKSERIAL'))
endif

if not get_option('FILLER_DATA').contains('DEFAULT')
  option_defines += string_opt.format('FILLER_DATA',get_option('FILLER_DATA'))
endif

if not get_option('SHARED_SECRET').contains('DEFAULT')
  option_defines += string_opt.format('SHARED_SECRET',get_option('SHARED_SECRET'))
endif

if get_option('WITH_THREADNAMES')
  option_defines += '-DWITH_THREADNAMES=1'
endif

if get_option('NOLLHLS')
  option_defines += '-DNOLLHLS=1'
endif

# End of options

message('Building release @0@ for version @1@ @ debug level @2@'.format(release, version, get_option('DEBUG')))

# Set dependencies

mist_deps = []
ccpp = meson.get_compiler('cpp')

if usessl
  mbedtls = ccpp.find_library('mbedtls', required: false)
  mbedx509 = ccpp.find_library('mbedx509', required: false)
  mbedcrypto = ccpp.find_library('mbedcrypto', required: false)

  ##This currently only works for MbedTLS < 3
  code_upstream = '''
  #include <mbedtls/ssl.h>
  static mbedtls_ssl_srtp_profile srtp_profiles[] ={MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_TLS_SRTP_AES128_CM_HMAC_SHA1_32, MBEDTLS_TLS_SRTP_UNSET};
  static int test(){
    mbedtls_ssl_config ssl_conf;
    mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtp_profiles);
    return 0;
  }
  '''

  # Test if we can compile the way we expect
  code_ddvtech = '''
  #include <mbedtls/ssl.h>
  mbedtls_ssl_srtp_profile srtp_profiles[]={MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80, MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32};
  static int test(){
    mbedtls_ssl_config ssl_conf;
    mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtp_profiles, sizeof(srtp_profiles) / sizeof(srtp_profiles[0]));
    return 0;
  }
  '''

  have_upstream_mbedtls_srtp = ccpp.compiles(code_upstream, dependencies: [mbedtls, mbedx509, mbedcrypto], name: 'MbedTLS SRTP is upstream')
  option_defines += int_opt.format('HAVE_UPSTREAM_MBEDTLS_SRTP', have_upstream_mbedtls_srtp.to_int())
  if not have_upstream_mbedtls_srtp
    ddvtech_mbedtls = ccpp.compiles(code_ddvtech, dependencies: [mbedtls, mbedx509, mbedcrypto], name: 'MbedTLS is DDVTech fork')
    if not mbedtls.found() or not ddvtech_mbedtls
      mbedtls_proj = subproject('mbedtls')
      mbedtls = mbedtls_proj.get_variable('mbedtls_dep')
      mbedx509 = mbedtls_proj.get_variable('mbedx509_dep')
      mbedcrypto = mbedtls_proj.get_variable('mbedcrypto_dep')
    endif

  endif
  mist_deps += [mbedtls, mbedx509, mbedcrypto]
  mist_deps += dependency('libsrtp2', default_options: ['tests=disabled', 'crypto-library=mbedtls'], fallback: ['libsrtp2', 'libsrtp2_dep'])

  usrsctp_dep = false
  if not get_option('NOUSRSCTP') and host_machine.system() != 'cygwin'
    usrsctp_dep = dependency('usrsctp', fallback: ['usrsctp', 'usrsctp_dep'])
  endif
  have_usrsctp = not get_option('NOUSRSCTP') and host_machine.system() != 'cygwin' and usrsctp_dep.found()
  if have_usrsctp
    option_defines += '-DWITH_DATACHANNELS'
  endif
endif

libsrt = false
if not get_option('NOSRT') and host_machine.system() != 'darwin'
  libsrt = dependency('srt', fallback: ['libsrt', 'srt_dep'])
endif
have_srt = not get_option('NOSRT') and host_machine.system() != 'darwin' and libsrt.found()
if have_srt
  option_defines += '-DWITH_SRT'
endif

librist = false
if not get_option('NORIST')
  librist = dependency('librist', fallback: ['librist', 'librist_dep'], default_options:['test=false', 'built_tools=false'])
endif
have_librist = not get_option('NORIST') and librist.found()

# Add thread dependency since we always have thread code in libmist
mist_deps += dependency('threads')

# Add rt dependency when using shared memory
if not get_option('NOSHM') and host_machine.system() != 'darwin'
  mist_deps += ccpp.find_library('rt', required : true)
endif

#
if host_machine.system() == 'cygwin'
  option_defines += ['-D_POSIX_C_SOURCE=200112L', '-D_GNU_SOURCE', '-D_TTHREAD_POSIX_', '-D_TTHREAD_PLATFORM_DEFINED_']
  option_defines += '-D_GNU_SOURCE'
endif

# Set defines from active options
add_project_arguments(option_defines, language: 'cpp')

# Set build targets

executables = []

# Web sources
subdir('lsp')
subdir('generated')

# libmist
subdir('lib')
subdir('mist')

# Binaries
subdir('src')
subdir('test')

exec_tgts = []

make_header = find_program('scripts/make_header.sh')

onebinary_stuff = []
onebinary_stuff += 'MistInAAC'
onebinary_stuff += 'MistInBalancer'
onebinary_stuff += 'MistInBuffer'
onebinary_stuff += 'MistInDTSC'
onebinary_stuff += 'MistInEBML'
onebinary_stuff += 'MistInFLAC'
onebinary_stuff += 'MistInFLV'
onebinary_stuff += 'MistInFolder'
onebinary_stuff += 'MistInH264'
onebinary_stuff += 'MistInHLS'
onebinary_stuff += 'MistInISMV'
onebinary_stuff += 'MistInMP3'
onebinary_stuff += 'MistInMP4'
onebinary_stuff += 'MistInOGG'
onebinary_stuff += 'MistInPlaylist'
onebinary_stuff += 'MistInRTSP'
onebinary_stuff += 'MistInSDP'
onebinary_stuff += 'MistInSubRip'
onebinary_stuff += 'MistInTS'
# onebinary_stuff += 'MistInTSRIST'
# onebinary_stuff += 'MistInTSSRT'
onebinary_stuff += 'MistOutAAC'
onebinary_stuff += 'MistOutCMAF'
onebinary_stuff += 'MistOutDTSC'
onebinary_stuff += 'MistOutEBML'
onebinary_stuff += 'MistOutFLAC'
onebinary_stuff += 'MistOutFLV'
onebinary_stuff += 'MistOutH264'
onebinary_stuff += 'MistOutHDS'
onebinary_stuff += 'MistOutHLS'
onebinary_stuff += 'MistOutHTTP'
onebinary_stuff += 'MistOutHTTPMinimalServer'
onebinary_stuff += 'MistOutHTTPS'
onebinary_stuff += 'MistOutHTTPTS'
onebinary_stuff += 'MistOutJSON'
onebinary_stuff += 'MistOutJSONLine'
onebinary_stuff += 'MistOutMP3'
onebinary_stuff += 'MistOutMP4'
onebinary_stuff += 'MistOutOGG'
onebinary_stuff += 'MistOutRTMP'
onebinary_stuff += 'MistOutRTSP'
# onebinary_stuff += 'MistOutSDP'
# onebinary_stuff += 'MistOutSubRip'
onebinary_stuff += 'MistOutTS'
# onebinary_stuff += 'MistOutTSRIST'
# onebinary_stuff += 'MistOutTSSRT'
onebinary_stuff += 'MistOutWAV'
# onebinary_stuff += 'MistOutWebRTC'

onebinary_tgts = []
onebinary_headers = []
onebinary_sources = []

## This makes sure all (installable) executables are build in top level directory
## Done because MistController expects its binaries to all be in the same directory
foreach exec : executables
  my_exec = executable(
    exec.get('name'),
    exec.get('sources'),
    dependencies: exec.get('deps'),
    cpp_args: exec.get('defines'),
    install: true,
  )
  exec_tgts += my_exec
  gen_src_name = exec.get('name') + '.json'
  gen_src = custom_target(gen_src_name,
    input: [my_exec],
    output: [exec.get('name') + '.json'],
    command: [make_header, '@INPUT@', '@OUTPUT0@']
  )
  if onebinary_stuff.contains(exec.get('name'))
    onebinary_tgts += gen_src
    onebinary_headers += exec.get('headers')
    onebinary_sources += exec.get('sources_debased')
  endif
endforeach

prog_python = find_program('python3')
script = files('scripts/onebinary_gen.py')

onebinary_header = custom_target('onebinary',
  input: [onebinary_tgts, onebinary_headers],
  output: ['controller_static_capabilities.cpp', 'mistserver.cpp'],
  command: [prog_python, script, '--cap-header', '@OUTPUT0@', '--entrypoint', '@OUTPUT1@', '@INPUT@'],
  depends: [onebinary_tgts]
)

onebinary = executable(
  'MistServer',
  [
    files(
      'src/controller/controller_external_writers.cpp',
      'src/controller/controller_updater.cpp',
      'src/controller/controller_streams.cpp',
      'src/controller/controller_storage.cpp',
      'src/controller/controller_connectors.cpp',
      'src/controller/controller_statistics.cpp',
      'src/controller/controller_limits.cpp',
      'src/controller/controller_capabilities.cpp',
      'src/controller/controller_uplink.cpp',
      'src/controller/controller_api.cpp',
      'src/controller/controller_push.cpp',
      'src/controller/controller_variables.cpp',
      'src/session.cpp',
      'src/controller/controller.cpp',
    ),
    onebinary_sources,
    io_cpp,
    header_tgts,
    embed_tgts,
    server_html,
    onebinary_header,
  ],
  cpp_args: [
    '-DTS_BASECLASS=Output',
    '-DONE_BINARY=1',
  ],
  dependencies : [libmist_dep],
  install: true,
)

# Docs
doxygen = find_program('doxygen', required: false)
if doxygen.found()
  doxyfile = configure_file(output: 'Doxyfile', input: 'Doxyfile.in', configuration: {
    'PACKAGE_VERSION': version,
    'RELEASE' : release,
    'DOXY_LAYOUT': meson.project_source_root() + '/DoxygenLayout.xml',
    'INPUT_DIRS': meson.project_source_root() + '/src ' + meson.project_source_root() + '/lib',
  })
  run_target('docs', command: [doxygen, doxyfile])
endif

