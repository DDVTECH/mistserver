header_tgts = []

foreach header : headers
  header_tgts += custom_target('copy'+header, input: join_paths(meson.source_root(),'lib',header), output: header, command: ['cp', '@INPUT@', '@OUTPUT@'])
endforeach