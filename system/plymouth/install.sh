#!/bin/bash
# ──────────────────────────────────────────────────────────
# Zyl OS Plymouth Theme Installer
#
# Copies the boot splash theme to the system plymouth
# directory and rebuilds initramfs.
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
