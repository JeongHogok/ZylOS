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
    include_directories: include_directories('../runtime/services/telephony'),
    dependencies: [gio_dep],
  )
)

# Test: bootloader lock requests (GPLv3 §6 unlock path) against fake
# fw_printenv/fw_setenv, and the variable names in board/bpi-f3/u-boot-env.txt
test('bootctl',
  executable('test-bootctl',
    'test_bootctl.c',
    dependencies: [gio_dep],
    c_args: ['-DZYL_TEST'],
  ),
  workdir: meson.project_source_root(),
)

# Test: device app scan — a user app must not replace a system app by reusing its id
test('manifest-scan',
  executable('test-manifest-scan',
    'test_manifest_scan.c',
    dependencies: [gio_dep, json_glib_dep],
  )
)
