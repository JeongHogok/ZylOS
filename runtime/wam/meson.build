# runtime/wam/meson.build — Zyl OS Web Application Manager
# Layer: Runtime / WAM
# Role: Build the GTK4+WebKit web-app manager
# Scope: Compile WAM sources, link GTK4 + WebKitGTK + JSON-GLib
# Dependency direction: depends on gtk4, webkitgtk, json-glib, gio
# SOLID: Single-responsibility — only WAM build logic

gtk4 = dependency('gtk4')

# WebKitGTK: try multiple pkg-config names across distros
# webkitgtk-6.0 (Fedora 40+, future) | webkit2gtk-4.1 (Ubuntu/Debian) | webkit2gtk-4.0 (Arch)
wam_c_args = []
webkitgtk = dependency('webkitgtk-6.0', required: false)
if not webkitgtk.found()
  webkitgtk = dependency('webkit2gtk-4.1', required: false)
  if not webkitgtk.found()
    webkitgtk = dependency('webkit2gtk-4.0', required: false)
  endif
  if webkitgtk.found()
    wam_c_args += ['-DZYL_USE_WEBKIT2GTK']
  else
    error('WebKitGTK not found. Install one of: webkitgtk-6.0, webkit2gtk-4.1, webkit2gtk-4.0')
  endif
endif
json_glib = dependency('json-glib-1.0')
gio = dependency('gio-2.0')

wam_deps = [gtk4, webkitgtk, json_glib, gio]

wam_sources = files(
  'src/wam.c',
  'src/manifest/manifest.c',
  'src/lifecycle/lifecycle.c',
  'src/bridge/bridge.c',
  'src/dbus/dbus_service.c',
)

executable('zyl-wam',
  wam_sources,
  dependencies: wam_deps,
  c_args: wam_c_args,
  install: true,
)

# Install the JS bridge script to the runtime data directory
install_data('src/bridge/bridge.js',
  install_dir: get_option('datadir') / 'zyl-os' / 'wam',
)
