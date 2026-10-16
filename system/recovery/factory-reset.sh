#!/bin/bash
# ----------------------------------------------------------
# [Clean Architecture] Infrastructure Layer - Factory Reset
#
# 역할: 팩토리 리셋 수행 — 기기에 쌓인 것을 전부 지운다
# 수행범위: /data 의 모든 것(사용자·앱·키체인·설정·통화 기록·기기 UUID…) → sync →
#           재부팅. 루트는 읽기 전용이고 /var 는 휘발이라 기기에 남는 것은 /data
#           뿐이다 — 디렉터리를 하나씩 나열하면 새 서비스의 상태를 빠뜨린다.
#           빈 /data 는 다음 부팅에 tmpfiles(zyl-data.conf)가 다시 채운다
# 의존방향: 없음
# SOLID: SRP — 팩토리 리셋 실행만 담당
# ----------------------------------------------------------

set -euo pipefail

echo "==============================="
echo " Zyl OS Factory Reset"
echo "==============================="
echo ""
echo "WARNING: This will erase everything on this device:"
echo "  users, apps and their data, saved networks and pairings, the keychain,"
echo "  settings, call log, messages, contacts and the device's telemetry ID."
echo ""

read -r -p "Type YES to confirm: " confirm

if [ "$confirm" = "YES" ]; then
    echo "Performing factory reset..."
    if mountpoint -q /data; then
        find /data -mindepth 1 -maxdepth 1 -exec rm -rf {} +
        echo "  [OK] /data cleared"
    else
        echo "ERROR: /data is not mounted — nothing erased" >&2
        exit 1
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
