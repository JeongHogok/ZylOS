#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - License Audit
#
# 역할: THIRD_PARTY_LICENSES.md와 실제 의존성 대조
# 수행범위: Cargo.toml, meson.build 의존성 추출 → 문서와 비교
# 의존방향: bash, grep
# SOLID: SRP — 라이선스 감사만 담당
# ──────────────────────────────────────────────────────────

set -euo pipefail
cd "$(dirname "$0")/.."

ERRORS=0
WARNINGS=0
LICENSE_FILE="THIRD_PARTY_LICENSES.md"

echo "═══════════════════════════════════════"
echo "  ZylOS License Audit"
echo "═══════════════════════════════════════"
echo ""

# ─── Check license file exists ───
if [ ! -f "$LICENSE_FILE" ]; then
  echo "✗ THIRD_PARTY_LICENSES.md not found"
  exit 1
fi

# ─── Rust dependencies from Cargo.toml ───
echo "■ Rust Dependencies (emulator-app/Cargo.toml)"
RUST_DEPS=$(grep -E '^[a-z][-a-z_]+ *=' emulator-app/Cargo.toml | grep -v '^\[' | sed 's/ *=.*//' | grep -v -E '^(name|version|edition|description|license|strip|lto|codegen-units)$' | sort -u)

for dep in $RUST_DEPS; do
  if grep -qi "$dep" "$LICENSE_FILE"; then
    echo "  ✓ $dep — documented"
  else
    echo "  ✗ $dep — NOT in $LICENSE_FILE"
    ERRORS=$((ERRORS + 1))
  fi
done

# ─── C dependencies from meson.build ───
echo ""
echo "■ C Dependencies (meson.build + services)"
C_DEPS=$(grep -rh "dependency(" meson.build runtime/*/meson.build runtime/services/*/meson.build 2>/dev/null | \
  sed -n "s/.*dependency('\([^']*\)'.*/\1/p" | sort -u)

for dep in $C_DEPS; do
  # Map pkg-config names to library names
  case "$dep" in
    gio-2.0)        lib="GLib / GIO" ;;
    gtk4)           lib="GTK 4" ;;
    json-glib-1.0)  lib="json-glib" ;;
    libseccomp)     lib="libseccomp" ;;
    openssl)        lib="OpenSSL" ;;
    wlroots*)       lib="wlroots" ;;
    wayland*)       lib="wayland" ;;
    xkbcommon)      lib="xkbcommon" ;;
    pixman-1)       lib="Pixman" ;;
    libinput)       lib="libinput" ;;
    webkitgtk*|webkit2gtk*) lib="WebKitGTK" ;;
    *)              lib="$dep" ;;
  esac

  if grep -qi "$lib" "$LICENSE_FILE"; then
    echo "  ✓ $dep ($lib) — documented"
  else
    echo "  ⚠ $dep ($lib) — NOT in $LICENSE_FILE"
    WARNINGS=$((WARNINGS + 1))
  fi
done

# ─── Check for GPL-incompatible licenses ───
echo ""
echo "■ GPL-3.0 Compatibility Check"
INCOMPATIBLE=""

# Check if any Cargo dependency is SSPL, AGPL (unless compatible), or proprietary
if [ -f "emulator-app/Cargo.lock" ]; then
  # cargo-license would be ideal; manual check for known issues
  echo "  ℹ Cargo.lock exists — run 'cargo license' for full audit"
else
  echo "  ⚠ No Cargo.lock — cannot audit Rust dependency licenses"
  WARNINGS=$((WARNINGS + 1))
fi

# ─── OpenSSL special case ───
if grep -rq "openssl" meson.build runtime/*/meson.build runtime/services/*/meson.build 2>/dev/null; then
  if grep -qi "OpenSSL" "$LICENSE_FILE"; then
    echo "  ✓ OpenSSL — documented"
  else
    echo "  ✗ OpenSSL used but NOT in $LICENSE_FILE"
    ERRORS=$((ERRORS + 1))
  fi
  # OpenSSL 3.x is Apache-2.0 (GPL-3.0 compatible)
  echo "  ℹ OpenSSL 3.x uses Apache-2.0 (GPL-3.0 compatible)"
fi

# ─── libzip check ───
if grep -rq "libzip" runtime/services/appstore/ 2>/dev/null; then
  if grep -qi "libzip" "$LICENSE_FILE"; then
    echo "  ✓ libzip — documented"
  else
    echo "  ⚠ libzip used in appstore but NOT in $LICENSE_FILE"
    WARNINGS=$((WARNINGS + 1))
  fi
fi

# ─── Summary ───
echo ""
echo "═══════════════════════════════════════"
echo "  Errors: $ERRORS  Warnings: $WARNINGS"
echo "═══════════════════════════════════════"

if [ $ERRORS -gt 0 ]; then
  exit 1
fi
echo "License audit passed."
