outputs = [
    {'name' : 'RTMP',              'format' : 'rtmp'},
    {'name' : 'DTSC',              'format' : 'dtsc'},
    {'name' : 'OGG',               'format' : 'ogg',                'extra': ['http']},
    {'name' : 'FLV',               'format' : 'flv',                'extra': ['http'] },
    {'name' : 'HTTPMinimalServer', 'format' : 'http_minimalserver', 'extra': ['http']},
    {'name' : 'MP4',               'format' : 'mp4',                'extra': ['http']},
    {'name' : 'AAC',               'format' : 'aac',                'extra': ['http']},
    {'name' : 'MP3',               'format' : 'mp3',                'extra': ['http']},
    {'name' : 'H264',              'format' : 'h264',               'extra': ['http']},
    {'name' : 'HDS',               'format' : 'hds',                'extra': ['http']},
    {'name' : 'SRT',               'format' : 'srt',                'extra': ['http']},
    {'name' : 'JSON',              'format' : 'json',               'extra': ['http']},
    {'name' : 'TS',                'format' : 'ts',                 'extra': ['ts']},
    {'name' : 'HTTPTS',            'format' : 'httpts',             'extra': ['http', 'ts']},
    {'name' : 'HLS',               'format' : 'hls',                'extra': ['http', 'ts']},
    {'name' : 'CMAF',              'format' : 'cmaf',               'extra': ['http']},
    {'name' : 'EBML',              'format' : 'ebml',               'extra': ['http']},
    {'name' : 'RTSP',              'format' : 'rtsp'},
    {'name' : 'WAV',               'format' : 'wav',                'extra': ['http']},
    {'name' : 'SDP',               'format' : 'sdp',                'extra': ['http']},
]

if have_librist
  outputs += {'name' : 'TSRIST', 'format' : 'tsrist', 'extra': ['ts', 'debased', 'with_rist']}
endif

if have_srt
  outputs += {'name' : 'TSSRT', 'format' : 'tssrt', 'extra': ['ts', 'debased', 'with_srt']}
endif

if get_option('WITH_JPG')
  outputs += {'name' : 'JPG', 'format' : 'jpg', 'extra': ['http','jpg']}
endif

if usessl 
  outputs += [
    {'name' : 'HTTPS', 'format' : 'https'},
    {'name' : 'WebRTC', 'format' : 'webrtc', 'extra': ['http','jpg']}
  ]
endif

if get_option('WITH_SANITY')
  outputs += {'name' : 'SanityCheck', 'format' : 'sanitycheck'}
endif

outputs_tgts = []

foreach output : outputs 
  link_libs = [libmist]
  deps = []
  base = 'mist_out.cpp'
  tsBaseClass = 'Output'
  extra_tgt_dep = []

  sources = [
    'output.cpp',
    'output_'+output.get('format')+'.cpp',
    join_paths(meson.source_root(), 'src/io.cpp')
  ]

  if output.has_key('extra')
    extra = output.get('extra')
    if not extra.contains('debased')
      sources += base
    endif
    if extra.contains('http')
      sources += 'output_http.cpp'
      if extra.contains('ts')
        tsBaseClass = 'HTTPOutput'
      endif
    endif
    if extra.contains('jpg')
      extra_tgt_dep = embed_tgts
    endif
    if extra.contains('ts')
      sources += 'output_ts_base.cpp'
    endif
    if extra.contains('with_rist')
      deps += librist
    endif
    if extra.contains('with_srt')
      link_libs += libmist_srt
      deps += libsrt
    endif
  else
    sources += base
  endif
  if output.get('name').contains('HTTPS')
    deps += ssl_deps
  endif

  outputs_tgts += executable('MistOut'+output.get('name'),
                              sources,
                              extra_tgt_dep,
                              header_tgts,
                              include_directories: incroot,
                              link_with: link_libs,
                              dependencies: deps,
                              install: true,
                              cpp_args: [
                                string_opt.format('OUTPUTTYPE', 'output_'+output.get('format')+'.h'),
                                '-DTS_BASECLASS='+tsBaseClass])
endforeach

MistOutHTTP = executable('MistOutHTTP',
                        'mist_out.cpp',
                        'output.cpp',
                        'output_http.cpp',
                        'output_http_internal.cpp',
                        join_paths(meson.source_root(), 'src/io.cpp'),
                        header_tgts,
                        embed_tgts,
                        link_with: libmist,
                        include_directories: incroot,
                        install: true,
                        cpp_args: [string_opt.format('OUTPUTTYPE', 'output_http_internal.h')]
)
