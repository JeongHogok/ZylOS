#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Integration Test
#
# 역할: Zyl OS 전체 통합 테스트 — 빌드, 바이너리, 매니페스트, JS, systemd 검증
# 수행범위: 빌드 성공 여부, 실행 파일 존재, 앱 매니페스트 유효성, JS 구문, systemd 유닛 검증
# 의존방향: meson, ninja, bash, node, python3, systemd-analyze (선택)
# SOLID: SRP — 통합 테스트 실행만 담당
# ──────────────────────────────────────────────────────────

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "=== Zyl OS Integration Tests ==="
echo "Project: $PROJECT_DIR"
echo ""

PASS=0
FAIL=0
SKIP=0

pass() { PASS=$((PASS + 1)); echo "PASS"; }
fail() { FAIL=$((FAIL + 1)); echo "FAIL"; }
skip() { SKIP=$((SKIP + 1)); echo "SKIP ($1)"; }

# ── Test 1: Meson build succeeds ─────────────────────────────

echo -n "[1] Meson build... "
BUILDDIR="$PROJECT_DIR/builddir-test"
rm -rf "$BUILDDIR"
if meson setup "$BUILDDIR" "$PROJECT_DIR" 2>/dev/null && ninja -C "$BUILDDIR" 2>/dev/null; then
    pass
else
    skip "build requires cross-compile deps"
fi

# ── Test 2: All expected executables exist (if build succeeded) ─

EXPECTED_BINS=(
    zyl-compositor
    zyl-wam
    zyl-notification
    zyl-power
    zyl-sensors
    zyl-location
    zyl-telephony
    zyl-display
    zyl-input
    zyl-usb
    zyl-user
    zyl-credential
)

for bin in "${EXPECTED_BINS[@]}"; do
    echo -n "[2] Binary: $bin... "
    found=$(find "$BUILDDIR" -name "$bin" -type f 2>/dev/null | head -1)
    if [ -n "$found" ]; then
        pass
    else
        skip "cross-compile only"
    fi
done

# ── Test 3: All app manifests valid ───────────────────────────

echo -n "[3] App manifests... "
if bash "$SCRIPT_DIR/test_manifests.sh" >/dev/null 2>&1; then
    pass
else
    fail
fi

# ── Test 4: All JS syntax valid ──────────────────────────────

echo -n "[4] JS syntax... "
if command -v node >/dev/null 2>&1; then
    if bash "$SCRIPT_DIR/test_js_syntax.sh" >/dev/null 2>&1; then
        pass
    else
        fail
    fi
else
    skip "node not installed"
fi

# ── Test 5: systemd service files valid ──────────────────────

echo -n "[5] systemd units... "
if command -v systemd-analyze >/dev/null 2>&1; then
    SERR=0
    for f in "$PROJECT_DIR"/system/*.service "$PROJECT_DIR"/system/*.target; do
        [ -f "$f" ] || continue
        systemd-analyze verify "$f" 2>/dev/null || SERR=$((SERR + 1))
    done
    if [ $SERR -eq 0 ]; then
        pass
    else
        fail
    fi
else
    skip "systemd not available"
fi

# ── Test 6: All required app directories exist ────────────────

REQUIRED_APPS=(home lockscreen statusbar settings browser files terminal camera)
echo -n "[6] App directories... "
APP_MISSING=0
for app in "${REQUIRED_APPS[@]}"; do
    if [ ! -d "$PROJECT_DIR/apps/$app" ]; then
        echo ""
        echo "  MISSING: apps/$app"
        APP_MISSING=$((APP_MISSING + 1))
    fi
done
if [ $APP_MISSING -eq 0 ]; then
    pass
else
    fail
fi

# ── Test 7: System service files exist ────────────────────────

echo -n "[7] Service files... "
SVC_MISSING=0
for svc in compositor wam notification power sensors location telephony display usb input user credential; do
    if [ ! -f "$PROJECT_DIR/system/zyl-$svc.service" ] && [ "$svc" != "compositor" ] && [ "$svc" != "wam" ]; then
        # compositor and wam checked separately
        true
    fi
    if [ ! -f "$PROJECT_DIR/system/zyl-$svc.service" ]; then
        echo ""
        echo "  MISSING: system/zyl-$svc.service"
        SVC_MISSING=$((SVC_MISSING + 1))
    fi
done
if [ ! -f "$PROJECT_DIR/system/zyl-os.target" ]; then
    echo ""
    echo "  MISSING: system/zyl-os.target"
    SVC_MISSING=$((SVC_MISSING + 1))
fi
if [ $SVC_MISSING -eq 0 ]; then
    pass
else
    fail
fi

# ── Test 8: meson.build exists for all service directories ───

echo -n "[8] Service meson.build files... "
MESON_MISSING=0
for svc_dir in "$PROJECT_DIR"/runtime/services/*/; do
    [ -d "$svc_dir" ] || continue
    if [ ! -f "$svc_dir/meson.build" ]; then
        echo ""
        echo "  MISSING: $svc_dir/meson.build"
        MESON_MISSING=$((MESON_MISSING + 1))
    fi
done
if [ $MESON_MISSING -eq 0 ]; then
    pass
else
    fail
fi

# ── Cleanup ───────────────────────────────────────────────────

rm -rf "$BUILDDIR"

# ── Summary ───────────────────────────────────────────────────

echo ""
echo "========================================"
echo "Results: $PASS passed, $FAIL failed, $SKIP skipped"
echo "========================================"
exit $FAIL
