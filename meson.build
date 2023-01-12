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

if usessl
  ccpp = meson.get_compiler('cpp')
  mbedtls = ccpp.find_library('mbedtls', required: false)
  mbedx509 = ccpp.find_library('mbedx509', required: false)
  mbedcrypto = ccpp.find_library('mbedcrypto', required: false)

  # Test if we can compile the way we expect
  code_ddvtech = '''
  #include <mbedtls/ssl.h>
  mbedtls_ssl_srtp_profile srtp_profiles[] ={MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_80,
                                              MBEDTLS_SRTP_AES128_CM_HMAC_SHA1_32};
  static int test()
  {
    mbedtls_ssl_config ssl_conf;
    mbedtls_ssl_conf_dtls_srtp_protection_profiles(&ssl_conf, srtp_profiles,
                                                     sizeof(srtp_profiles) / sizeof(srtp_profiles[0]));
    return 0;
  }
  '''
  ddvtech_mbedtls = ccpp.compiles(code_ddvtech, dependencies: [mbedtls, mbedx509, mbedcrypto], name: 'MbedTLS is DDVTech fork')
  if not mbedtls.found() or not ddvtech_mbedtls
    mbedtls_proj = subproject('mbedtls')
    mbedtls = mbedtls_proj.get_variable('mbedtls_dep')
    mbedx509 = mbedtls_proj.get_variable('mbedx509_dep')
    mbedcrypto = mbedtls_proj.get_variable('mbedcrypto_dep')
  endif

  mist_deps += [mbedtls, mbedx509, mbedcrypto]
  mist_deps += dependency('libsrtp2', default_options: ['tests=disabled'], fallback: ['libsrtp2', 'libsrtp2_dep'])
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

## This makes sure all (installable) executables are build in top level directory
## Done because MistController expects its binaries to all be in the same directory
foreach exec : executables
  exec_tgts += executable(
    exec.get('name'),
    exec.get('sources'),
    dependencies: exec.get('deps'),
    cpp_args: exec.get('defines'),
    install: true,
  )
endforeach

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

