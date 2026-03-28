# Zyl OS

BPI-F3 (SpacemiT K1 RISC-V) 기반 커스텀 모바일 운영체제

## 아키텍처

- **커널**: Linux (SpacemiT BSP 6.6)
- **디스플레이**: Wayland (wlroots 기반 모바일 컴포지터)
- **앱 런타임**: WebKitGTK
- **앱**: HTML/CSS/JS 웹 앱
- **IPC**: D-Bus

## 빌드

```bash
# 의존성 설치 (macOS 호스트)
./tools/setup-toolchain.sh

# 컴포지터 빌드 (크로스 컴파일)
cd compositor && meson setup builddir --cross-file ../tools/riscv64-cross.ini && ninja -C builddir

# WAM 빌드
cd runtime/wam && meson setup builddir --cross-file ../../tools/riscv64-cross.ini && ninja -C builddir
```

## 디렉토리 구조

```
compositor/     wlroots 기반 모바일 컴포지터
runtime/wam/    Web Application Manager
runtime/bridge/ JS-Native Bridge
runtime/services/ D-Bus 시스템 서비스
apps/           시스템 앱 (HTML/CSS/JS)
tools/          빌드/개발 도구
docs/           문서
```
