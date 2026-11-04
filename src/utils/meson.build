
utils = [
    {'name': 'Stats',   'file': 'stats'},
    {'name': 'META',    'file': 'meta'},
    {'name': 'RAX',     'file': 'rax'},
    {'name': 'AMF',     'file': 'amf'},
    {'name': 'Certbot', 'file': 'certbot'},
    {'name': 'Nuke',    'file': 'nuke'},
    {'name': 'Log',     'file': 'log'},
    {'name': 'Writer',  'file': 'writer'},
    {'name': 'Health',  'file': 'health'},
    {'name': 'Cache',   'file': 'cache'},
]

if get_option('LOAD_BALANCE')
  utils += {'name': 'Load', 'file': 'load'}
endif

utils_tgts = []

foreach util : utils
  util_deps = [libmist_dep]
  if util.get('name') == 'Cache' and host_machine.system() == 'darwin'
    util_deps += dependency('appleframeworks', modules: ['CoreServices'])
  endif
  executables += {
    'name': 'MistUtil'+util.get('name'),
    'sources' : [
      files('util_'+util.get('file')+'.cpp'),
      header_tgts
    ],
    'deps' : util_deps,
    'defines' :[],
  }
endforeach

