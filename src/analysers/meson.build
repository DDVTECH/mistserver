
analysers = [
    {'name': 'RTMP', 'format': 'rtmp'},
    {'name': 'FLV', 'format': 'flv'},
    {'name': 'DTSC', 'format': 'dtsc'},
    {'name': 'OGG', 'format': 'ogg'},
    {'name': 'EBML', 'format': 'ebml'},
    {'name': 'TS', 'format': 'ts'},
    {'name': 'MP4', 'format': 'mp4'},
    {'name': 'H264', 'format': 'h264'},
    {'name': 'HLS', 'format': 'hls'},
    {'name': 'RIFF', 'format': 'riff'},
    {'name': 'RTSP', 'format': 'rtsp'},
]

analysers_tgts = []

foreach analyser : analysers
  analysers_tgts += executable('MistAnalyser'+analyser.get('name'),
                                'mist_analyse.cpp',
                                'analyser.cpp',
                                'analyser_'+analyser.get('format')+'.cpp',
                                header_tgts,
                                link_with: libmist,
                                include_directories: incroot,
                                cpp_args: [string_opt.format('ANALYSERHEADER', 'analyser_'+analyser.get('format')+'.h'), '-DANALYSERTYPE=Analyser'+analyser.get('name')],
                                install: true,                             
  )
endforeach

analysers_tgts += executable('MistTranslateH264', 'h264_translate.cpp',  header_tgts, link_with: libmist, include_directories: incroot, install: true)