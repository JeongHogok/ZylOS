gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0')
libzip = dependency('libzip')
openssl = dependency('openssl', version: '>=1.1.1')

executable('zyl-appstore',
  'appstore.c', 'store_catalog.c', 'store_package.c', 'app_seal.c',
  '../appdata/container.c', '../updater/ota_verify.c',
  dependencies: [gio, json_glib, libzip, openssl, zyl_hal_dep],
  install: true,
)
