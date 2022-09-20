
MistProcFFMPEG = executable('MistProcFFMPEG',
                            'process_ffmpeg.cpp',
                            join_paths(meson.source_root(), 'src/output/output_ebml.cpp'),
                            join_paths(meson.source_root(), 'src/input/input_ebml.cpp'),
                            join_paths(meson.source_root(), 'src/input/input.cpp'),
                            join_paths(meson.source_root(), 'src/output/output_http.cpp'),
                            join_paths(meson.source_root(), 'src/output/output.cpp'),
                            join_paths(meson.source_root(), 'src/io.cpp'),
                            header_tgts,
                            include_directories: incroot,
                            link_with: libmist,
                            install: true     
)


MistProcMKVExec = executable('MistProcMKVExec',
                            'process_exec.cpp',
                            join_paths(meson.source_root(), 'src/output/output_ebml.cpp'),
                            join_paths(meson.source_root(), 'src/input/input_ebml.cpp'),
                            join_paths(meson.source_root(), 'src/input/input.cpp'),
                            join_paths(meson.source_root(), 'src/output/output_http.cpp'),
                            join_paths(meson.source_root(), 'src/output/output.cpp'),
                            join_paths(meson.source_root(), 'src/io.cpp'),
                            header_tgts,
                            include_directories: incroot,
                            link_with: libmist,
                            install: true     
)

MistProcLivepeer = executable('MistProcLivepeer',
                            'process_livepeer.cpp',
                            join_paths(meson.source_root(), 'src/input/input.cpp'),
                            join_paths(meson.source_root(), 'src/output/output_http.cpp'),
                            join_paths(meson.source_root(), 'src/output/output_ts_base.cpp'),
                            join_paths(meson.source_root(), 'src/output/output.cpp'),
                            join_paths(meson.source_root(), 'src/io.cpp'),
                            header_tgts,
                            include_directories: incroot,
                            link_with: libmist,
                            install: true     
)

