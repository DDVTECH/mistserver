
utils = [
#    {'name': 'Stats',   'file': 'stats'},
    {'name': 'META',    'file': 'meta'},
    {'name': 'RAX',     'file': 'rax'},
    {'name': 'AMF',     'file': 'amf'},
    {'name': 'Certbot', 'file': 'certbot'},
    {'name': 'Nuke',    'file': 'nuke'},
]

if get_option('LOAD_BALANCE')
  utils += {'name': 'Load', 'file': 'load'}
endif

utils_tgts = []

foreach util : utils
  executables += {
    'name': 'MistUtil'+util.get('name'),
    'sources' : [
      files('util_'+util.get('file')+'.cpp'),
      header_tgts
    ],
    'deps' : [libmist_dep],
    'defines' :[],
  }
endforeach

