#!/bin/bash
# ──────────────────────────────────────────────────────────
# OS 이미지에서 앱을 ui/apps/로 배포 (dev mode 전용)
# cargo tauri dev의 beforeDevCommand로 실행됨
# ──────────────────────────────────────────────────────────

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DST="$PROJECT_DIR/ui/apps"
IMG_DIR="$HOME/Library/Application Support/zyl-emulator/os-images"

# 이미 배포된 앱이 있으면 스킵
if [ -d "$DST" ] && [ "$(ls -A "$DST" 2>/dev/null)" ]; then
    echo "[deploy-apps] Apps already deployed, skipping"
    exit 0
fi

# 기본 OS 이미지 찾기
IMG=""
for f in "$IMG_DIR"/*.img; do
    if [ -f "$f" ]; then
        IMG="$f"
        break
    fi
done

if [ -z "$IMG" ]; then
    echo "[deploy-apps] No OS image found in $IMG_DIR"
    # 폴백: 프로젝트 루트의 apps/ 에서 복사
    if [ -d "$PROJECT_DIR/../apps" ]; then
        cp -r "$PROJECT_DIR/../apps" "$DST"
        echo "[deploy-apps] Fallback: copied from project apps/"
    fi
    exit 0
fi

echo "[deploy-apps] Mounting $IMG..."

# macOS
if command -v hdiutil &>/dev/null; then
    MOUNT_OUTPUT=$(hdiutil attach -nobrowse "$IMG" 2>&1)
    MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | grep "/Volumes/" | awk -F'\t' '{print $NF}' | head -1 | xargs)

    if [ -d "$MOUNT_POINT/apps" ]; then
        mkdir -p "$DST"
        cp -r "$MOUNT_POINT/apps/"* "$DST/"
        echo "[deploy-apps] Deployed from $MOUNT_POINT/apps/"
    fi

    hdiutil detach "$MOUNT_POINT" 2>/dev/null
# Linux
elif command -v udisksctl &>/dev/null; then
    LOOP_OUTPUT=$(udisksctl loop-setup -f "$IMG" 2>&1)
    LOOP_DEV=$(echo "$LOOP_OUTPUT" | grep -o '/dev/loop[0-9]*')

    if [ -n "$LOOP_DEV" ]; then
        MOUNT_OUTPUT=$(udisksctl mount -b "$LOOP_DEV" --no-user-interaction 2>&1)
        MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | grep -o '/media/[^ ]*')

        if [ -d "$MOUNT_POINT/apps" ]; then
            mkdir -p "$DST"
            cp -r "$MOUNT_POINT/apps/"* "$DST/"
            echo "[deploy-apps] Deployed from $MOUNT_POINT/apps/"
        fi

        udisksctl unmount -b "$LOOP_DEV" --no-user-interaction 2>/dev/null
        udisksctl loop-delete -b "$LOOP_DEV" 2>/dev/null
    fi
fi

echo "[deploy-apps] Done"
