openssl = dependency('openssl', version: '>=1.1.1')
json_glib = dependency('json-glib-1.0')
gio = dependency('gio-2.0')

executable('zyl-updater', 'updaterd.c', 'updater.c', 'ota_install.c',
  dependencies: [openssl, json_glib, gio, zyl_hal_dep, zyl_ota_verify_dep],
  install: true,
)

# The key that signs OTA packages (tools/build-ota.sh signs with the boot key:
# give its .pub). Without it the device refuses every update.
if get_option('ota_public_key') != ''
  install_data(meson.project_source_root() / get_option('ota_public_key'),
    rename: 'ota-signing-key.pem', install_dir: '/etc/zyl-os')
endif
