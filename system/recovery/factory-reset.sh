#!/bin/bash
# ----------------------------------------------------------
# [Clean Architecture] Infrastructure Layer - Factory Reset
#
# 역할: 팩토리 리셋 수행 — 사용자 데이터 및 앱 데이터 삭제
# 수행범위: /data/users, /data/apps, /var/lib/zyl-os, /data/telemetry (UUID),
#           /data/netstats, /data/alerts, /data/lock, /data/settings,
#           /data/telephony (call log), /data/contacts, /data/messages,
#           /data/appdata (each app's private data), /data/alarms, /data/push,
#           /data 삭제 후 sync → 재부팅
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
echo "  - /data/netstats/*    (data usage)"
echo "  - /data/alerts/*      (emergency alert history)"
echo "  - /data/telephony/*   (call log)"
echo "  - /data/contacts/*    (contacts)"
echo "  - /data/messages/*    (text messages)"
echo "  - /data/appdata/*     (apps' private data)"
echo "  - /data/lock/*        (screen lock PIN)"
echo "  - /data/settings/*    (settings)"
echo "  - /data/alarms/*      (alarms)"
echo "  - /data/push/*        (push registrations)"
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

    # Data usage counts and alert history describe where the previous owner
    # was; settings, the screen lock PIN and who they called are theirs too
    # Push topics would keep delivering the previous owner's apps' messages here
    for d in /data/netstats /data/alerts /data/lock /data/settings /data/telephony /data/contacts /data/messages /data/appdata /data/alarms /data/push; do
        if [ -d "$d" ]; then
            rm -rf "$d"/*
            echo "  [OK] $d cleared"
        fi
    done

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
