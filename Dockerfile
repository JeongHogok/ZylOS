# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Build Environment
#
# 역할: Reproducible build 환경 — Docker 기반
# 수행범위: Arch Linux + 전체 의존성 → meson/ninja 빌드
# 의존방향: 없음 (self-contained)
# SOLID: SRP — 빌드 환경 정의만 담당
# ──────────────────────────────────────────────────────────

# Arch 는 amd64 이미지만 낸다 — ARM 호스트(로컬 CI)에서도 이 이미지를 쓰게 명시
FROM --platform=linux/amd64 archlinux:latest AS builder

ENV LANG=en_US.UTF-8

# Install build dependencies.
# 기본 미러 둘은 같은 CDN 이라 동기화 중이면 함께 실패한다 — 독립 1티어 미러를
# 뒤에 두고 일시 장애는 재시도로 넘긴다 (CI 의 build-native 와 같은 처방)
# 에뮬레이션(ARM 호스트) 안에서는 seccomp 필터를 못 걸어 pacman 다운로드
# 샌드박스가 실패한다 — 끈다. 패키지 서명 검증은 그대로다
RUN pacman-key --init && \
    pacman-key --populate archlinux && \
    sed -i 's/^#DisableSandboxSyscalls/DisableSandboxSyscalls/' /etc/pacman.conf && \
    printf 'Server = https://%s/$repo/os/$arch\n' \
      mirror.rackspace.com/archlinux mirrors.kernel.org/archlinux \
      >> /etc/pacman.d/mirrorlist && \
    for i in 1 2 3; do \
      pacman -Syu --noconfirm --needed \
        base-devel meson ninja pkgconf git \
        wayland wayland-protocols \
        libxkbcommon pixman libinput \
        libdrm mesa libglvnd \
        gtk4 webkitgtk-6.0 json-glib \
        hwdata libdisplay-info \
        seatd \
        openssl libzip curl gpsd \
        python && break; \
      [ "$i" = 3 ] && exit 1; sleep 30; \
    done && \
    # Build wlroots from source (pinned version)
    for i in 1 2 3; do \
      git clone --depth 1 --branch 0.18.2 \
        https://gitlab.freedesktop.org/wlroots/wlroots.git /tmp/wlroots && break; \
      [ "$i" = 3 ] && exit 1; rm -rf /tmp/wlroots; sleep 15; \
    done && \
    cd /tmp/wlroots && \
    meson setup build --prefix=/usr -Dexamples=false -Dxwayland=disabled -Dwerror=false && \
    ninja -C build && ninja -C build install && \
    ldconfig && \
    # Cleanup
    rm -rf /tmp/wlroots && \
    pacman -Scc --noconfirm

# Copy source
WORKDIR /build
COPY . .

# Build
RUN meson setup builddir --prefix=/usr && \
    ninja -C builddir

# Validate manifests
RUN for f in apps/*/app.json; do \
      python3 -c "import json; json.load(open('$f'))" || exit 1; \
    done && echo "All manifests valid"

# ─── Output stage: just the build artifacts ───
FROM scratch AS output
COPY --from=builder /build/builddir /builddir
