#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Script
#
# Role: Deploy OS image apps to ui/apps/ for dev mode
# Scope: Mount OS image or fallback to project apps/, sync to ui/apps/
# Dependency: hdiutil (macOS) or udisksctl (Linux)
# SOLID: SRP — app deployment only
#
# Clean Architecture, SOLID principles, i18n rules strictly followed
# The emulator provides a real device runtime environment and must not contain OS image content
# ──────────────────────────────────────────────────────────

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
DST="$PROJECT_DIR/ui/apps"
SRC_APPS="$PROJECT_DIR/../apps"

# Platform-specific data directory
case "$(uname)" in
  Darwin) IMG_DIR="$HOME/Library/Application Support/zyl-emulator/os-images" ;;
  *)      IMG_DIR="${XDG_DATA_HOME:-$HOME/.local/share}/zyl-emulator/os-images" ;;
esac

# Always sync from source to ensure latest files are deployed.
# In dev mode, project apps/ is the authoritative source.
# In production, the OS image is mounted at boot by the Rust backend.

sync_from_source() {
    local src="$1"
    if [ ! -d "$src" ]; then
        echo "[deploy-apps] Source not found: $src"
        return 1
    fi
    mkdir -p "$DST"
    # Use rsync if available (preserves timestamps, handles deletes)
    if command -v rsync &>/dev/null; then
        rsync -a --delete "$src/" "$DST/"
        echo "[deploy-apps] Synced via rsync from $src"
    else
        # Fallback: remove stale, copy fresh
        rm -rf "$DST"
        cp -r "$src" "$DST"
        echo "[deploy-apps] Copied from $src"
    fi
    return 0
}

# Strategy 1: Use project source apps/ directly (fastest for dev)
if [ -d "$SRC_APPS" ]; then
    sync_from_source "$SRC_APPS"
    echo "[deploy-apps] Done (from project source)"
    exit 0
fi

# Strategy 2: Mount OS image and extract apps/
IMG=""
for f in "$IMG_DIR"/*.img; do
    if [ -f "$f" ]; then
        IMG="$f"
        break
    fi
done

if [ -z "$IMG" ]; then
    echo "[deploy-apps] No OS image found in $IMG_DIR and no project apps/"
    exit 0
fi

echo "[deploy-apps] Mounting $IMG..."

# macOS
if command -v hdiutil &>/dev/null; then
    MOUNT_OUTPUT=$(hdiutil attach -nobrowse "$IMG" 2>&1)
    MOUNT_POINT=$(echo "$MOUNT_OUTPUT" | grep "/Volumes/" | awk -F'\t' '{print $NF}' | head -1 | xargs)

    if [ -d "$MOUNT_POINT/apps" ]; then
        sync_from_source "$MOUNT_POINT/apps"
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
            sync_from_source "$MOUNT_POINT/apps"
        fi

        udisksctl unmount -b "$LOOP_DEV" --no-user-interaction 2>/dev/null
        udisksctl loop-delete -b "$LOOP_DEV" 2>/dev/null
    fi
fi

echo "[deploy-apps] Done"
