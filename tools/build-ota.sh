#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - OTA Packaging
#
# 역할: OTA 업데이트 패키지 + 서버 응답(매니페스트) 생성
# 수행범위: 두 슬롯용 서명 이미지(tools/sign-image.sh --slot a|b) → rootfs 와
#           슬롯별 verity 트리·FIT 를 tar.gz 로 묶음 → 패키지 SHA-256 에 대한
#           RSA-SHA256 서명 → 업데이터가 /check 에서 받는 JSON
# 의존방향: tools/sign-image.sh (veritysetup, mkimage …), tar, openssl.
#           형식의 다른 쪽 끝: runtime/services/updater/updater.c
#           (zyl_updater_apply, verify_package)
# SOLID: SRP — 패키지 생성만 담당
#
# 사용법: tools/build-ota.sh [--key-dir DIR] [--url URL] <version> <rootfs-image> [output-dir]
#   커널·DTB·램디스크는 sign-image.sh 와 같은 환경변수(ZYL_KERNEL …)로 준다.
#   패키지는 부트 서명 키(<key-dir>/zylos-boot-key.key)로 서명한다 — 기기는
#   그 공개키를 /etc/zyl-os/ota-signing-key.pem 으로 가진다. 부팅 이미지를
#   서명할 수 있는 키가 곧 업데이트를 낼 수 있는 키라 따로 둘 이유가 없다.
#   출력: zylos-<version>.pkg, zylos-<version>.json
#   --url 은 패키지를 내려받을 https 주소. 비우면 서버가 채워야 한다.
# ──────────────────────────────────────────────────────────
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
KEY_DIR="$ROOT/board/bpi-f3/keys"
URL=""
while [ $# -gt 0 ]; do
    case "$1" in
        --key-dir) KEY_DIR="$2"; shift 2 ;;
        --url)     URL="$2"; shift 2 ;;
        -*) echo "✗ unknown option $1" >&2; exit 2 ;;
        *) break ;;
    esac
done
VERSION="${1:-}"
ROOTFS="${2:-}"
OUTPUT_DIR="${3:-build/ota}"
if [ -z "$VERSION" ] || [ -z "$ROOTFS" ]; then
    echo "Usage: $0 [--key-dir DIR] [--url URL] <version> <rootfs-image> [output-dir]" >&2
    exit 2
fi
# The updater accepts digits and dots only
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+)*$ ]] || { echo "✗ version must be digits and dots: $VERSION" >&2; exit 2; }
case "$URL" in ""|https://*) ;; *) echo "✗ --url must be https://" >&2; exit 2 ;; esac
KEY="$KEY_DIR/zylos-boot-key.key"
[ -f "$KEY" ] || { echo "✗ Not found: $KEY" >&2; exit 1; }
[ -f "$ROOTFS" ] || { echo "✗ Not found: $ROOTFS" >&2; exit 1; }

hash_file() {
    if command -v sha256sum >/dev/null 2>&1; then sha256sum "$1"; else shasum -a 256 "$1"; fi
}
abs() { (cd "$(dirname "$1")" && printf '%s/%s\n' "$(pwd)" "$(basename "$1")"); }

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
mkdir -p "$OUTPUT_DIR" "$W/pkg"
OUT="$(cd "$OUTPUT_DIR" && pwd)"
echo "═══ Zyl OS OTA package $VERSION ═══"

for slot in a b; do
    echo "▸ Slot $slot: verity tree + signed FIT"
    if ! bash "$ROOT/tools/sign-image.sh" --slot "$slot" --key-dir "$KEY_DIR" "$ROOTFS" "$W/$slot" >"$W/log-$slot" 2>&1; then
        cat "$W/log-$slot" >&2
        exit 1
    fi
    ln -s "$W/$slot/verity-hashtree-$slot.img" "$W/pkg/"
    ln -s "$W/$slot/fit-image-$slot.itb" "$W/pkg/"
done
ln -s "$(abs "$ROOTFS")" "$W/pkg/rootfs.img"

echo "▸ Packing and signing"
PKG="$OUT/zylos-$VERSION.pkg"
# -h: the links above, not copies of a multi-GiB image
tar -czhf "$PKG" -C "$W/pkg" rootfs.img verity-hashtree-a.img verity-hashtree-b.img fit-image-a.itb fit-image-b.itb
# What the updater checks: RSA-SHA256 over the package's SHA-256
openssl dgst -sha256 -binary "$PKG" > "$W/digest"
openssl dgst -sha256 -sign "$KEY" -out "$W/sig" "$W/digest"
SIG=$(openssl base64 -A -in "$W/sig")
SHA=$(hash_file "$PKG" | awk '{print $1}')
SIZE=$(wc -c < "$PKG" | tr -d ' ')
cat > "$OUT/zylos-$VERSION.json" <<EOF
{"available": true, "version": "$VERSION", "type": "full", "url": "$URL", "size": $SIZE, "sha256": "$SHA", "signature": "$SIG"}
EOF

echo "  ✓ $PKG ($SIZE bytes)"
echo "  ✓ $OUT/zylos-$VERSION.json — the update server's answer to /check"
