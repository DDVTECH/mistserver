
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
  utils_tgts += executable('MistUtil'+util.get('name'), 'util_'+util.get('file')+'.cpp', header_tgts, include_directories: incroot, link_with: libmist, install: true)
endforeach