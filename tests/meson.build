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
