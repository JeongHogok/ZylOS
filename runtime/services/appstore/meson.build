openssl = dependency('openssl', version: '>=1.1.1')
libzip = dependency('libzip', required: false)
json_glib = dependency('json-glib-1.0')
gio = dependency('gio-2.0')

appstore_deps = [openssl, json_glib, gio]
appstore_c_args = []
if libzip.found()
  appstore_deps += libzip
  appstore_c_args += '-DHAVE_LIBZIP=1'
endif

static_library('zyl-appstore',
  'appstore.c',
  dependencies: appstore_deps,
  c_args: appstore_c_args,
  install: false,
)
