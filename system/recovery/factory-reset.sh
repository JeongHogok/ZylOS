#!/bin/bash
# ----------------------------------------------------------
# [Clean Architecture] Infrastructure Layer - Factory Reset
#
# 역할: 팩토리 리셋 수행 — 사용자 데이터 및 앱 데이터 삭제
# 수행범위: /data/users, /data/apps, /var/lib/zyl-os, /data/telemetry (UUID),
#           /data/crash 삭제 후 sync → 재부팅
# 의존방향: 없음
# SOLID: SRP — 팩토리 리셋 실행만 담당
# ----------------------------------------------------------

set -euo pipefail

echo "==============================="
echo " Zyl OS Factory Reset"
echo "==============================="
echo ""
echo "WARNING: This will erase all user data!"
echo "The following directories will be deleted:"
echo "  - /data/users/*"
echo "  - /data/apps/*"
echo "  - /var/lib/zyl-os/*"
echo "  - /data/telemetry/*   (device UUID and telemetry queue)"
echo "  - /data/crash/*       (crash reports)"
echo ""

read -r -p "Type YES to confirm: " confirm

if [ "$confirm" = "YES" ]; then
    echo "Performing factory reset..."

    if [ -d /data/users ]; then
        rm -rf /data/users/*
        echo "  [OK] /data/users cleared"
    fi

    if [ -d /data/apps ]; then
        rm -rf /data/apps/*
        echo "  [OK] /data/apps cleared"
    fi

    if [ -d /var/lib/zyl-os ]; then
        rm -rf /var/lib/zyl-os/*
        echo "  [OK] /var/lib/zyl-os cleared"
    fi

    # Remove telemetry data including device UUID to prevent cross-reset
    # device fingerprinting (privacy requirement).
    if [ -d /data/telemetry ]; then
        rm -rf /data/telemetry/*
        echo "  [OK] /data/telemetry cleared (device UUID reset)"
    fi

    # Remove crash reports — these may contain sensitive stack traces and
    # personal data from app processes.
    if [ -d /data/crash ]; then
        rm -rf /data/crash/*
        echo "  [OK] /data/crash cleared"
    fi

    # Also clear WAM/app caches
    if [ -d /var/cache/zyl-os ]; then
        rm -rf /var/cache/zyl-os/*
        echo "  [OK] /var/cache/zyl-os cleared"
    fi

    echo ""
    echo "Syncing filesystems..."
    # Flush all pending writes before reboot to prevent data corruption.
    sync
    echo "  [OK] sync complete"

    echo ""
    echo "Factory reset complete. Rebooting in 3 seconds..."
    sleep 3
    reboot
else
    echo "Factory reset cancelled."
    exit 0
fi
