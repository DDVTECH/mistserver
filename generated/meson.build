
sourcery = executable('sourcery', '../src/sourcery.cpp', native: true)

make_html = executable('make_html', 'make_html.cpp', native: true)

gen_html = custom_target('gen_html', output: 'server.html', input: html_files, command: [make_html, '@OUTPUT@', '@INPUT@'])

embed_files = [
    {'infile': '../embed/min/player.js',                'variable': 'player_js',         'outfile': 'player.js.h'},
    {'infile': '../embed/min/wrappers/html5.js',        'variable': 'html5_js',          'outfile': 'html5.js.h'},
    {'infile': '../embed/min/wrappers/flash_strobe.js', 'variable': 'flash_strobe_js',   'outfile': 'flash_strobe.js.h'},
    {'infile': '../embed/min/wrappers/dashjs.js',       'variable': 'dash_js',           'outfile': 'dashjs.js.h'},
    {'infile': '../embed/min/wrappers/videojs.js',      'variable': 'video_js',          'outfile': 'videojs.js.h'},
    {'infile': '../embed/min/wrappers/webrtc.js',       'variable': 'webrtc_js',         'outfile': 'webrtc.js.h'},
    {'infile': '../embed/min/wrappers/mews.js',         'variable': 'mews_js',           'outfile': 'mews.js.h'},
    {'infile': '../embed/min/wrappers/flv.js',          'variable': 'flv_js',            'outfile': 'flv.js.h'},
    {'infile': '../embed/min/wrappers/hlsjs.js',        'variable': 'hlsjs_js',          'outfile': 'hlsjs.js.h'},
    {'infile': '../embed/min/wrappers/rawws.js',        'variable': 'rawws_js',          'outfile': 'rawws.js.h'},
    {'infile': '../embed/players/dash.js.license.js',   'variable': 'player_dash_lic_js','outfile': 'player_dash_lic.js.h'},
    {'infile': '../embed/players/dash.all.min.js',      'variable': 'player_dash_js',    'outfile': 'player_dash.js.h'},
    {'infile': '../embed/players/video.min.js',         'variable': 'player_video_js',   'outfile': 'player_video.js.h'},
    {'infile': '../embed/players/webrtc.js',            'variable': 'player_webrtc_js',  'outfile': 'player_webrtc.js.h'},
    {'infile': '../embed/players/flv.min.js',           'variable': 'player_flv_js',     'outfile': 'player_flv.js.h'},
    {'infile': '../embed/players/hls.js',               'variable': 'player_hlsjs_js',   'outfile': 'player_hlsjs.js.h'},
    {'infile': '../embed/players/libde265.min.js',      'variable': 'player_libde265_js','outfile': 'player_libde265.js.h'},
    {'infile': '../embed/min/skins/default.css',        'variable': 'skin_default_css',  'outfile': 'skin_default.css.h'},
    {'infile': '../embed/min/skins/dev.css',            'variable': 'skin_dev_css',      'outfile': 'skin_dev.css.h'},
    {'infile': '../embed/skins/video-js.css',           'variable': 'skin_videojs_css',  'outfile': 'skin_videojs.css.h'},
    {'infile': '../src/output/noffmpeg.jpg',            'variable': 'noffmpeg',          'outfile': 'noffmpeg.h'},
    {'infile': '../src/output/noh264.jpg',              'variable': 'noh264',            'outfile': 'noh264.h'},
]

embed_tgts = []

foreach e : embed_files
  embed_tgts += custom_target('embed_'+e.get('outfile'), output: e.get('outfile'), input: e.get('infile'), command: [sourcery, '@INPUT@', e.get('variable'), '@OUTPUT@'])
endforeach


server_html = custom_target('embed_server.html', output: 'server.html.h', input: gen_html, command: [sourcery, '@INPUT@', 'server_html', '@OUTPUT@'])
