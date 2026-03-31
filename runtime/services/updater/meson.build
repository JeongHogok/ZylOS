openssl = dependency('openssl', version: '>=1.1.1')
json_glib = dependency('json-glib-1.0')

static_library('zyl-updater',
  'updater.c',
  dependencies: [openssl, json_glib],
  install: false,
)
