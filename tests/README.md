# Zyl OS Tests

## Unit Tests (M7)

Unit tests are built with Meson and use `assert()` for verification.

### Build and run all unit tests

```bash
meson setup builddir
meson test -C builddir
```

### Individual test targets

| Test | Binary | Description |
|------|--------|-------------|
| `gesture-detection` | `test-gesture` | Gesture detection algorithm (swipe direction, thresholds, edge zones) |
| `manifest-parsing` | `test-manifest` | app.json manifest parsing (required fields, invalid JSON, unknown fields) |
| `notification` | `test-notification` | Notification service (post, cancel, clear, channel filtering) |
| `bridge-dispatch` | `test-bridge-dispatch` | Real bridge.c dispatch/response routing with mock WebKit shim |

### Standalone WAM bridge test (manual compile)

`tests/test_bridge_dispatch.c` links the real `runtime/wam/src/bridge/bridge.c`
through a mock WebKit shim under `tests/include/`. The same test is also wired
into `tests/meson.build` as `bridge-dispatch`.

```bash
cc -std=c11 -Wall -Wextra -DZYL_USE_WEBKIT2GTK \
  -Itests -Itests/include -I. \
  tests/test_bridge_dispatch.c \
  $(pkg-config --cflags --libs glib-2.0 gio-2.0 json-glib-1.0) \
  -o /tmp/test-bridge-dispatch
/tmp/test-bridge-dispatch
```

### Shell-based tests

```bash
# Check all JS files for syntax errors
bash tests/test_js_syntax.sh

# Validate all app.json manifest files
bash tests/test_manifests.sh

# OS 이미지 독립성 검증 (에뮬레이터 의존 차단)
bash tests/check-os-independence.sh
```

## Full Codebase Verification

전체 코드베이스를 11개 섹션으로 자동 검증합니다:

```bash
bash tests/verify-all.sh
```

| 섹션 | 영역 | 검증 내용 |
|------|------|----------|
| [1] | apps/ | Tauri 참조, postMessage 직접호출, 에뮬레이터 경로 |
| [2] | apps/ | ES5 호환 (let/const/arrow 금지) |
| [3] | apps/ | i18n 하드코딩, 5개 언어 키 균등 |
| [4] | apps/ | Clean Architecture 헤더 (JS + CSS) |
| [5] | apps/ | backdrop-filter 소프트웨어 렌더링 폴백 |
| [6] | apps/ | Mock/Demo 데이터 금지 |
| [7] | apps/ | app.json 필수 필드 (id, name, version) |
| [8] | emulator-app/ | 비즈니스 로직 금지, Rust CA 헤더, cargo check |
| [9] | runtime/ | TODO/FIXME, 빈 함수, C CA 헤더 |
| [10] | system/ | systemd unit 파일 존재 |
| [11] | 전체 | 기술부채 총계, console.log, 하드코딩 비밀 |

### Git Pre-commit Hook

`apps/` 변경 시 자동 실행되어 다음을 차단합니다:
- `__TAURI__` 참조
- `window.parent.postMessage` 직접 호출
- ES5 위반 (let/const/arrow function)

## Integration Tests (M8)

End-to-end test that verifies the complete build pipeline, binary generation,
manifest validity, JS syntax, and systemd unit files.

```bash
bash tests/test_integration.sh
```

The integration test checks:
1. Meson build succeeds
2. All expected executables are produced
3. All app manifests are valid
4. All JS files have valid syntax
5. All systemd service files are valid
6. All required app directories exist
7. All system service files exist
8. All service directories have meson.build files

## Performance Profiling (M9)

```bash
bash tools/profile.sh
```

Reports:
- Build time measurement
- Source file counts by language
- Lines of code by language
- Binary sizes (if build succeeded)
- App directory sizes
- Memory usage estimates

## Requirements

- **Unit tests**: Meson, Ninja, GLib/GIO 2.0, json-glib-1.0
- **JS syntax check**: Node.js
- **Manifest validation**: Python 3
- **Integration tests**: All of the above
- **systemd verification**: systemd-analyze (optional, skipped if unavailable)
