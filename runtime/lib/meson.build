# runtime/lib/meson.build — Zyl OS shared runtime code
# Layer: Runtime / Library
# Role: Code more than one OS component compiles — once, here, instead of one
#       service directory serving as another's library
# Scope: app containers (appdata, fs, appstore, WAM), OTA/package signature
#        checks (updater, appstore, seals), app seals (appstore, WAM), where
#        users and their files live (user, fs, appstore), reading JSON strings
#        (HAL, WAM, services), following a settings category (telephony, location),
#        per-file encryption of user data (lock, credential, messaging, user)
# Dependency direction: glib, json-glib, OpenSSL; nothing of the services
# SOLID: SRP — one small static library per concern, each with only its own deps

_lib_inc = include_directories('.')
_glib = dependency('glib-2.0')
_openssl = dependency('openssl', version: '>=1.1.1')

# /data/users and the owner's files (users.h): header only
zyl_users_dep = declare_dependency(include_directories: _lib_inc)

# A service following one settings category (the switches the user flips —
# location, airplane mode — kept by the service that does the work)
zyl_settings_follow_dep = declare_dependency(
  link_with: static_library('zyl-settings-follow', 'settings_follow.c',
                            dependencies: [dependency('gio-2.0')]),
  include_directories: _lib_inc,
  dependencies: [dependency('gio-2.0')],
)

# User data's per-file encryption (fscrypt v2): the lock service puts the key on
# and gives directories their policy; credential, messaging and user wait while locked
zyl_fscrypt_dep = declare_dependency(
  link_with: static_library('zyl-fscrypt', 'fscrypt.c', dependencies: [dependency('gio-2.0')]),
  include_directories: _lib_inc,
  dependencies: [dependency('gio-2.0')],
)

# The one way runtime/ reads a JSON string member (zyl_json.h): header only
zyl_json_dep = declare_dependency(include_directories: _lib_inc,
                                  dependencies: dependency('json-glib-1.0'))

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
