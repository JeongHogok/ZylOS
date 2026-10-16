gio = dependency('gio-2.0')
json_glib = dependency('json-glib-1.0')
libzip = dependency('libzip')
openssl = dependency('openssl', version: '>=1.1.1')

executable('zyl-appstore',
  'appstore.c', 'store_catalog.c', 'store_package.c',
  dependencies: [gio, json_glib, libzip, openssl, zyl_hal_dep,
                 zyl_container_dep, zyl_seal_dep, zyl_ota_verify_dep, zyl_json_dep],
  install: true,
)
