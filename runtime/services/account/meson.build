gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0')
openssl = dependency('openssl')

executable('zyl-account',
  'account.c',
  dependencies: [gio, json_glib, openssl],
  install: true,
)
