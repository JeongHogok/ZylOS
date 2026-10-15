#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Key Generation
#
# 역할: 부팅 체인 RSA-2048 키 생성
# 수행범위: 없는 키만 만들고 있는 키는 그대로 둔다 (키는 기기와 평생 묶임)
#   zylos-boot-key  OS FIT(커널·DTB·램디스크) — U-Boot 가 검증 (.key .crt .pub)
#   zylos-fw-key    OpenSBI·U-Boot FIT — SPL 이 검증 (.key .crt .pub)
#   zylos-spl-key   FSBL 안의 SPL — Boot ROM 이 검증 (.key .pub)
#   zylos-rotpk     루트 키: FSBL 헤더 서명, 그 해시가 eFuse 에 (.key .pub)
# 의존방향: openssl
# SOLID: SRP — 키 생성만 담당
#
# 사용법: ./tools/gen-boot-keys.sh [output-dir]
# 쓰는 곳: tools/sign-image.sh(boot), tools/build-bootloader.sh(전부)
# ──────────────────────────────────────────────────────────
set -euo pipefail

KEY_DIR="${1:-board/bpi-f3/keys}"
VALID_DAYS=3650  # 10년 (mkimage 가 인증서 형태를 요구할 뿐, 기한은 검증되지 않는다)

mkdir -p "$KEY_DIR"
echo "═══ Zyl OS Boot Key Generation ═══"

# name, needs a certificate (mkimage signs FITs with key + .crt)
make_key() {
    local name="$1" crt="$2"
    if [ -f "$KEY_DIR/$name.key" ]; then
        echo "  · $name — kept"
        return
    fi
    openssl genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$KEY_DIR/$name.key" 2>/dev/null
    chmod 600 "$KEY_DIR/$name.key"
    openssl pkey -in "$KEY_DIR/$name.key" -pubout -out "$KEY_DIR/$name.pub"
    if [ "$crt" = 1 ]; then
        openssl req -new -x509 -key "$KEY_DIR/$name.key" -out "$KEY_DIR/$name.crt" -days $VALID_DAYS \
            -subj "/CN=$name/O=Zyl OS Project/OU=Firmware"
    fi
    echo "  ✓ $name"
}

make_key zylos-boot-key 1
make_key zylos-fw-key 1
make_key zylos-spl-key 0
make_key zylos-rotpk 0

# Private keys never go into git
GITIGNORE="$KEY_DIR/.gitignore"
if [ ! -f "$GITIGNORE" ] || ! grep -q '^\*\.key$' "$GITIGNORE"; then
    echo "*.key" >> "$GITIGNORE"
fi

echo ""
echo "  Keep $KEY_DIR/*.key offline (HSM or encrypted storage)."
echo "  zylos-rotpk is the device's root of trust: once its hash is in eFuse"
echo "  (tools/build-bootloader.sh --burn-rotpk) it can never change."
