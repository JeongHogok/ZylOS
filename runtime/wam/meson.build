project('bpi-wam', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

gtk4 = dependency('gtk4')
webkitgtk = dependency('webkitgtk-6.0', required: false)
if not webkitgtk.found()
  webkitgtk = dependency('webkit2gtk-4.1')
endif
json_glib = dependency('json-glib-1.0')
gio = dependency('gio-2.0')

deps = [gtk4, webkitgtk, json_glib, gio]

wam_sources = files(
  'src/wam.c',
  'src/manifest/manifest.c',
  'src/lifecycle/lifecycle.c',
  'src/bridge/bridge.c',
  'src/dbus/dbus_service.c',
)

executable('bpi-wam',
  wam_sources,
  dependencies: deps,
  install: true,
)

# Install the JS bridge script to the runtime data directory
install_data('src/bridge/bridge.js',
  install_dir: get_option('datadir') / 'bpi-os' / 'wam',
)
