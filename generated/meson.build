
# Utilities that we use to generate source files
sourcery = executable('sourcery', '../src/sourcery.cpp', native: true)
make_html = executable('make_html', '../src/make_html.cpp', native: true)

# If requested, use local versions instead of building our own
if get_option('LOCAL_GENERATORS')
  make_html = find_program('make_html', required:true, native:true, dirs: [meson.project_source_root()])
  sourcery = find_program('sourcery', required:true, native:true, dirs: [meson.project_source_root()])
endif

gen_html = custom_target('gen_html', output: 'server.html', input: html_files, command: [make_html, '@OUTPUT@', '@INPUT@'])

embed_files = [
    {'infile': '../embed/min/player.js',                'variable': 'player_js',         'outfile': 'player.js.h'},
    {'infile': '../embed/min/wrappers/html5.js',        'variable': 'html5_js',          'outfile': 'html5.js.h'},
    {'infile': '../embed/min/wrappers/flash_strobe.js', 'variable': 'flash_strobe_js',   'outfile': 'flash_strobe.js.h'},
    {'infile': '../embed/min/wrappers/dashjs.js',       'variable': 'dash_js',           'outfile': 'dashjs.js.h'},
    {'infile': '../embed/min/wrappers/videojs.js',      'variable': 'video_js',          'outfile': 'videojs.js.h'},
    {'infile': '../embed/min/wrappers/wheprtc.js',      'variable': 'wheprtc_js',         'outfile': 'wheprtc.js.h'},
    {'infile': '../embed/min/wrappers/webrtc.js',       'variable': 'webrtc_js',         'outfile': 'webrtc.js.h'},
    {'infile': '../embed/min/wrappers/mews.js',         'variable': 'mews_js',           'outfile': 'mews.js.h'},
    {'infile': '../embed/min/wrappers/flv.js',          'variable': 'flv_js',            'outfile': 'flv.js.h'},
    {'infile': '../embed/min/wrappers/hlsjs.js',        'variable': 'hlsjs_js',          'outfile': 'hlsjs.js.h'},
    {'infile': '../embed/min/wrappers/rawws.js',        'variable': 'rawws_js',          'outfile': 'rawws.js.h'},
    {'infile': '../embed/min/wrappers/rawwscanvas.js',  'variable': 'rawwscanvas_js',   'outfile': 'rawwscanvas.js.h'},
    {'infile': '../embed/min/wrappers/webcodecsworker.js','variable': 'wcworker_js',     'outfile': 'wcworker.js.h'},
    {'infile': '../embed/players/dash.js.license.js',   'variable': 'player_dash_lic_js','outfile': 'player_dash_lic.js.h'},
    {'infile': '../embed/players/dash.all.min.js',      'variable': 'player_dash_js',    'outfile': 'player_dash.js.h'},
    {'infile': '../embed/players/video.min.js',         'variable': 'player_video_js',   'outfile': 'player_video.js.h'},
    {'infile': '../embed/players/flv.min.js',           'variable': 'player_flv_js',     'outfile': 'player_flv.js.h'},
    {'infile': '../embed/players/hls.js',               'variable': 'player_hlsjs_js',   'outfile': 'player_hlsjs.js.h'},
    {'infile': '../embed/players/libde265.min.js',      'variable': 'player_libde265_js','outfile': 'player_libde265.js.h'},
    {'infile': '../embed/min/skins/default.css',        'variable': 'skin_default_css',  'outfile': 'skin_default.css.h'},
    {'infile': '../embed/min/skins/dev.css',            'variable': 'skin_dev_css',      'outfile': 'skin_dev.css.h'},
    {'infile': '../embed/skins/video-js.css',           'variable': 'skin_videojs_css',  'outfile': 'skin_videojs.css.h'},
]

embed_tgts = []

foreach e : embed_files
  embed_tgts += custom_target('embed_'+e.get('outfile'), output: e.get('outfile'), input: e.get('infile'), command: [sourcery, '@INPUT@', e.get('variable'), '@OUTPUT@'])
endforeach


server_html = custom_target('embed_server.html', output: 'server.html.h', input: gen_html, command: [sourcery, '@INPUT@', 'server_html', '@OUTPUT@'])

# Translation catalogs, embedded as a lookup table so the controller can serve
# /translations/<code>.json without a per-language code change.
gen_lang_header = files('../scripts/gen_lang_header.py')
lang_catalogs = custom_target('lang_catalogs',
  output: 'lang_catalogs.h',
  input: lang_json_tgts,
  command: [py_installation, gen_lang_header, '@OUTPUT@'] + lang_specs + ['--', '@INPUT@'])

# Backend (C++) translation catalogs. Separate from the LSP ones above: these
# are consumed by the controller itself (to translate API responses, since
# the API is also used standalone / by other front-ends), never served to a
# browser. --prefix keeps the generated types/variables from colliding with
# the frontend ones if both headers are ever included in the same file;
# --no-index skips the code -> native-name index JSON the frontend uses for
# its language picker, since nothing here ever lists backend languages.
backend_languages = [
  {'code': 'de-DE', 'name': 'Deutsch'},
]

backend_lang_json_tgts = []
backend_lang_specs = []

foreach l : backend_languages
  backend_lang_json_tgts += custom_target('backend_lang_json_' + l.get('code'),
    input: '../src/lang/' + l.get('code') + '.po',
    output: 'backend_' + l.get('code') + '.json',
    command: [py_installation, po2json, '@INPUT@', '@OUTPUT@'])
  backend_lang_specs += l.get('code') + '=' + l.get('name')
endforeach

backend_lang_catalogs = custom_target('backend_lang_catalogs',
  output: 'backend_lang_catalogs.h',
  input: backend_lang_json_tgts,
  command: [py_installation, gen_lang_header, '@OUTPUT@', '--prefix', 'Backend', '--no-index'] + backend_lang_specs + ['--', '@INPUT@'])
