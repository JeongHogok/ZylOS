# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Test Runner
#
# 역할: Zyl OS 단위 테스트 빌드 정의
# 수행범위: gesture detection, manifest parsing, notification service 테스트
# 의존방향: gio-2.0, 각 테스트 대상 소스 파일
# SOLID: SRP — 테스트 빌드 구성만 담당
# ──────────────────────────────────────────────────────────

test_deps = [dependency('gio-2.0')]

# Test: gesture detection algorithm
test('gesture-detection',
  executable('test-gesture',
    'test_gesture.c',
    '../compositor/src/input/gesture.c',
    include_directories: include_directories(
      '../compositor/include',
      '../compositor/src',
      '../compositor/src/input',
    ),
    dependencies: test_deps,
  )
)

# Test: app.json manifest parsing
test('manifest-parsing',
  executable('test-manifest',
    'test_manifest.c',
    dependencies: test_deps,
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
    dependencies: test_deps,
  )
)
