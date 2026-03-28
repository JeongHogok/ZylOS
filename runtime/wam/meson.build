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

executable('bpi-wam',
  'src/wam.c',
  dependencies: deps,
  install: true,
)
