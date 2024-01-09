
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
    {'name': 'FLAC', 'format': 'flac'},
]

foreach analyser : analysers
  executables += { 'name': 'MistAnalyser'+analyser.get('name'),
                 'sources' : [
                    files('mist_analyse.cpp',
                          'analyser.cpp',
                          'analyser_'+analyser.get('format')+'.cpp'),
                    header_tgts,
                 ],
                 'defines': [
                    string_opt.format('ANALYSERHEADER', 'analyser_'+analyser.get('format')+'.h'),
                    '-DANALYSERTYPE=Analyser'+analyser.get('name')
                 ],
                 'deps': [libmist_dep]
                }
endforeach

executables += {
  'name': 'MistTranslateH264', 
  'sources' : [files('h264_translate.cpp'),  header_tgts],
  'defines': [],
  'deps' : [libmist_dep]
}

