gio = dependency('gio-2.0')
threads = dependency('threads')
math = meson.get_compiler('c').find_library('m', required: true)

executable('zyl-sensors',
  'sensors.c',
  dependencies: [gio, threads, math],
  install: true,
)
