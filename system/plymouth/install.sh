#!/bin/bash
# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Installer
# 역할: Plymouth 부트 스플래시 테마를 시스템에 설치하고 initramfs 재빌드
# 수행범위: 테마 파일 복사, initramfs 갱신. 테마 디자인 로직 미포함
# 의존방향: plymouth, update-initramfs, 호스트 OS
# SOLID: SRP — Plymouth 테마 설치만 담당
# ──────────────────────────────────────────────────────────

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
THEME_DIR=/usr/share/plymouth/themes/zyl-os

echo "[zyl-os] Installing Plymouth theme to ${THEME_DIR} ..."

sudo mkdir -p "${THEME_DIR}"
sudo cp -r "${SCRIPT_DIR}/zyl-os/"* "${THEME_DIR}/"
sudo plymouth-set-default-theme zyl-os

echo "[zyl-os] Updating initramfs ..."
sudo update-initramfs -u

echo "[zyl-os] Plymouth theme installed successfully."
