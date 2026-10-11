libseccomp = dependency('libseccomp', version: '>=2.5.0')
gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0')

static_library('zyl-sandbox',
  'sandbox.c',
  'app_uid.c',
  dependencies: [libseccomp, gio, json_glib],
  install: false,
)
