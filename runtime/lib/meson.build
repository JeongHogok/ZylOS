# runtime/lib/meson.build — Zyl OS shared runtime code
# Layer: Runtime / Library
# Role: Code more than one OS component compiles — once, here, instead of one
#       service directory serving as another's library
# Scope: app containers (appdata, fs, appstore, WAM), OTA/package signature
#        checks (updater, appstore, seals), app seals (appstore, WAM)
# Dependency direction: glib, json-glib, OpenSSL; nothing of the services
# SOLID: SRP — one small static library per concern, each with only its own deps

_lib_inc = include_directories('.')
_glib = dependency('glib-2.0')
_openssl = dependency('openssl', version: '>=1.1.1')

# /data/appdata/<app id>/ — where an app's everything lives (CLAUDE.md rule 12)
zyl_container_dep = declare_dependency(
  link_with: static_library('zyl-container', 'container.c', dependencies: [_glib]),
  include_directories: _lib_inc,
  dependencies: [_glib],
)

# SHA-256 + RSA-SHA256 over it (tools/build-ota.sh, tools/build-store.py)
zyl_ota_verify_dep = declare_dependency(
  link_with: static_library('zyl-ota-verify', 'ota_verify.c', dependencies: [_openssl]),
  include_directories: _lib_inc,
  dependencies: [_openssl],
)

# An installed app runs only as sealed (CLAUDE.md rule 12)
_seal_deps = [_glib, dependency('json-glib-1.0'), zyl_ota_verify_dep]
zyl_seal_dep = declare_dependency(
  link_with: static_library('zyl-seal', 'app_seal.c', dependencies: _seal_deps),
  include_directories: _lib_inc,
  dependencies: _seal_deps,
)
