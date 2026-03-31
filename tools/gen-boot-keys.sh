#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Key Generation
#
# 역할: Verified Boot RSA-2048 키 쌍 생성
# 수행범위: 부트 서명용 비밀키 + 공개키 생성, U-Boot DTB에 공개키 임베드
# 의존방향: openssl, mkimage
# SOLID: SRP — 키 생성만 담당
#
# 사용법: ./tools/gen-boot-keys.sh [output-dir]
# 출력:
#   {output-dir}/zylos-boot-key.key     — RSA-2048 비밀키 (절대 배포 금지!)
#   {output-dir}/zylos-boot-key.crt     — 자체 서명 인증서 (공개키 포함)
#   {output-dir}/zylos-boot-key.pub     — PEM 공개키
# ──────────────────────────────────────────────────────────
set -euo pipefail

KEY_DIR="${1:-board/bpi-f3/keys}"
KEY_NAME="zylos-boot-key"
KEY_BITS=2048
VALID_DAYS=3650  # 10년

mkdir -p "$KEY_DIR"

# 이미 존재하면 덮어쓰기 방지
if [ -f "$KEY_DIR/$KEY_NAME.key" ]; then
    echo "⚠ Key already exists: $KEY_DIR/$KEY_NAME.key"
    echo "  Delete it first if you want to regenerate."
    exit 1
fi

echo "═══ Zyl OS Boot Key Generation ═══"
echo ""

# 1. RSA-2048 비밀키 생성
echo "▸ Generating RSA-$KEY_BITS private key..."
openssl genpkey -algorithm RSA \
    -pkeyopt "rsa_keygen_bits:$KEY_BITS" \
    -out "$KEY_DIR/$KEY_NAME.key"
chmod 600 "$KEY_DIR/$KEY_NAME.key"

# 2. 자체 서명 인증서 생성 (U-Boot mkimage가 .crt 필요)
echo "▸ Generating self-signed certificate..."
openssl req -new -x509 \
    -key "$KEY_DIR/$KEY_NAME.key" \
    -out "$KEY_DIR/$KEY_NAME.crt" \
    -days $VALID_DAYS \
    -subj "/CN=ZylOS Boot Signing Key/O=Zyl OS Project/OU=Firmware"

# 3. 공개키 PEM 추출 (참조용)
echo "▸ Extracting public key..."
openssl x509 -in "$KEY_DIR/$KEY_NAME.crt" \
    -pubkey -noout > "$KEY_DIR/$KEY_NAME.pub"

# 4. .gitignore에 비밀키 추가 (보안)
GITIGNORE="$KEY_DIR/.gitignore"
if [ ! -f "$GITIGNORE" ] || ! grep -q "*.key" "$GITIGNORE"; then
    echo "*.key" >> "$GITIGNORE"
    echo "▸ Added *.key to $GITIGNORE"
fi

echo ""
echo "═══ Keys Generated ═══"
echo "  Private key:   $KEY_DIR/$KEY_NAME.key (KEEP SECRET)"
echo "  Certificate:   $KEY_DIR/$KEY_NAME.crt"
echo "  Public key:    $KEY_DIR/$KEY_NAME.pub"
echo ""
echo "Next step: Sign FIT image with:"
echo "  mkimage -f board/bpi-f3/fit-image.its \\"
echo "    -k $KEY_DIR -K u-boot.dtb -r fit-image.itb"
