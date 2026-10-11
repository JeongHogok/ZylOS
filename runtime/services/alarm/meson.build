gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0')

executable('zyl-alarm',
  'alarm.c',
  dependencies: [gio, json_glib],
  c_args: ['-D_GNU_SOURCE'],
  install: true,
)
