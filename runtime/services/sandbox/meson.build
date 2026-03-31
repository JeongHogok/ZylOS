libseccomp = dependency('libseccomp', version: '>=2.5.0')

static_library('zyl-sandbox',
  'sandbox.c',
  'app_uid.c',
  dependencies: [libseccomp],
  install: false,
)
