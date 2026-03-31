#!/bin/bash
# [Clean Architecture] Infrastructure Layer - Tool
# 역할: RISC-V 64비트 크로스 컴파일 툴체인 및 의존성 설치
# 수행범위: 패키지 설치, 툴체인 다운로드/설정. 빌드 자체는 수행하지 않음
# 의존방향: 외부 패키지 매니저(apt/dnf), 호스트 OS
# SOLID: SRP — 개발 환경 설정만 담당

set -euo pipefail

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }
need_cmd() { command -v "$1" >/dev/null 2>&1 || error "required command not found: $1"; }

OS="$(uname -s)"
ARCH="$(uname -m)"

info "Zyl OS 개발 환경 설정"
info "Host: $OS ($ARCH)"
echo ""

# ─── macOS ───
if [ "$OS" = "Darwin" ]; then
    info "macOS 환경 감지"

    need_cmd brew

    info "RISC-V bare-metal 크로스 컴파일러 설치..."
    brew install riscv64-elf-gcc 2>/dev/null || brew upgrade riscv64-elf-gcc 2>/dev/null || true

    info "빌드 도구 설치..."
    brew install meson ninja pkg-config cmake qemu 2>/dev/null || true

    info "Wayland/wlroots 의존성 (호스트 빌드 테스트용)..."
    brew install wayland-protocols libxkbcommon pixman 2>/dev/null || true

    # riscv64-unknown-linux-gnu 툴체인 (Linux target) is mandatory for the
    # advertised cross-file. Previously this was only a warning and the script
    # exited successfully even though follow-up builds were guaranteed to fail.
    if ! command -v riscv64-unknown-linux-gnu-gcc >/dev/null 2>&1; then
        error "riscv64-unknown-linux-gnu-gcc가 없습니다. macOS에서는 Linux-target RISC-V toolchain을 별도 설치하거나 Docker 기반 cross-compile을 사용해야 합니다.\n예: docker run --rm -it -v \"$PWD:/src\" riscv64/ubuntu:24.04"
    fi

    need_cmd meson
    need_cmd ninja
    need_cmd pkg-config
    need_cmd qemu-system-riscv64

# ─── Linux (Ubuntu/Debian) ───
elif [ "$OS" = "Linux" ]; then
    info "Linux 환경 감지"

    if command -v apt >/dev/null 2>&1; then
        need_cmd sudo
        info "APT 패키지 설치..."
        sudo apt update

        info "RISC-V 크로스 컴파일러 설치..."
        sudo apt install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu

        info "빌드 도구 설치..."
        sudo apt install -y meson ninja-build pkg-config cmake git

        info "QEMU RISC-V 설치..."
        sudo apt install -y qemu-system-riscv64 qemu-user-static

        info "wlroots 빌드 의존성 설치..."
        sudo apt install -y \
            libwayland-dev wayland-protocols libwayland-server0 \
            libwlroots-dev \
            libxkbcommon-dev \
            libpixman-1-dev \
            libinput-dev \
            libdrm-dev \
            libegl-dev libgles2-mesa-dev \
            libgbm-dev

        info "WebKitGTK 의존성 설치..."
        sudo apt install -y \
            libwebkitgtk-6.0-dev \
            libgtk-4-dev \
            libjson-glib-dev

        info "RISC-V 크로스 라이브러리 설치..."
        sudo dpkg --add-architecture riscv64
        sudo apt install -y \
            libwayland-dev:riscv64 \
            libxkbcommon-dev:riscv64 \
            libpixman-1-dev:riscv64 \
            || warn "일부 riscv64 크로스 라이브러리 설치 실패 (패키지 저장소 구성에 따라 정상일 수 있음)"

        need_cmd riscv64-linux-gnu-gcc
        need_cmd riscv64-linux-gnu-g++
        need_cmd meson
        need_cmd ninja

    elif command -v pacman >/dev/null 2>&1; then
        need_cmd sudo
        info "Arch Linux 환경"
        sudo pacman -S --needed \
            riscv64-linux-gnu-gcc \
            meson ninja cmake pkgconf \
            qemu-system-riscv \
            wayland wayland-protocols \
            wlroots libxkbcommon pixman libinput \
            webkit2gtk-4.1 gtk4 json-glib

        need_cmd riscv64-linux-gnu-gcc
        need_cmd meson
        need_cmd ninja
    else
        error "지원되지 않는 Linux 배포판입니다. Ubuntu/Debian 또는 Arch를 사용하세요."
    fi
else
    error "지원되지 않는 OS입니다: $OS"
fi

echo ""
info "────────────────────────────────────"
info "개발 환경 설정 완료!"
echo ""
info "다음 단계:"
info "  1. 컴포지터 빌드 (네이티브 테스트):"
info "     cd compositor && meson setup builddir && ninja -C builddir"
info ""
info "  2. 크로스 컴파일 (RISC-V):"
info "     cd compositor && meson setup builddir-riscv --cross-file ../tools/riscv64-cross.ini"
info ""
info "  3. QEMU로 테스트:"
info "     qemu-system-riscv64 -machine virt -m 4G -kernel <kernel> -append 'root=/dev/vda' -drive file=<rootfs.img>"
info "────────────────────────────────────"
