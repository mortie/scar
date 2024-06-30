project('scar', 'c')

zlib_dep = dependency('zlib')

args = []
if get_option('trace-errors')
  args += '-DSCAR_TRACE_ERROR'
endif

libscar = library(
  'scar',
  'src/compression/gzip.c',
  'src/ioutil.c',
  'src/pax-meta.c',
  'src/pax-syntax.c',
  'src/pax.c',
  'src/scar-reader.c',
  'src/scar-writer.c',
  c_args: args,
  dependencies: zlib_dep,
  install: true,
  include_directories: 'include/scar',
)

libscar_dep = declare_dependency(
  include_directories: 'include',
  link_with: libscar,
)

executable(
  'scar',
  'cmd/scar/convert.c',
  'cmd/scar/list.c',
  'cmd/scar/main.c',
  'cmd/scar/util.c',
  dependencies: libscar_dep,
  install: true,
)

executable(
  'test-scar',
  'test/main.c',
  'test/compression/gzip.t.c',
  'test/ioutil.t.c',
  'test/pax-syntax.t.c',
  dependencies: libscar_dep,
  include_directories: [
    'include/scar',
    'test',
  ],
  install: false,
)
