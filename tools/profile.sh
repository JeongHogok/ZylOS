#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Tool
#
# 역할: Zyl OS 성능 프로파일링 — 빌드 시간, 소스 통계, 바이너리/앱 크기 측정
# 수행범위: 빌드 시간 측정, 소스 파일/LOC 카운트, 바이너리 크기, 앱 크기, 메모리 추정
# 의존방향: meson, ninja, find, wc, du
# SOLID: SRP — 성능 프로파일링만 담당
# ──────────────────────────────────────────────────────────

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILDDIR="$PROJECT_DIR/builddir-perf"

echo "=== Zyl OS Performance Profile ==="
echo "Project: $PROJECT_DIR"
echo "Date: $(date '+%Y-%m-%d %H:%M:%S')"
echo ""

# ── Build time ────────────────────────────────────────────────

echo "--- Build Time ---"
rm -rf "$BUILDDIR"
BUILD_START=$(date +%s)
if meson setup "$BUILDDIR" "$PROJECT_DIR" 2>/dev/null && ninja -C "$BUILDDIR" 2>/dev/null; then
    BUILD_END=$(date +%s)
    BUILD_SECS=$((BUILD_END - BUILD_START))
    echo "Build time: ${BUILD_SECS}s"
else
    echo "Build time: N/A (build requires cross-compile deps)"
fi
echo ""

# ── Source file statistics ────────────────────────────────────

echo "--- Source Statistics ---"

C_FILES=$(find "$PROJECT_DIR" -name '*.c' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')
H_FILES=$(find "$PROJECT_DIR" -name '*.h' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')
JS_FILES=$(find "$PROJECT_DIR" -name '*.js' -not -path '*/.git/*' -not -path '*/builddir*' -not -path '*/node_modules/*' | wc -l | tr -d ' ')
HTML_FILES=$(find "$PROJECT_DIR" -name '*.html' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')
CSS_FILES=$(find "$PROJECT_DIR" -name '*.css' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')
MESON_FILES=$(find "$PROJECT_DIR" -name 'meson.build' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')
SH_FILES=$(find "$PROJECT_DIR" -name '*.sh' -not -path '*/.git/*' -not -path '*/builddir*' | wc -l | tr -d ' ')

TOTAL_FILES=$((C_FILES + H_FILES + JS_FILES + HTML_FILES + CSS_FILES + MESON_FILES + SH_FILES))

echo "  C source:    $C_FILES files"
echo "  C headers:   $H_FILES files"
echo "  JavaScript:  $JS_FILES files"
echo "  HTML:        $HTML_FILES files"
echo "  CSS:         $CSS_FILES files"
echo "  Meson build: $MESON_FILES files"
echo "  Shell:       $SH_FILES files"
echo "  Total:       $TOTAL_FILES files"
echo ""

# ── Lines of code ─────────────────────────────────────────────

echo "--- Lines of Code ---"

C_LOC=$(find "$PROJECT_DIR" -name '*.c' -not -path '*/.git/*' -not -path '*/builddir*' -exec cat {} + 2>/dev/null | wc -l | tr -d ' ')
H_LOC=$(find "$PROJECT_DIR" -name '*.h' -not -path '*/.git/*' -not -path '*/builddir*' -exec cat {} + 2>/dev/null | wc -l | tr -d ' ')
JS_LOC=$(find "$PROJECT_DIR" -name '*.js' -not -path '*/.git/*' -not -path '*/builddir*' -not -path '*/node_modules/*' -exec cat {} + 2>/dev/null | wc -l | tr -d ' ')
HTML_LOC=$(find "$PROJECT_DIR" -name '*.html' -not -path '*/.git/*' -not -path '*/builddir*' -exec cat {} + 2>/dev/null | wc -l | tr -d ' ')
CSS_LOC=$(find "$PROJECT_DIR" -name '*.css' -not -path '*/.git/*' -not -path '*/builddir*' -exec cat {} + 2>/dev/null | wc -l | tr -d ' ')

TOTAL_LOC=$((C_LOC + H_LOC + JS_LOC + HTML_LOC + CSS_LOC))

echo "  C:           $C_LOC lines"
echo "  Headers:     $H_LOC lines"
echo "  JavaScript:  $JS_LOC lines"
echo "  HTML:        $HTML_LOC lines"
echo "  CSS:         $CSS_LOC lines"
echo "  Total:       $TOTAL_LOC lines"
echo ""

# ── Binary sizes (if built) ──────────────────────────────────

echo "--- Binary Sizes ---"
BINS_FOUND=0
find "$BUILDDIR" -name 'zyl-*' -executable -type f 2>/dev/null | sort | while read -r f; do
    BINS_FOUND=1
    SIZE=$(du -h "$f" | cut -f1)
    echo "  $(basename "$f"): $SIZE"
done
if [ $BINS_FOUND -eq 0 ] 2>/dev/null; then
    echo "  (no binaries found — build may have been skipped)"
fi
echo ""

# ── App sizes ─────────────────────────────────────────────────

echo "--- App Sizes ---"
for d in "$PROJECT_DIR"/apps/*/; do
    [ -d "$d" ] || continue
    SIZE=$(du -sh "$d" 2>/dev/null | cut -f1)
    FILES=$(find "$d" -type f | wc -l | tr -d ' ')
    echo "  $(basename "$d"): $SIZE ($FILES files)"
done
echo ""

# ── Shared resources ──────────────────────────────────────────

echo "--- Shared Resources ---"
if [ -d "$PROJECT_DIR/apps/shared" ]; then
    SHARED_SIZE=$(du -sh "$PROJECT_DIR/apps/shared" 2>/dev/null | cut -f1)
    echo "  shared/: $SHARED_SIZE"
fi
echo ""

# ── Memory estimates ──────────────────────────────────────────

echo "--- Memory Estimates (per service, approximate) ---"
echo "  Compositor (wlroots + GPU buffers): ~15MB"
echo "  WAM (per app, WebKit process):      ~50MB"
echo "  System services (each):             ~2-5MB"
echo "  D-Bus daemon:                       ~2MB"
echo "  Total baseline (no apps):           ~100MB"
echo "  With 3 apps running:                ~250MB"
echo ""

# ── Cleanup ───────────────────────────────────────────────────

rm -rf "$BUILDDIR"

echo "=== Profile Complete ==="
