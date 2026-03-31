#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - App Signing Tool
#
# 역할: .ospkg 앱 패키지 서명 — 개발자가 앱 배포 전 실행
# 수행범위: app.json SHA-256 해시 → RSA-2048 서명 → SIGNATURE/CERT 파일 생성
# 의존방향: openssl, zip
# SOLID: SRP — 앱 패키지 서명만 담당
#
# 사용법:
#   ./tools/sign-ospkg.sh <app-dir> <developer-key.pem> <developer-cert.pem> [output.ospkg]
#
# 입력:
#   app-dir/           — 앱 소스 (app.json + index.html + ...)
#   developer-key.pem  — RSA-2048 개발자 비밀키
#   developer-cert.pem — 개발자 인증서 (공개키)
#
# 출력:
#   output.ospkg       — 서명된 앱 패키지 (ZIP)
# ──────────────────────────────────────────────────────────
set -euo pipefail

APP_DIR="${1:-}"
KEY_FILE="${2:-}"
CERT_FILE="${3:-}"
OUTPUT="${4:-}"

if [ -z "$APP_DIR" ] || [ -z "$KEY_FILE" ] || [ -z "$CERT_FILE" ]; then
    echo "Usage: $0 <app-dir> <developer-key.pem> <developer-cert.pem> [output.ospkg]"
    echo ""
    echo "Example:"
    echo "  $0 apps/weather keys/dev.key keys/dev.crt weather.ospkg"
    exit 1
fi

# Validate inputs
if [ ! -d "$APP_DIR" ]; then echo "✗ App directory not found: $APP_DIR"; exit 1; fi
if [ ! -f "$APP_DIR/app.json" ]; then echo "✗ app.json not found in $APP_DIR"; exit 1; fi
if [ ! -f "$KEY_FILE" ]; then echo "✗ Private key not found: $KEY_FILE"; exit 1; fi
if [ ! -f "$CERT_FILE" ]; then echo "✗ Certificate not found: $CERT_FILE"; exit 1; fi

# Derive output name from app.json id
APP_ID=$(python3 -c "import json; print(json.load(open('$APP_DIR/app.json'))['id'])" 2>/dev/null || echo "unknown")
APP_VERSION=$(python3 -c "import json; print(json.load(open('$APP_DIR/app.json'))['version'])" 2>/dev/null || echo "0.0.0")

if [ -z "$OUTPUT" ]; then
    OUTPUT="${APP_ID}_${APP_VERSION}.ospkg"
fi

echo "═══ Zyl OS App Package Signing ═══"
echo "  App:     $APP_ID v$APP_VERSION"
echo "  Source:  $APP_DIR"
echo "  Key:     $KEY_FILE"
echo "  Output:  $OUTPUT"
echo ""

# 1. Validate app.json required fields
echo "▸ [1/5] Validating app.json..."
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

# 2. Compute SHA-256 hash of app.json
echo "▸ [2/5] Computing manifest hash..."
MANIFEST_HASH=$(sha256sum "$APP_DIR/app.json" | cut -d' ' -f1)
echo "  Hash: $MANIFEST_HASH"

# 3. Sign the hash with RSA-2048
echo "▸ [3/5] Signing with RSA-2048..."
echo -n "$MANIFEST_HASH" | openssl dgst -sha256 -sign "$KEY_FILE" | openssl base64 -A > /tmp/zyl-sig-$$.b64
SIGNATURE=$(cat /tmp/zyl-sig-$$.b64)
rm -f /tmp/zyl-sig-$$.b64

if [ -z "$SIGNATURE" ]; then
    echo "✗ Signing failed"
    exit 1
fi
echo "  ✓ Signature generated ($(echo -n "$SIGNATURE" | wc -c | tr -d ' ') bytes base64)"

# 4. Extract certificate fingerprint
echo "▸ [4/5] Extracting certificate fingerprint..."
CERT_FP=$(openssl x509 -in "$CERT_FILE" -fingerprint -sha256 -noout | sed 's/.*=//;s/://g')
echo "  Fingerprint: $CERT_FP"

# 5. Package as .ospkg (ZIP)
echo "▸ [5/5] Creating .ospkg package..."
TMPDIR=$(mktemp -d)
cp -r "$APP_DIR"/* "$TMPDIR/"

# Write SIGNATURE and CERT files
echo -n "$SIGNATURE" > "$TMPDIR/SIGNATURE"
echo -n "$CERT_FP" > "$TMPDIR/CERT"

# Create ZIP
(cd "$TMPDIR" && zip -r -q "$OLDPWD/$OUTPUT" .)
rm -rf "$TMPDIR"

# Verify the package
echo ""
echo "═══ Package Created ═══"
echo "  File: $OUTPUT"
echo "  Size: $(du -h "$OUTPUT" | cut -f1)"
echo ""

# Self-verify
echo "▸ Self-verification..."
EXTRACTED=$(mktemp -d)
unzip -q "$OUTPUT" -d "$EXTRACTED"
VERIFY_HASH=$(sha256sum "$EXTRACTED/app.json" | cut -d' ' -f1)
rm -rf "$EXTRACTED"

if [ "$VERIFY_HASH" = "$MANIFEST_HASH" ]; then
    echo "  ✓ Hash matches after packaging"
else
    echo "  ✗ Hash mismatch! Package may be corrupt"
    exit 1
fi

echo "  ✓ Package ready for distribution: $OUTPUT"
