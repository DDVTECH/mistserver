minified = files('minified.js')

if get_option('LSP_MINIFY')
    java = find_program('java', required: true)

    closure_compiler = files('closure-compiler.jar')

    minified = custom_target('lsp_gen_minified',output: 'minified.js', input: ['plugins/md5.js', 'plugins/cattablesort.js', 'i18n.js', 'mist.js'], command: [java, '-jar', closure_compiler, '--warning_level', 'QUIET', '@INPUT@'], capture: true)
endif

html_list = ['header.html',
     'main.css',
     'footer.html',
     'plugins/jquery.js',
     'plugins/jquery.flot.min.js',
     'plugins/jquery.flot.time.min.js',
     'plugins/jquery.qrcode.min.js',
     'i18n.js',
    ]

html_files = files(html_list)
html_files += minified

# --- Translation catalogs ----------------------------------------------------
# Languages shipped with the panel. English is the source language: its strings
# are the msgids, so it needs no catalog. Adding a language means dropping a
# lsp/lang/<code>.po next to the others and adding one line here.
lsp_languages = [
  {'code': 'de-DE', 'name': 'Deutsch'},
]

py_installation = import('python').find_installation('python3')
po2json = files('../scripts/po2json.py')

lang_json_tgts = []
lang_specs = []

foreach l : lsp_languages
  lang_json_tgts += custom_target('lang_json_' + l.get('code'),
    input: 'lang/' + l.get('code') + '.po',
    output: l.get('code') + '.json',
    command: [py_installation, po2json, '@INPUT@', '@OUTPUT@'])
  lang_specs += l.get('code') + '=' + l.get('name')
endforeach
