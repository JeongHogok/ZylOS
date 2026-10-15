# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Runner
#
# 역할: Zyl OS 단위 테스트 빌드 정의
# 수행범위: gesture detection, manifest parsing, notification service 테스트
# 의존방향: gio-2.0, json-glib-1.0, 각 테스트 대상 소스 파일
# SOLID: SRP — 테스트 빌드 구성만 담당
# ──────────────────────────────────────────────────────────

gio_dep = dependency('gio-2.0')
json_glib_dep = dependency('json-glib-1.0')

# Test: gesture detection algorithm
# Self-contained — replicates gesture_detect() logic to avoid wlroots deps
test('gesture-detection',
  executable('test-gesture',
    'test_gesture.c',
  )
)

# Test: app.json manifest parsing
test('manifest-parsing',
  executable('test-manifest',
    'test_manifest.c',
    dependencies: [gio_dep, json_glib_dep],
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

# Test: sandbox permission vocabulary mapping (Linux only — uses seccomp/namespaces)
if host_machine.system() == 'linux'
  test('sandbox-permissions',
    executable('test-sandbox-permissions',
      'test_sandbox_permissions.c',
      include_directories: include_directories(
        '../runtime/services/sandbox',
      ),
      dependencies: [gio_dep, json_glib_dep,
                     dependency('libseccomp', version: '>=2.5.0')],
    )
  )
endif

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
# fw_printenv/fw_setenv, and the variable names in board/bpi-f3/u-boot-env.txt
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
    include_directories: include_directories('../runtime/hal', '../runtime/services/updater'),
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

# Test: device app scan — a user app must not replace a system app by reusing its id
test('manifest-scan',
  executable('test-manifest-scan',
    'test_manifest_scan.c',
    dependencies: [gio_dep, json_glib_dep],
  )
)
