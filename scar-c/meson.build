project(
  'scar', 'c',
  default_options: [
    'c_std=c99',
    'warning_level=3',
  ]
)

cc = meson.get_compiler('c')
m_dep = cc.find_library('m')
zlib_dep = dependency('zlib')

args = []
if get_option('trace-errors')
  args += '-DSCAR_TRACE_ERROR'
endif

libscar = library(
  'scar',
  'src/compression/common.c',
  'src/compression/gzip.c',
  'src/compression/plain.c',
  'src/ioutil.c',
  'src/meta.c',
  'src/pax-syntax.c',
  'src/pax.c',
  'src/scar-reader.c',
  'src/scar-writer.c',
  c_args: args,
  dependencies: [zlib_dep, m_dep],
  install: true,
  include_directories: 'include/scar',
)

libscar_dep = declare_dependency(
  include_directories: 'include',
  link_with: libscar,
)

if host_machine.system() == 'windows'
  system = 'windows'
else
  system = 'posix'
endif

executable(
  'scar',
  'cmd/scar/platform/' + system + '.c',
  'cmd/scar/subcmds/cat.c',
  'cmd/scar/subcmds/convert.c',
  'cmd/scar/subcmds/create.c',
  'cmd/scar/subcmds/ls.c',
  'cmd/scar/subcmds/tree.c',
  'cmd/scar/main.c',
  'cmd/scar/rx.c',
  dependencies: libscar_dep,
  install: true,
)

executable(
  'test-scar',
  'test/main.c',
  'test/compression.t.c',
  'test/ioutil/block-reader.t.c',
  'test/ioutil/mem.t.c',
  'test/pax-syntax.t.c',
  dependencies: libscar_dep,
  include_directories: [
    'include/scar',
    'test',
  ],
  install: false,
)
