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

### Shell-based tests

```bash
# Check all JS files for syntax errors
bash tests/test_js_syntax.sh

# Validate all app.json manifest files
bash tests/test_manifests.sh
```

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
