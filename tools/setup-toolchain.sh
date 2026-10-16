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
        # crossbuild-essential 이 gcc·g++·binutils 를 한 묶음으로 가져온다
        sudo apt install -y crossbuild-essential-riscv64

        info "빌드 도구 설치..."
        sudo apt install -y meson ninja-build pkg-config cmake git

        info "이미지 빌드 도구 설치 (tools/build-image.sh)..."
        sudo apt install -y debootstrap fdisk e2fsprogs systemd dbus

        info "QEMU RISC-V 설치..."
        # qemu-system-riscv64 는 qemu-system-misc 안에 있다. qemu-user-static
        # 은 다른 아키텍처 rootfs 조립(2단계 부트스트랩)에 쓰이고, 등록은
        # systemd-binfmt 가 한다 (systemd 없는 컨테이너면 직접 호출)
        sudo apt install -y qemu-system-misc qemu-user-static

        info "wlroots 빌드 의존성 설치..."
        sudo apt install -y \
            libwayland-dev wayland-protocols libwayland-server0 \
            libwlroots-0.18-dev \
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

        # riscv64 개발 라이브러리(:riscv64)는 여기서 깔지 않는다 — 호스트
        # 아키텍처 개발 패키지와 같은 시스템에 섞으면 libpsl-dev 처럼 man
        # 페이지가 충돌해 dpkg 가 멈춘다. 교차 빌드는 전용 컨테이너에서:
        # CI 의 qemu-boot-riscv64 잡이 패키지 목록의 단일 출처다
        info "riscv64 개발 라이브러리는 전용 컨테이너에서 — docs/DEPLOYMENT_GUIDE.md"

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
            qemu-user-static \
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
info "  1. 네이티브 빌드 (테스트):"
info "     meson setup builddir && ninja -C builddir"
info ""
info "  2. 크로스 컴파일 (RISC-V, :riscv64 개발 라이브러리가 있는 컨테이너에서):"
info "     meson setup builddir-riscv --prefix=/usr --cross-file tools/cross/riscv64.ini -Dboard=bpi-f3 -Dprofile=phone"
info ""
info "  3. QEMU로 테스트 (이미지 조립 + 부팅 + 자기점검):"
info "     tools/build-image.sh --builddir <builddir> --out out/image --with-kernel"
info "     tools/run-qemu.sh --image out/image/zyl-os-<board>-<profile>.img --builddir <builddir> --check"
info "────────────────────────────────────"
