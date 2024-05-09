io_cpp = files('io.cpp')

subdir('analysers')
subdir('utils')
subdir('input')
subdir('output')
subdir('process')
subdir('controller')

executables += {
  'name': 'MistSession',
  'sources' : [files('session.cpp'),  header_tgts],
  'defines': [],
  'deps' : [libmist_dep]
}

executables += {
  'name': 'MistServer',
  'sources': [
    files(
      'mistserver.cpp',
      'input/input.cpp',
      'input/input_buffer.cpp',
      'output/output.cpp',
      'output/output_hls.cpp',
      'output/output_http.cpp',
      'output/output_http_internal.cpp',
      'output/output_ts.cpp',
      'output/output_ts_base.cpp',
      'output/output_rtmp.cpp'
    ),
    io_cpp,
    header_tgts,
    embed_tgts
  ],
  'defines': [
    '-DTS_BASECLASS=HTTPOutput',
  ],
  'deps' : [libmist_dep]
}

# executables += {
#   'name': 'MistServer',
#   'sources': [
#     files(
#       'output/mist_out.cpp',
#       'output/output.cpp',
#       'output/output_rtmp.cpp'
#     ),
#     io_cpp,
#     header_tgts
#   ],
#   'defines': [
#     string_opt.format('OUTPUTTYPE', 'output_rtmp.h')
#   ],
#   'deps' : [libmist_dep]
# }
  # executables += {
  #   'name': 'MistOut'+output.get('name'),
  #   'sources' : [
  #     sources,
  #     header_tgts
  #   ],
  #   'deps' : deps,
  #   'defines' : [
  #     string_opt.format('OUTPUTTYPE', 'output_'+output.get('format')+'.h'),
  #     '-DTS_BASECLASS='+tsBaseClass
  #   ]
  # }