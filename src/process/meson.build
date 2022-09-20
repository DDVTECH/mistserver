process_common = static_library('mist_process_common',
                            output_ebml_cpp,
                            input_ebml_cpp,
                            input_cpp,
                            output_http_cpp,
                            output_cpp,
                            io_cpp,
                            header_tgts,
                            include_directories: incroot,
                            install: false,
)

executables += {
    'name' : 'MistProcFFMPEG',
    'sources' : [
        files('process_ffmpeg.cpp'),
        header_tgts
    ],
    'link' : [libmist, process_common],
    'deps' :[],
    'defines': [],
}

executables += {
    'name' : 'MistProcMKVExec',
    'sources' : [
        files('process_exec.cpp'),
        header_tgts
    ],
    'link' : [libmist, process_common],
    'deps' :[],
    'defines': [],
}

executables += {
    'name' : 'MistProcLivepeer',
    'sources' : [
        files('process_livepeer.cpp'),
        input_cpp,
        output_http_cpp,
        output_ts_base_cpp,
        output_cpp,
        io_cpp,
        header_tgts
    ],
    'link' : [libmist],
    'deps' :[],
    'defines': [],
}

