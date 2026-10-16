# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Runner
#
# 역할: Zyl OS 단위 테스트 빌드 정의
# 수행범위: 서비스·WAM·HAL·라이브러리 단위 테스트, 사설 버스 종단 테스트
# 의존방향: gio-2.0, json-glib-1.0, 각 테스트 대상 소스 파일
# SOLID: SRP — 테스트 빌드 구성만 담당
# ──────────────────────────────────────────────────────────

gio_dep = dependency('gio-2.0')
# json-glib and the one way runtime/ reads its string members (runtime/lib/zyl_json.h)
json_glib_dep = [dependency('json-glib-1.0'), zyl_json_dep]
# The shared runtime code (runtime/lib): tests compile its sources themselves
# so their own -D paths apply, and need its headers for what they include
_lib_inc = include_directories('../runtime/lib')

# Test: system gesture judgement — only edge touches are gestures; the real
# compositor/src/input/gesture_detect.c (no wlroots), not a copy of it
test('gesture-detection',
  executable('test-gesture',
    'test_gesture.c',
    '../compositor/src/input/gesture_detect.c',
    include_directories: include_directories('../compositor/include'),
  )
)

# Test: notification service
test('notification',
  executable('test-notification',
    'test_notification.c',
    '../runtime/services/notification/notification.c',
    include_directories: include_directories(
      '../runtime/services/notification',
    ),
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: bridge dispatch/response routing using the real bridge.c with mock WebKit.
test('bridge-dispatch',
  executable('test-bridge-dispatch',
    'test_bridge_dispatch.c',
    include_directories: include_directories(
      '.',
      'include',
    ),
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_USE_WEBKIT2GTK'],
  )
)

# Test: telemetry opt-in consent (no collection or identifier before consent,
# erasure on withdrawal). Uses the real daemon source with main() compiled out.
test('telemetry-consent',
  executable('test-telemetry-consent',
    'test_telemetry_consent.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: data usage accounting (double-count guards, billing cycle, mobile
# data limit). Uses the real daemon source with main() compiled out.
test('netstats',
  executable('test-netstats',
    'test_netstats.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: emergency alerts (Cell Broadcast) — classification, repeats, history
test('cbs',
  executable('test-cbs',
    'test_cbs.c',
    '../runtime/services/telephony/cbs.c',
    include_directories: include_directories('../runtime/services/telephony'),
    dependencies: [gio_dep],
  )
)

# Test: the real telephony daemon against a fake ModemManager on a private
# bus and a fake nmcli — emergency alerts (channel setup, delivery, ack) and
# the mobile data connection (IPv4v6, CLAT, roaming). Skips without dbus-daemon.
test('telephony-dbus',
  executable('test-telephony-dbus',
    'test_telephony_dbus.c',
    '../runtime/services/telephony/cbs.c',
    '../runtime/services/telephony/calllog.c',
    include_directories: include_directories('../runtime/services/telephony'),
    dependencies: [gio_dep],
  )
)

# Test: bootloader lock requests (GPLv3 §6 unlock path) against fake
# fw_printenv/fw_setenv, and the variable names in board/boot-policy.txt
test('bootctl',
  executable('test-bootctl',
    'test_bootctl.c',
    '../runtime/hal/hal_boot.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  ),
  workdir: meson.project_source_root(),
)

# Test: settings persistence (defaults + user layer, corrupt file kept,
# JSON ↔ D-Bus value round trip, the shipped defaults file)
test('settings',
  executable('test-settings',
    'test_settings.c',
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_TEST'],
  ),
  workdir: meson.project_source_root(),
)

# Test: screen lock credential (hash-only storage, failure waits that survive
# a restart, current PIN to change/remove)
test('lock',
  executable('test-lock',
    'test_lock.c',
    dependencies: [gio_dep, dependency('openssl', version: '>=1.1.1')],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: OTA install — signed package only, inactive slot's rootfs/verity/FIT
# only, U-Boot pointed at it after every write (fake partitions and fw_*env)
test('updater',
  executable('test-updater',
    'test_updater.c',
    include_directories: [include_directories('../runtime/hal', '../runtime/services/updater'), _lib_inc],
    dependencies: [gio_dep, json_glib_dep, dependency('openssl', version: '>=1.1.1')],
  ),
  workdir: meson.project_source_root(),
)

# Test: Wi-Fi HAL over nmcli (list parsing, Wi-Fi/AP presence, IPv6-shared
# hotspot) and the service's hotspot name/passphrase rules
test('wifi',
  executable('test-wifi',
    'test_wifi.c',
    '../runtime/services/wifi/captive_portal.c',
    include_directories: include_directories('../runtime/services/wifi', '../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: call log policy — outgoing/incoming/missed, duration, unique ids, the
# 500-entry cap, persistence, delete
test('calllog',
  executable('test-calllog',
    'test_calllog.c',
    '../runtime/services/telephony/calllog.c',
    include_directories: include_directories('../runtime/services/telephony'),
    dependencies: [gio_dep],
  )
)

# Test: Audio HAL — speakerphone only with both earpiece and loudspeaker,
# output volume, microphone mute (fake wpctl/pactl)
test('hal-audio',
  executable('test-hal-audio',
    'test_hal_audio.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep, json_glib_dep, meson.get_compiler('c').find_library('m')],
  )
)

# Test: Vibrator HAL — the motor is the input device with FF_RUMBLE (fake sysfs)
test('hal-vibrator',
  executable('test-hal-vibrator',
    'test_hal_vibrator.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep, dependency('threads')],
  )
)

# Test: Bluetooth HAL against a fake BlueZ on a private bus (adapter discovery,
# device list, power, pairing path, no adapter)
test('hal-bt',
  executable('test-hal-bt',
    'test_hal_bt.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
  )
)

# Test: Display/Battery/Torch/CPU HAL against a fake sysfs (DRM outputs,
# backlight range, no backlight, battery and a battery-less board, flash LED, cpufreq)
test('hal-power',
  executable('test-hal-power',
    'test_hal_power.c',
    '../runtime/hal/hal_display.c',
    '../runtime/hal/hal_battery.c',
    '../runtime/hal/hal_torch.c',
    '../runtime/hal/hal_cpu.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_SYSFS_ROOT="/tmp/zyl-hal-power-test"'],
  )
)

# Test: file service path policy (root, .., links, protected paths), listing,
# text vs explicit base64, rename/remove
test('fs-service',
  executable('test-fs-service',
    'test_fs_service.c',
    '../runtime/lib/container.c',
    include_directories: _lib_inc,
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: contacts (only the fields given change, search, 0600) and messages
# (thread ids are safe file names, unread from received only, send status)
test('contacts-messaging',
  executable('test-contacts-messaging',
    'test_contacts_messaging.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: clipboard (source app, size cap, sensitive values clear themselves) and
# network fetch policy (https, allowed hosts only, no user info / port / IP)
test('clipboard-network',
  executable('test-clipboard-network',
    'test_clipboard_network.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: app data — each app only its own, ids cannot leave the root, quota
test('appdata',
  executable('test-appdata',
    'test_appdata.c',
    '../runtime/lib/container.c',
    include_directories: _lib_inc,
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: intents — who handles one (manifest "intents", mime rules, not the
# starting app, explicit target) and what the handler gets
test('intent',
  executable('test-intent',
    'test_intent.c',
    '../runtime/wam/src/intent/intent.c',
    '../runtime/wam/src/manifest/manifest.c',
    dependencies: [gio_dep, json_glib_dep],
  )
)

# Test: shell policy — what may be in front during setup and while locked,
# who unlocks and who finishes setup (same rules as apps/system/shell.js)
test('shell',
  executable('test-shell',
    'test_shell.c',
    '../runtime/wam/src/shell/shell.c',
    dependencies: [gio_dep],
  )
)

# Test: app origins zyl-app:// — an app reads its own files and the OS shared
# code, nothing else (.., links out, directories)
test('app-scheme',
  executable('test-app-scheme',
    'test_app_scheme.c',
    '../runtime/wam/src/scheme/app_scheme.c',
    dependencies: [gio_dep],
  )
)

# Test: device app scan — a user app must not replace a system app by reusing its id
test('manifest-scan',
  executable('test-manifest-scan',
    'test_manifest_scan.c',
    dependencies: [gio_dep, json_glib_dep],
  )
)

# ─── Services without a test before (credential, user, storage, input, power,
#     display, camera, location, usb, logger, accessibility, audio, bluetooth,
#     device) ───

# Test: keychain — AES-256-GCM round trip, tampered/truncated files and a
# wrong passphrase refused, unsafe or path-truncating names refused
test('credential',
  executable('test-credential',
    'test_credential.c',
    dependencies: [gio_dep, dependency('openssl', version: '>=1.1.1')],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: users — one owner, uid and data dir per user, limit, switch, remove
# wipes data, a new user after restart does not inherit a forgotten one's files
test('user',
  executable('test-user',
    'test_user.c',
    include_directories: _lib_inc,
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: storage — statvfs of the data partition (fallback /), GetUsage fields
# the same as the emulator's { total, used, available, percent }
test('storage',
  executable('test-storage',
    'test_storage.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: power state machine — screen off/on keeps the user's brightness, idle
# dims to 30% then off, user activity keeps it awake, wakelocks (hold, cap,
# 10-minute expiry, timers resume after the last release), doze → suspend →
# back to the state it left (fake sysfs; StateChanged over a socket-pair D-Bus)
test('power',
  executable('test-power',
    'test_power.c',
    '../runtime/services/power/power_cpu.c',
    '../runtime/services/power/power_display.c',
    '../runtime/services/power/power_wakelock.c',
    '../runtime/hal/hal_display.c',
    '../runtime/hal/hal_cpu.c',
    '../runtime/hal/hal_battery.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST', '-DZYL_SYSFS_ROOT="/tmp/zyl-power-test"'],
  )
)

# Test: input — evdev keys to ZylHardwareKey and HardwareKeyPressed (press
# only), what is not a hardware key, keys from every device (power and volume
# apart, an unplugged one dropped, a touchscreen not opened), the virtual
# keyboard's layouts. The Input HAL over FIFO device nodes and a fake evdev ioctl
test('input',
  executable('test-input',
    'test_input.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: display — modes of the connected output, rotation and scale limits,
# auto-rotate (steady half second, lying flat keeps it, malformed SensorEvent)
test('display',
  executable('test-display',
    'test_display.c',
    '../runtime/hal/hal_display.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep, meson.get_compiler('c').find_library('m')],
    c_args: ['-DZYL_TEST', '-DZYL_SYSFS_ROOT="/tmp/zyl-display-test"'],
  )
)

# Test: camera — only a capture device opens, captures stay in the camera
# directory, the first frame is waited for, no buffer or device is left behind
# a capture, a driver that will not do YUYV is refused, padded rows come out
# packed (the V4L2 Camera HAL over a fake V4L2 driver)
test('camera',
  executable('test-camera', 'test_camera.c', '../runtime/hal/hal_torch.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: location — malformed, huge, NaN/inf or off-the-globe GeoIP/Wi-Fi
# answers give no fix, gpsd's unknown fields never reach apps as NaN,
# GPS > Wi-Fi > GeoIP fallback, geofence limits and enter/exit.
# Built like the service: curl and gpsd when the system has them
_libcurl_dep = dependency('libcurl', required: false)
_libgps_dep = dependency('libgps', required: false)
if _libcurl_dep.found()
  test('location',
    executable('test-location', 'test_location.c',
      dependencies: [gio_dep, _libcurl_dep, _libgps_dep, dependency('threads'),
                     meson.get_compiler('c').find_library('m')],
      c_args: ['-DZYL_TEST', '-DHAVE_CURL'] + (_libgps_dep.found() ? ['-DHAVE_GPSD'] : []),
    )
  )
endif

# Test: USB gadget modes — only the function links change, charging really
# unbinds, a failed bind or no controller is a failure, unknown names refused,
# connection from the UDC state (fake configfs and UDC class)
test('usb',
  executable('test-usb', 'test_usb.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: logger — one valid JSON line per entry whatever the message (control
# characters, a huge one, UTF-8 cut at the limit), no crash report faked by a
# message, a fatal signal still kills, rotation at the size limit keeping five
# old logs, the filter level across a restart
test('logger',
  executable('test-logger',
    'test_logger.c',
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: accessibility over a peer D-Bus — font scale range (NaN refused),
# StateChanged, screen reader start, spoken text never an espeak-ng option
# (fake espeak-ng/orca/at-spi2-registryd)
test('accessibility',
  executable('test-accessibility',
    'test_accessibility.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: sound policy over a peer D-Bus — volume clamping, stream names, silent
# mode (key clicks quiet, alerts sound, restored from Settings), mute, the
# vibration setting (fake Audio/Vibrator HAL, fake Settings)
test('audio',
  executable('test-audio',
    'test_audio.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep, dependency('threads')],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: Bluetooth device list — fields, unreadable list, the 64 cap, a name
# cut inside a character (fake BT HAL)
test('bluetooth',
  executable('test-bluetooth',
    'test_bluetooth.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: device info — the board's values and their format, the first connected
# output's preferred mode, unknowns left empty (fake Board/Display HAL)
test('device',
  executable('test-device',
    'test_device.c',
    include_directories: include_directories('../runtime/hal'),
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# ─── App store ───
_openssl_dep = dependency('openssl', version: '>=1.1.1')
_libzip_dep = dependency('libzip')
_store_src = ['../runtime/services/appstore/store_catalog.c', '../runtime/services/appstore/store_package.c',
              '../runtime/lib/app_seal.c', '../runtime/lib/container.c',
              '../runtime/lib/ota_verify.c', '../runtime/hal/hal_board.c']
_store_inc = [include_directories('../runtime/hal'), _lib_inc]

# Test: catalog validation, versions, safe unpacking (.., absolute, links),
# app.json must be the catalog's app, update swap, removal leaves nothing
test('store',
  executable('test-store', 'test_store.c', _store_src,
    include_directories: _store_inc,
    dependencies: [gio_dep, json_glib_dep, _libzip_dep, _openssl_dep],
    c_args: ['-DZYL_TEST', '-DZYL_SYSFS_ROOT="/tmp/zyl-store-test/sys"'],
  )
)

# Test: app seals — the store's own, a development build only where its
# store-signed certificate and profile allow (app, device, expiry, revocation),
# every file exactly as sealed
test('app-seal',
  executable('test-app-seal', 'test_app_seal.c',
    '../runtime/lib/app_seal.c', '../runtime/lib/ota_verify.c',
    dependencies: [gio_dep, json_glib_dep, _openssl_dep],
  ),
  workdir: meson.project_source_root(),
)

# Test: the sensor service polls the hardware it found without being asked
# (nothing on D-Bus starts polling, so GetLatest used to return 0 forever) —
# the Sensors HAL over a fake IIO sysfs
test('sensors-polling',
  executable('test-sensors-polling', 'test_sensors_polling.c',
    include_directories: include_directories('../runtime/services/sensors', '../runtime/hal'),
    dependencies: [gio_dep, dependency('threads'),
                   meson.get_compiler('c').find_library('m')],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: Sensors HAL — IIO channels per kind (one chip may carry two), raw × scale,
# an unreadable value is no reading, over-long device paths skipped (fake sysfs)
test('hal-sensors',
  executable('test-hal-sensors', 'test_hal_sensors.c',
    dependencies: [gio_dep, dependency('threads'),
                   meson.get_compiler('c').find_library('m')],
  )
)

# Test: a removed app's alarms go, from memory and from disk
test('alarm',
  executable('test-alarm', 'test_alarm.c',
    include_directories: include_directories('../runtime/services/alarm'),
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test: push — topics, endpoints, the one stream, messages only, state that survives
test('push',
  executable('test-push', 'test_push.c',
    dependencies: [gio_dep, json_glib_dep],
    c_args: ['-DZYL_TEST'],
  )
)

# Test (end to end): push — a local ntfy-like server over HTTPS, the push and
# notification daemons on a private bus, built with paths under /tmp
_push_e2e = '/tmp/zyl-push-e2e'
_push_e2e_push = executable('e2e-push', '../runtime/services/push/push.c',
  dependencies: [gio_dep, json_glib_dep],
  c_args: ['-DPUSH_CONFIG="@0@/push.json"'.format(_push_e2e), '-DPUSH_DATA_DIR="@0@/data"'.format(_push_e2e)])
_push_e2e_notification = executable('e2e-push-notification',
  '../runtime/services/notification/notification.c',
  include_directories: include_directories('../runtime/services/notification'),
  dependencies: [gio_dep, json_glib_dep], c_args: ['-DNOTIF_PERSIST_DIR="@0@/notifications"'.format(_push_e2e)])
test('push-e2e', find_program('push_e2e.sh'),
  args: [_push_e2e_push, _push_e2e_notification],
  timeout: 90, is_parallel: false)

# Test (end to end): a signed store over local HTTPS, the real daemons on a
# private bus — install, the app makes data, files, a notification, web
# storage; uninstall leaves nothing of it; tampered package and catalog refused.
# The daemons are built with paths under /tmp so the test touches nothing else.
_e2e = '/tmp/zyl-store-e2e'
_e2e_appstore = executable('e2e-appstore',
  '../runtime/services/appstore/appstore.c', _store_src,
  include_directories: _store_inc,
  dependencies: [gio_dep, json_glib_dep, _libzip_dep, _openssl_dep],
  c_args: ['-DZYL_STORE_CONFIG="@0@/store.json"'.format(_e2e), '-DZYL_APPS_DIR="@0@/apps"'.format(_e2e),
           '-DZYL_SYSTEM_APPS_DIR="@0@/system"'.format(_e2e), '-DZYL_CONTAINERS_ROOT="@0@/appdata"'.format(_e2e),
           '-DZYL_FILES_ROOT="@0@/files"'.format(_e2e), '-DZYL_SYSFS_ROOT="@0@/sys"'.format(_e2e)])
_e2e_appdata = executable('e2e-appdata',
  '../runtime/services/appdata/appdata.c', '../runtime/lib/container.c',
  include_directories: _lib_inc, dependencies: [gio_dep], c_args: ['-DZYL_APPDATA_ROOT="@0@/appdata"'.format(_e2e)])
_e2e_fs = executable('e2e-fs',
  '../runtime/services/fs/fs.c', '../runtime/lib/container.c',
  include_directories: _lib_inc, dependencies: [gio_dep], c_args: ['-DZYL_FILES_ROOT="@0@/files"'.format(_e2e),
                                    '-DZYL_CONTAINERS_ROOT="@0@/appdata"'.format(_e2e)])
_e2e_notification = executable('e2e-notification',
  '../runtime/services/notification/notification.c',
  include_directories: include_directories('../runtime/services/notification'),
  dependencies: [gio_dep, json_glib_dep], c_args: ['-DNOTIF_PERSIST_DIR="@0@/notifications"'.format(_e2e)])
# Test: the Zyl service bus — services own their names on the system bus and
# reach each other across it under the shipped policy. The services had been
# asking for the session bus while their units (Type=dbus + BusName=) waited on
# the system bus: on a device every service would sit in "activating" forever.
_bus = '/tmp/zyl-bus-test'
_bus_alarm = executable('bus-alarm',
  '../runtime/services/alarm/alarm.c',
  include_directories: include_directories('../runtime/services/alarm'),
  dependencies: [gio_dep, json_glib_dep],
  c_args: ['-DALARM_PERSIST_DIR="@0@/alarms"'.format(_bus)])
_bus_notification = executable('bus-notification',
  '../runtime/services/notification/notification.c',
  include_directories: include_directories('../runtime/services/notification'),
  dependencies: [gio_dep, json_glib_dep],
  c_args: ['-DNOTIF_PERSIST_DIR="@0@/notifications"'.format(_bus)])
test('dbus-policy', find_program('test_dbus_policy.sh'),
  args: [_bus_alarm, _bus_notification,
         meson.project_source_root() / 'system' / 'dbus-1' / 'zyl-os.conf'],
  timeout: 60, is_parallel: false)

test('store-e2e', find_program('store_e2e.sh'),
  args: [_e2e_appstore, _e2e_appdata, _e2e_fs, _e2e_notification,
         meson.project_source_root() / 'tools' / 'build-store.py'],
  timeout: 120, is_parallel: false)
