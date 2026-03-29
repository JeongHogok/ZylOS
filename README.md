# Zyl OS

**Developer Preview v0.1.0** — BPI-F3 (SpacemiT K1 RISC-V) 기반 모바일 운영체제

> ⚠️ Zyl OS v0.1.0은 **개발자 프리뷰**입니다. OS 아키텍처와 앱 개발 환경을 평가하기 위한 목적이며, 일반 사용자용 배포가 아닙니다. 보안 제한사항은 [SECURITY.md](docs/SECURITY.md)를 참고하세요.

## 특징

- **RISC-V 네이티브**: SpacemiT K1 SoC (8코어 X60) 최적화
- **Wayland 컴포지터**: wlroots 0.18 기반 모바일 전용 (제스처, 풀스크린)
- **웹 앱 런타임**: WebKitGTK 기반 — HTML/CSS/JS로 앱 개발
- **14개 시스템 서비스**: D-Bus IPC (전력, 센서, GPS, 전화, 알림 등)
- **5계층 보안**: namespace + seccomp + cgroup + network + D-Bus 정책
- **에뮬레이터**: 브라우저에서 실행 가능한 디바이스 에뮬레이터
- **A/B OTA**: 원자적 시스템 업데이트 + 자동 롤백

## 빠른 시작

### 에뮬레이터 (하드웨어 불필요)
```bash
# 방법 1: 네이티브 앱 (Tauri — 실제 리소스 예약)
cd emulator-app && cargo tauri dev

# 방법 2: 브라우저 (간편 테스트)
python3 -m http.server 9000
# http://localhost:9000/emulator/index.html
```

### 네이티브 빌드
```bash
./tools/setup-toolchain.sh
meson setup builddir
ninja -C builddir
```

### 크로스 컴파일 (RISC-V)
```bash
meson setup builddir-riscv --cross-file tools/riscv64-cross.ini
ninja -C builddir-riscv
```

## 프로젝트 구조

```
compositor/          Wayland 모바일 컴포지터
runtime/wam/         Web Application Manager
runtime/hal/         Hardware Abstraction Layer
runtime/services/    시스템 서비스 (14개)
apps/                시스템 앱 (11개)
emulator/            웹 기반 디바이스 에뮬레이터 (브라우저)
emulator-app/        Tauri 네이티브 에뮬레이터 (.app/.dmg/.deb)
system/              systemd, Plymouth, DTS, AppArmor, 복구 모드
tests/               단위/통합 테스트
tools/               빌드/프로파일링 도구
docs/                문서
```

## 문서

- [앱 개발 가이드](docs/APP_DEVELOPMENT_GUIDE.md)
- [아키텍처](docs/ARCHITECTURE.md)
- [배포 가이드](docs/DEPLOYMENT_GUIDE.md)
- [보안 안내](docs/SECURITY.md)
- [기여 가이드](CONTRIBUTING.md)
- [감사 보고서](docs/AUDIT_REPORT.md)

## 라이선스

MIT License — [LICENSE](LICENSE) 참고

LGPL 의존성 정보: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
