openssl = dependency('openssl', version: '>=1.1.1')
libzip = dependency('libzip', required: false)

appstore_deps = [openssl]
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
