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
      'output/output_rtmp.cpp',
      'controller/controller_external_writers.cpp',
      'controller/controller_updater.cpp',
      'controller/controller_streams.cpp',
      'controller/controller_storage.cpp',
      'controller/controller_connectors.cpp',
      'controller/controller_statistics.cpp',
      'controller/controller_limits.cpp',
      'controller/controller_capabilities.cpp',
      'controller/controller_uplink.cpp',
      'controller/controller_api.cpp',
      'controller/controller_push.cpp',
      'controller/controller_variables.cpp',
    ),
    io_cpp,
    header_tgts,
    embed_tgts,
    server_html
  ],
  'defines': [
    '-DTS_BASECLASS=HTTPOutput',
    '-DONE_BINARY=true',
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