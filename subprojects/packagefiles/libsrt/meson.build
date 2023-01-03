project('SRT', 'cpp', 'c', version: '1.5.1')

add_project_arguments(['-DENABLE_LOGGING=1', '-O3', '-DNDEBUG', '-DENABLE_MONOTONIC_CLOCK=1', '-DENABLE_NEW_RCVBUFFER=1', '-DENABLE_SOCK_CLOEXEC=1', '-DHAI_ENABLE_SRT=1', '-DHAI_PATCH=1', '-DHAVE_INET_PTON=1', '-DHAVE_PTHREAD_GETNAME_NP=1', '-DHAVE_PTHREAD_SETNAME_NP=1', '-DLINUX=1', '-DNDEBUG', '-DSRT_DYNAMIC', '-DSRT_ENABLE_APP_READER', '-DSRT_ENABLE_BINDTODEVICE', '-DSRT_ENABLE_CLOSE_SYNCH', '-DSRT_ENABLE_ENCRYPTION', '-DSRT_EXPORTS', '-DSRT_VERSION="1.5.1"', '-DUSE_MBEDTLS=1', '-D_GNU_SOURCE'], language: ['cpp','c'])

mbedcrypto_lib = dependency('mbedcrypto', required: false)
mbedx509_lib = dependency('mbedx509', required: false)
mbedtls_lib = dependency('mbedtls', required: false)
if not mbedtls_lib.found()
  ccpp = meson.get_compiler('cpp')
  mbedtls_lib = ccpp.find_library('mbedtls')
  mbedx509_lib = ccpp.find_library('mbedx509')
  mbedcrypto_lib = ccpp.find_library('mbedcrypto')
endif
thread_dep = dependency('threads')


header_tgts = []
subdir('srt')

srt_src = files(
  'srtcore/api.cpp',
  'srtcore/buffer.cpp',
  'srtcore/buffer_rcv.cpp',
  'srtcore/cache.cpp',
  'srtcore/channel.cpp',
  'srtcore/common.cpp',
  'srtcore/core.cpp',
  'srtcore/crypto.cpp',
  'srtcore/epoll.cpp',
  'srtcore/fec.cpp',
  'srtcore/handshake.cpp',
  'srtcore/list.cpp',
  'srtcore/logger_default.cpp',
  'srtcore/logger_defs.cpp',
  'srtcore/md5.cpp',
  'srtcore/packet.cpp',
  'srtcore/packetfilter.cpp',
  'srtcore/queue.cpp',
  'srtcore/congctl.cpp',
  'srtcore/socketconfig.cpp',
  'srtcore/srt_c_api.cpp',
  'srtcore/strerror_defs.cpp',
  'srtcore/sync.cpp',
  'srtcore/tsbpd_time.cpp',
  'srtcore/window.cpp',
  'srtcore/sync_posix.cpp',
  'haicrypt/haicrypt_log.cpp',
  'srtcore/srt_compat.c',
  'haicrypt/cryspr.c',
  'haicrypt/cryspr-mbedtls.c',
  'haicrypt/hcrypt.c',
  'haicrypt/hcrypt_ctx_rx.c',
  'haicrypt/hcrypt_ctx_tx.c',
  'haicrypt/hcrypt_rx.c',
  'haicrypt/hcrypt_sa.c',
  'haicrypt/hcrypt_tx.c',
  'haicrypt/hcrypt_xpt_srt.c',
  'srtcore/srt.h',
  'srtcore/logging_api.h',
  'srtcore/access_control.h',
  'srtcore/platform_sys.h',
  'srtcore/udt.h'
)

libsrt = library(
  'srt',
  sources: [srt_src, versionfile],
  dependencies: [mbedtls_lib, mbedx509_lib, mbedcrypto_lib, thread_dep],
  include_directories: ['srt', 'haicrypt', 'srtcore']
)

srt_dep = declare_dependency(
  link_with : [libsrt],
  dependencies: [mbedtls_lib, mbedx509_lib, mbedcrypto_lib, thread_dep],
  sources: [header_tgts],
  include_directories: include_directories('.'),
)

