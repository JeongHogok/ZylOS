libseccomp = dependency('libseccomp', version: '>=2.5.0')

static_library('zyl-sandbox',
  'sandbox.c',
  dependencies: [libseccomp],
  install: false,
)
