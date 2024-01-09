process_common = static_library('mist_process_common',
                            output_ebml_cpp,
                            input_ebml_cpp,
                            input_cpp,
                            output_http_cpp,
                            output_cpp,
                            io_cpp,
                            header_tgts,
                            dependencies: libmist_dep,
                            install: false,
)
process_common_dep = declare_dependency(link_with: process_common)

executables += {
    'name' : 'MistProcFFMPEG',
    'sources' : [
        files('process_ffmpeg.cpp'),
        header_tgts
    ],
    'deps' :[libmist_dep, process_common_dep],
    'defines': [],
}

executables += {
    'name' : 'MistProcMKVExec',
    'sources' : [
        files('process_exec.cpp'),
        header_tgts
    ],
    'deps' :[libmist_dep, process_common_dep],
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
    'deps' :[libmist_dep],
    'defines': [],
}

