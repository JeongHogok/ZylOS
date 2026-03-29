#!/bin/bash
# ──────────────────────────────────────────────────────────
# OS 이미지 빌드: apps/ → .img 디스크 이미지 생성
# 용도: 배포용 OS 이미지 생성
# ──────────────────────────────────────────────────────────

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
APPS_SRC="$PROJECT_DIR/apps"
VERSION="${1:-0.1.0}"
SIZE_MB="${2:-64}"

# 플랫폼별 데이터 디렉토리
case "$(uname)" in
  Darwin) IMG_DIR="$HOME/Library/Application Support/zyl-emulator/os-images" ;;
  *)      IMG_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/zyl-emulator/os-images" ;;
esac

mkdir -p "$IMG_DIR"
IMG_PATH="$IMG_DIR/$VERSION.img"
META_PATH="$IMG_DIR/$VERSION.json"

if [ ! -d "$APPS_SRC" ]; then
    echo "[build-image] ERROR: apps/ directory not found at $APPS_SRC"
    exit 1
fi

echo "[build-image] Building OS image v$VERSION (${SIZE_MB}MB)..."
echo "[build-image] Source: $APPS_SRC"
echo "[build-image] Target: $IMG_PATH"

# 기존 이미지 삭제
if [ -f "$IMG_PATH" ]; then
    echo "[build-image] Removing existing image..."
    rm -f "$IMG_PATH" "$META_PATH"
fi

# macOS: HFS+ 이미지
if [ "$(uname)" = "Darwin" ]; then
    hdiutil create -size "${SIZE_MB}m" -fs HFS+ -volname "ZylOS-$VERSION" -o "$IMG_PATH"

    # hdiutil은 .dmg 확장자를 붙일 수 있음
    if [ -f "${IMG_PATH}.dmg" ] && [ ! -f "$IMG_PATH" ]; then
        mv "${IMG_PATH}.dmg" "$IMG_PATH"
    fi

    # 마운트
    MOUNT_OUTPUT=$(hdiutil attach -nobrowse "$IMG_PATH" 2>&1)
    MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | grep "/Volumes/" | awk -F'\t' '{print $NF}' | head -1 | xargs)

    if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT" ]; then
        echo "[build-image] ERROR: Failed to mount image"
        exit 1
    fi

    # 앱 복사
    cp -r "$APPS_SRC" "$MOUNT_POINT/apps"
    echo "[build-image] Apps copied: $(ls "$MOUNT_POINT/apps/" | wc -l | tr -d ' ') apps"

    # 언마운트
    hdiutil detach "$MOUNT_POINT"

# Linux: ext4 이미지
else
    SIZE_BYTES=$((SIZE_MB * 1024 * 1024))
    dd if=/dev/zero of="$IMG_PATH" bs=1 count=0 seek="$SIZE_BYTES" 2>/dev/null
    mkfs.ext4 -F -q "$IMG_PATH"

    # 마운트 (udisksctl 사용 — root 불필요)
    LOOP_OUTPUT=$(udisksctl loop-setup -f "$IMG_PATH" 2>&1)
    LOOP_DEV=$(echo "$LOOP_OUTPUT" | grep -o '/dev/loop[0-9]*' | head -1)

    if [ -z "$LOOP_DEV" ]; then
        echo "[build-image] ERROR: Failed to setup loop device"
        exit 1
    fi

    MOUNT_OUTPUT=$(udisksctl mount -b "$LOOP_DEV" --no-user-interaction 2>&1)
    MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | sed 's/.*at //' | tr -d '.')

    if [ -z "$MOUNT_POINT" ] || [ ! -d "$MOUNT_POINT" ]; then
        echo "[build-image] ERROR: Failed to mount image"
        udisksctl loop-delete -b "$LOOP_DEV" 2>/dev/null
        exit 1
    fi

    # 앱 복사
    cp -r "$APPS_SRC" "$MOUNT_POINT/apps"
    echo "[build-image] Apps copied: $(ls "$MOUNT_POINT/apps/" | wc -l | tr -d ' ') apps"

    # 언마운트
    udisksctl unmount -b "$LOOP_DEV" --no-user-interaction 2>/dev/null
    udisksctl loop-delete -b "$LOOP_DEV" 2>/dev/null
fi

# 메타데이터 생성
IMG_SIZE=$(stat -f%z "$IMG_PATH" 2>/dev/null || stat -c%s "$IMG_PATH" 2>/dev/null || echo "0")
cat > "$META_PATH" << EOF
{
  "version": "$VERSION",
  "label": "v$VERSION",
  "description": "Developer Preview",
  "path": "$IMG_PATH",
  "size_bytes": $IMG_SIZE,
  "format": "img"
}
EOF

echo ""
echo "[build-image] Done!"
echo "  Image: $IMG_PATH ($(echo "scale=1; $IMG_SIZE / 1048576" | bc 2>/dev/null || echo "$IMG_SIZE") MB)"
echo "  Meta:  $META_PATH"
