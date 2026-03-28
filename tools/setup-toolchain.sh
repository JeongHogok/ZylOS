#!/bin/bash
#
# Zyl OS 개발 환경 설정 스크립트
# RISC-V 64비트 크로스 컴파일 툴체인 및 의존성 설치
#

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${GREEN}[INFO]${NC} $1"; }
warn()  { echo -e "${YELLOW}[WARN]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; exit 1; }

OS="$(uname -s)"
ARCH="$(uname -m)"

info "Zyl OS 개발 환경 설정"
info "Host: $OS ($ARCH)"
echo ""

# ─── macOS ───
if [ "$OS" = "Darwin" ]; then
    info "macOS 환경 감지"

    # Homebrew 확인
    if ! command -v brew &>/dev/null; then
        error "Homebrew가 설치되어 있지 않습니다. https://brew.sh 에서 설치하세요."
    fi

    info "RISC-V 크로스 컴파일러 설치..."
    brew install riscv64-elf-gcc 2>/dev/null || brew upgrade riscv64-elf-gcc 2>/dev/null || true

    # riscv64-unknown-linux-gnu 툴체인 (Linux 타겟)
    if ! command -v riscv64-unknown-linux-gnu-gcc &>/dev/null; then
        warn "riscv64-linux-gnu-gcc가 없습니다. 수동 설치가 필요할 수 있습니다."
        warn "대안: Docker 기반 크로스 컴파일 또는 QEMU usermode 사용"
        warn "  docker pull riscv64/ubuntu:24.04"
    fi

    info "빌드 도구 설치..."
    brew install meson ninja pkg-config cmake 2>/dev/null || true
    brew install qemu 2>/dev/null || true

    info "Wayland/wlroots 의존성 (호스트 빌드 테스트용)..."
    brew install wayland-protocols libxkbcommon pixman 2>/dev/null || true

# ─── Linux (Ubuntu/Debian) ───
elif [ "$OS" = "Linux" ]; then
    info "Linux 환경 감지"

    if command -v apt &>/dev/null; then
        info "APT 패키지 설치..."
        sudo apt update

        # RISC-V 크로스 컴파일러
        info "RISC-V 크로스 컴파일러 설치..."
        sudo apt install -y gcc-riscv64-linux-gnu g++-riscv64-linux-gnu

        # 빌드 도구
        info "빌드 도구 설치..."
        sudo apt install -y meson ninja-build pkg-config cmake git

        # QEMU
        info "QEMU RISC-V 설치..."
        sudo apt install -y qemu-system-riscv64 qemu-user-static

        # wlroots 의존성
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

        # WebKitGTK 의존성
        info "WebKitGTK 의존성 설치..."
        sudo apt install -y \
            libwebkitgtk-6.0-dev \
            libgtk-4-dev \
            libjson-glib-dev

        # 크로스 컴파일용 라이브러리 (riscv64)
        info "RISC-V 크로스 라이브러리 설치..."
        sudo dpkg --add-architecture riscv64 2>/dev/null || true
        sudo apt install -y \
            libwayland-dev:riscv64 \
            libxkbcommon-dev:riscv64 \
            libpixman-1-dev:riscv64 \
            2>/dev/null || warn "일부 riscv64 크로스 라이브러리 설치 실패 (정상일 수 있음)"

    elif command -v pacman &>/dev/null; then
        info "Arch Linux 환경"
        sudo pacman -S --needed \
            riscv64-linux-gnu-gcc \
            meson ninja cmake pkgconf \
            qemu-system-riscv \
            wayland wayland-protocols \
            wlroots libxkbcommon pixman libinput \
            webkit2gtk-4.1 gtk4 json-glib
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
