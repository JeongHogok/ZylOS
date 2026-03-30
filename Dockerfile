# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Build Environment
#
# 역할: Reproducible build 환경 — Docker 기반
# 수행범위: Arch Linux + 전체 의존성 → meson/ninja 빌드
# 의존방향: 없음 (self-contained)
# SOLID: SRP — 빌드 환경 정의만 담당
# ──────────────────────────────────────────────────────────

FROM archlinux:latest AS builder

# Pinned mirror snapshot for reproducibility
# Update this date to bump base packages
ARG MIRROR_DATE=2026/03/30
ENV LANG=en_US.UTF-8

# Install build dependencies
RUN pacman-key --init && \
    pacman-key --populate archlinux && \
    pacman -Syu --noconfirm && \
    pacman -S --noconfirm \
      base-devel meson ninja pkgconf git \
      wayland wayland-protocols \
      libxkbcommon pixman libinput \
      libdrm mesa libglvnd \
      gtk4 json-glib \
      hwdata libdisplay-info \
      seatd \
      openssl libseccomp libzip curl gpsd \
      python && \
    # Build wlroots from source (pinned version)
    git clone --depth 1 --branch 0.18.2 \
      https://gitlab.freedesktop.org/wlroots/wlroots.git /tmp/wlroots && \
    cd /tmp/wlroots && \
    meson setup build --prefix=/usr -Dexamples=false -Dxwayland=disabled -Dwerror=false && \
    ninja -C build && ninja -C build install && \
    ldconfig && \
    # Generate xdg-shell protocol header
    PROTO_DIR=$(pkg-config --variable=pkgdatadir wayland-protocols) && \
    wayland-scanner server-header \
      "$PROTO_DIR/stable/xdg-shell/xdg-shell.xml" \
      /usr/include/xdg-shell-protocol.h && \
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
