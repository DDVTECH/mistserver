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
