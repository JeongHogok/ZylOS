#!/bin/bash
# ──────────────────────────────────────────────────────────
# Zyl OS Recovery Mode
#
# 진입 방법:
#   1. 부트로더: U-Boot 프롬프트에서 'run recovery' 또는
#      볼륨 다운 + 전원 버튼 3초 동안 누르기
#   2. systemd: systemctl isolate zyl-recovery.target
#   3. 설정 앱: 설정 → 시스템 → 복구 모드
# ──────────────────────────────────────────────────────────
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

# Non-interactive / serial-boot safety: if stdin is not a tty, default to
# "Reboot" to avoid an infinite select loop hanging the device.
if [ ! -t 0 ]; then
    echo "Non-interactive environment detected — rebooting to system."
    sync
    reboot
    exit 0
fi

echo "1) Factory Reset (wipe /data)"
echo "2) Wipe Cache"
echo "3) Reboot to System"
echo "4) Reboot to Bootloader"
echo ""

# Use read+case instead of select to have full control over the loop and
# to avoid bash select's infinite retry on empty input.
perform_action() {
    local choice="$1"
    case "$choice" in
        1)
            if [ -x "${RECOVERY_DIR}/factory-reset.sh" ]; then
                "${RECOVERY_DIR}/factory-reset.sh"
            else
                echo "ERROR: factory-reset.sh not found at ${RECOVERY_DIR}"
                exit 1
            fi
            ;;
        2)
            echo "Wiping cache..."
            rm -rf /var/cache/zyl-os/*
            sync
            echo "Cache wiped successfully."
            ;;
        3)
            echo "Rebooting to system..."
            sync
            reboot
            ;;
        4)
            echo "Rebooting to bootloader..."
            sync
            reboot bootloader
            ;;
        *)
            echo "Invalid option. Please select 1-4."
            return 1
            ;;
    esac
    return 0
}

while true; do
    read -r -p "Select option [1-4]: " user_choice || {
        # EOF on stdin — safe default: reboot
        echo ""
        echo "EOF on stdin — rebooting to system."
        sync
        reboot
        exit 0
    }

    if perform_action "$user_choice"; then
        break
    fi
done
