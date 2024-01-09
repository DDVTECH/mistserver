minified = files('minified.js')

if get_option('LSP_MINIFY')
    java = find_program('java', required: true)

    closure_compiler = files('closure-compiler.jar')

    minified = custom_target('lsp_gen_minified',output: 'minified.js', input: ['plugins/md5.js', 'plugins/cattablesort.js', 'mist.js'], command: [java, '-jar', closure_compiler, '--warning_level', 'QUIET', '@INPUT@'], capture: true)
endif

html_list = ['header.html',
     'main.css',
     'footer.html',
     'plugins/jquery.js',
     'plugins/jquery.flot.min.js',
     'plugins/jquery.flot.time.min.js',
     'plugins/jquery.qrcode.min.js',
    ]

html_files = files(html_list)
html_files += minified

if not get_option('NOGA')
    html_files += files('analytics.js')
endif

