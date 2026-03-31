#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - App Signing Tool
#
# 역할: .ospkg 앱 패키지 서명 — 개발자가 앱 배포 전 실행
# 수행범위: 전체 패키지 파일 manifest SHA-256 생성 → RSA-2048 서명 → SIGNATURE/CERT/MANIFEST.sha256 포함
# 의존방향: openssl, zip, unzip, python3
# SOLID: SRP — 앱 패키지 서명만 담당
#
# 사용법:
#   ./tools/sign-ospkg.sh <app-dir> <developer-key.pem> <developer-cert.pem> [output.ospkg]
# ──────────────────────────────────────────────────────────
set -euo pipefail

APP_DIR="${1:-}"
KEY_FILE="${2:-}"
CERT_FILE="${3:-}"
OUTPUT="${4:-}"
CALLER_DIR="$(pwd)"

need_cmd() {
    command -v "$1" >/dev/null 2>&1 || {
        echo "✗ Required command not found: $1" >&2
        exit 1
    }
}

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$@"
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$@"
    else
        echo "✗ Required command not found: sha256sum or shasum" >&2
        exit 1
    fi
}

for cmd in python3 openssl zip unzip mktemp sort find awk; do
    need_cmd "$cmd"
done

if [ -z "$APP_DIR" ] || [ -z "$KEY_FILE" ] || [ -z "$CERT_FILE" ]; then
    echo "Usage: $0 <app-dir> <developer-key.pem> <developer-cert.pem> [output.ospkg]"
    echo ""
    echo "Example:"
    echo "  $0 apps/weather keys/dev.key keys/dev.crt weather.ospkg"
    exit 1
fi

if [ ! -d "$APP_DIR" ]; then echo "✗ App directory not found: $APP_DIR"; exit 1; fi
if [ ! -f "$APP_DIR/app.json" ]; then echo "✗ app.json not found in $APP_DIR"; exit 1; fi
if [ ! -f "$KEY_FILE" ]; then echo "✗ Private key not found: $KEY_FILE"; exit 1; fi
if [ ! -f "$CERT_FILE" ]; then echo "✗ Certificate not found: $CERT_FILE"; exit 1; fi

APP_ID=$(python3 -c "import json; print(json.load(open('$APP_DIR/app.json'))['id'])" 2>/dev/null || echo "unknown")
APP_VERSION=$(python3 -c "import json; print(json.load(open('$APP_DIR/app.json'))['version'])" 2>/dev/null || echo "0.0.0")

if [ -z "$OUTPUT" ]; then
    OUTPUT="${APP_ID}_${APP_VERSION}.ospkg"
fi

case "$OUTPUT" in
    /*) OUTPUT_PATH="$OUTPUT" ;;
    *) OUTPUT_PATH="$CALLER_DIR/$OUTPUT" ;;
esac

echo "═══ Zyl OS App Package Signing ═══"
echo "  App:     $APP_ID v$APP_VERSION"
echo "  Source:  $APP_DIR"
echo "  Key:     $KEY_FILE"
echo "  Output:  $OUTPUT_PATH"
echo ""

echo "▸ [1/6] Validating app.json..."
python3 -c "
import json, sys
m = json.load(open('$APP_DIR/app.json'))
required = ['id', 'name', 'version', 'entry']
missing = [f for f in required if f not in m or not m[f]]
if missing:
    print(f'✗ Missing required fields: {missing}')
    sys.exit(1)
print(f'  ✓ {m[\"id\"]} v{m[\"version\"]} — {m[\"name\"]}')
" || exit 1

echo "▸ [2/6] Staging package payload..."
TMPDIR=$(mktemp -d)
EXTRACTED=$(mktemp -d)
trap 'rm -rf "$TMPDIR" "$EXTRACTED"' EXIT

cp -R "$APP_DIR"/. "$TMPDIR/"
find "$TMPDIR" -name '.DS_Store' -delete
find "$TMPDIR" -type f | LC_ALL=C sort > /dev/null

echo "▸ [3/6] Generating whole-package manifest..."
(
  cd "$TMPDIR"
  find . -type f ! -name 'MANIFEST.sha256' ! -name 'SIGNATURE' ! -name 'CERT' -print \
    | LC_ALL=C sort \
    | while IFS= read -r rel; do
        hash=$(hash_file "$rel" | awk '{print $1}')
        printf '%s\t%s\n' "$hash" "$rel"
      done > MANIFEST.sha256
)
MANIFEST_HASH=$(hash_file "$TMPDIR/MANIFEST.sha256" | awk '{print $1}')
echo "  Manifest hash: $MANIFEST_HASH"

echo "▸ [4/6] Signing manifest with RSA-2048..."
SIGNATURE=$(openssl dgst -sha256 -sign "$KEY_FILE" "$TMPDIR/MANIFEST.sha256" | openssl base64 -A)
if [ -z "$SIGNATURE" ]; then
    echo "✗ Signing failed"
    exit 1
fi
echo "  ✓ Signature generated ($(printf '%s' "$SIGNATURE" | wc -c | tr -d ' ') bytes base64)"

echo "▸ [5/6] Extracting certificate fingerprint..."
CERT_FP=$(openssl x509 -in "$CERT_FILE" -fingerprint -sha256 -noout | sed 's/.*=//;s/://g')
printf '%s' "$SIGNATURE" > "$TMPDIR/SIGNATURE"
printf '%s' "$CERT_FP" > "$TMPDIR/CERT"
echo "  Fingerprint: $CERT_FP"

echo "▸ [6/6] Creating .ospkg package..."
(
  cd "$TMPDIR"
  zip -r -q "$OUTPUT_PATH" .
)

echo ""
echo "═══ Package Created ═══"
echo "  File: $OUTPUT_PATH"
echo "  Size: $(du -h "$OUTPUT_PATH" | cut -f1)"
echo ""

echo "▸ Self-verification..."
unzip -q "$OUTPUT_PATH" -d "$EXTRACTED"
if [ ! -f "$EXTRACTED/MANIFEST.sha256" ] || [ ! -f "$EXTRACTED/SIGNATURE" ] || [ ! -f "$EXTRACTED/CERT" ]; then
    echo "  ✗ Package missing signature metadata"
    exit 1
fi

(
  cd "$EXTRACTED"
  while IFS=$'\t' read -r hash rel; do
    actual=$(hash_file "$rel" | awk '{print $1}')
    if [ "$hash" != "$actual" ]; then
      echo "  ✗ File hash mismatch: $rel" >&2
      exit 1
    fi
  done < MANIFEST.sha256
)

echo "  ✓ All packaged files match MANIFEST.sha256"
printf '%s' "$SIGNATURE" | openssl base64 -d -A > "$EXTRACTED/SIGNATURE.bin"
if openssl dgst -sha256 -verify <(openssl x509 -in "$CERT_FILE" -pubkey -noout) \
    -signature "$EXTRACTED/SIGNATURE.bin" "$EXTRACTED/MANIFEST.sha256" >/dev/null 2>&1; then
    echo "  ✓ Signature verifies against provided certificate"
else
    echo "  ✗ Signature verification failed"
    exit 1
fi

echo "  ✓ Package ready for distribution: $OUTPUT_PATH"
