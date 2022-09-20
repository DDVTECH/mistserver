inputs = [
    {'name' : 'HLS', 'format' : 'hls'},
    {'name' : 'DTSC', 'format' : 'dtsc'},
    {'name' : 'MP3', 'format' : 'mp3'},
    {'name' : 'FLV', 'format' : 'flv'},
    {'name' : 'OGG', 'format' : 'ogg'},
    {'name' : 'Buffer', 'format' : 'buffer'},
    {'name' : 'H264', 'format' : 'h264'},
    {'name' : 'EBML', 'format' : 'ebml'},
    {'name' : 'ISMV', 'format' : 'ismv'},
    {'name' : 'MP4', 'format' : 'mp4'},
    {'name' : 'TS', 'format' : 'ts'},
    {'name' : 'Folder', 'format' : 'folder'},
    {'name' : 'Playlist', 'format' : 'playlist'},
    {'name' : 'Balancer', 'format' : 'balancer'},
    {'name' : 'RTSP', 'format' : 'rtsp'},
    {'name' : 'SRT', 'format' : 'srt'},
    {'name' : 'SDP', 'format' : 'sdp'},
    {'name' : 'AAC', 'format' : 'aac'},
]

if have_librist
  inputs += {'name' : 'TSRIST', 'format' : 'tsrist', 'extra' : 'with_rist'}
endif

if have_srt
  inputs += {'name' : 'TSSRT', 'format' : 'tssrt', 'extra' : 'with_srt'}
endif

av_libs = []

if get_option('WITH_AV')
  inputs += {'name' : 'AV', 'format' : 'av'}
  av_libs = [
    dependency('libavformat'),
    dependency('libavcodec'),
    dependency('libavutil'),
  ]
endif

inputs_tgts = []

foreach input : inputs
  link_libs = [libmist]
  deps = []

  if input.has_key('extra')
    if input.get('extra').contains('with_rist')
      deps += librist
    endif
    if input.get('extra').contains('with_srt')
      link_libs += libmist_srt
      deps += libsrt
    endif
  endif
  if input.get('name').contains('AV')
    deps += av_libs
  endif
  if input.get('name').contains('HLS')
    deps += ssl_deps
  endif
  inputs_tgts += executable('MistIn'+input.get('name'),
                            'mist_in.cpp',
                            'input.cpp',
                            'input_'+input.get('format')+'.cpp',
                            join_paths(meson.source_root(),'src/io.cpp'),
                            header_tgts,
                            link_with: link_libs,
                            dependencies: deps,
                            include_directories: incroot,
                            cpp_args: [string_opt.format('INPUTTYPE', 'input_'+input.get('format')+'.h')],
                            install: true)
endforeach
