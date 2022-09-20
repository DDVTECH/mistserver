project('mistserver', 'cpp', default_options: ['cpp_std=gnu++03'])
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
rv = run_command('git', 'describe', '--tags', check: false)
version = rv.stdout().strip()
if rv.returncode() != 0
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

if not get_option('NOSHM')
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

add_project_arguments(option_defines, language: 'cpp')

# End of options

message('Building release @0@ for version @1@ @ debug level @2@'.format(release, version, get_option('DEBUG')))

# Set dependencies

ssl_deps = []

libsrt = false
if not get_option('NOSRT')
  libsrt = dependency('srt', required: false)
endif
have_srt = not get_option('NOSRT') and libsrt.found()

librist = false
if not get_option('NORIST')
  librist = dependency('librist', required: false)
endif
have_librist = not get_option('NORIST') and librist.found()

if usessl
  ccpp = meson.get_compiler('cpp')
  mbedtls = ccpp.find_library('mbedtls')
  mbedx509 = ccpp.find_library('mbedx509')
  mbedcrypto = ccpp.find_library('mbedcrypto')
  srtp2 = dependency('libsrtp2')
  ssl_deps = [mbedtls, mbedx509, mbedcrypto, srtp2]
endif

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
    link_with: exec.get('link'),
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

