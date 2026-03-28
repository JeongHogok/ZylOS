#!/bin/bash
# ----------------------------------------------------------
# [Clean Architecture] Infrastructure Layer - Recovery Script
#
# 역할: Zyl OS 복구 모드 진입 스크립트
# 수행범위: 팩토리 리셋, 캐시 삭제, 재부팅 메뉴 제공
# 의존방향: factory-reset.sh
# SOLID: SRP — 복구 모드 메뉴 및 실행만 담당
# ----------------------------------------------------------

set -euo pipefail

RECOVERY_DIR="/usr/share/zyl-os/recovery"

echo "=== Zyl OS Recovery Mode ==="
echo ""
echo "Entered via: hold Volume Down during boot, or 3 failed boot attempts"
echo ""
echo "1) Factory Reset (wipe /data)"
echo "2) Wipe Cache"
echo "3) Reboot to System"
echo "4) Reboot to Bootloader"
echo ""

select opt in "Factory Reset" "Wipe Cache" "Reboot" "Bootloader"; do
    case "$opt" in
        "Factory Reset")
            if [ -x "${RECOVERY_DIR}/factory-reset.sh" ]; then
                "${RECOVERY_DIR}/factory-reset.sh"
            else
                echo "ERROR: factory-reset.sh not found at ${RECOVERY_DIR}"
                exit 1
            fi
            ;;
        "Wipe Cache")
            echo "Wiping cache..."
            rm -rf /var/cache/zyl-os/*
            echo "Cache wiped successfully."
            ;;
        "Reboot")
            echo "Rebooting to system..."
            reboot
            ;;
        "Bootloader")
            echo "Rebooting to bootloader..."
            reboot bootloader
            ;;
        *)
            echo "Invalid option. Please select 1-4."
            ;;
    esac
    break
done
