outputs = [
    {'name' : 'RTMP',              'format' : 'rtmp'},
    {'name' : 'DTSC',              'format' : 'dtsc'},
    {'name' : 'OGG',               'format' : 'ogg',                'extra': ['http']},
    {'name' : 'FLV',               'format' : 'flv',                'extra': ['http'] },
    {'name' : 'HTTPMinimalServer', 'format' : 'http_minimalserver', 'extra': ['http']},
    {'name' : 'MP4',               'format' : 'mp4',                'extra': ['http']},
    {'name' : 'AAC',               'format' : 'aac',                'extra': ['http']},
    {'name' : 'FLAC',              'format' : 'flac',               'extra': ['http']},
    {'name' : 'MP3',               'format' : 'mp3',                'extra': ['http']},
    {'name' : 'H264',              'format' : 'h264',               'extra': ['http']},
    {'name' : 'HDS',               'format' : 'hds',                'extra': ['http']},
    {'name' : 'SubRip',            'format' : 'subrip',             'extra': ['http']},
    {'name' : 'JSON',              'format' : 'json',               'extra': ['http']},
    {'name' : 'TS',                'format' : 'ts',                 'extra': ['ts']},
    {'name' : 'HTTPTS',            'format' : 'httpts',             'extra': ['http', 'ts']},
    {'name' : 'HLS',               'format' : 'hls',                'extra': ['http', 'ts']},
    {'name' : 'CMAF',              'format' : 'cmaf',               'extra': ['http']},
    {'name' : 'EBML',              'format' : 'ebml',               'extra': ['http']},
    {'name' : 'RTSP',              'format' : 'rtsp'},
    {'name' : 'WAV',               'format' : 'wav',                'extra': ['http']},
    {'name' : 'SDP',               'format' : 'sdp',                'extra': ['http']},
    {'name' : 'HTTP',              'format' : 'http_internal',      'extra': ['http','embed']},
    {'name' : 'JSONLine',          'format' : 'jsonline'},
]

if usessl 
  outputs += [
    {'name' : 'HTTPS', 'format' : 'https'},
    {'name' : 'WebRTC', 'format' : 'webrtc', 'extra': ['http','jpg','srtp']}
  ]
endif

if have_librist
  outputs += {'name' : 'TSRIST', 'format' : 'tsrist', 'extra': ['ts', 'debased', 'with_rist']}
endif

if have_srt
  outputs += {'name' : 'TSSRT', 'format' : 'tssrt', 'extra': ['ts', 'debased', 'with_srt']}
endif

if get_option('WITH_JPG')
  outputs += {'name' : 'JPG', 'format' : 'jpg', 'extra': ['http','embed']}
endif

if get_option('WITH_SANITY')
  outputs += {'name' : 'SanityCheck', 'format' : 'sanitycheck'}
endif

#Referenced by targets in process
output_ebml_cpp = files('output_ebml.cpp')
output_http_cpp = files('output_http.cpp')
output_ts_base_cpp = files('output_ts_base.cpp')
output_cpp = files('output.cpp')

outputs_tgts = []

foreach output : outputs 
  deps = [libmist_dep]
  base = files('mist_out.cpp')
  tsBaseClass = 'Output'

  sources = [
    files('output.cpp',
    'output_'+output.get('format')+'.cpp'),
    io_cpp
  ]

  if output.has_key('extra')
    extra = output.get('extra')
    if extra.contains('http')
      sources += files('output_http.cpp')
      if extra.contains('ts')
        tsBaseClass = 'HTTPOutput'
      endif
    endif
    if extra.contains('jpg')
    endif
    if extra.contains('ts')
      sources += files('output_ts_base.cpp')
      sources += files('output_http.cpp')
    endif
    if extra.contains('with_rist')
      deps += librist
    endif
    if extra.contains('with_srt')
      deps += libmist_srt_dep
      deps += libsrt
    endif
    if extra.contains('srtp')
      sources += files('output_webrtc_srtp.cpp', 'output_webrtc_srtp.h')
      if have_usrsctp
        deps += usrsctp_dep
      endif
    endif
    if extra.contains('embed')
      sources += embed_tgts
    endif
  endif

  sources_debased = []
  foreach source : sources
    sources_debased += source
  endforeach

  if output.has_key('extra')
    extra = output.get('extra')
    if not extra.contains('debased')
      sources += base
    endif
  else
    sources += base
  endif

  outputtype_header = 'output_'+output.get('format')+'.h'

  executables += {
    'name': 'MistOut'+output.get('name'),
    'sources' : [
      sources,
      header_tgts
    ],
    'sources_debased' : [
      sources_debased
    ],
    'deps' : deps,
    'defines' : [
      string_opt.format('OUTPUTTYPE', outputtype_header),
      '-DTS_BASECLASS='+tsBaseClass
    ],
    'headers' : [
      files(outputtype_header)
    ]
  }
endforeach

