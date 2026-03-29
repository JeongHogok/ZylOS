# Zyl OS

**Developer Preview v0.1.0** — BPI-F3 (SpacemiT K1 RISC-V) 기반 모바일 운영체제

> Zyl OS v0.1.0은 **개발자 프리뷰**입니다. OS 아키텍처와 앱 개발 환경을 평가하기 위한 목적이며, 일반 사용자용 배포가 아닙니다.

## 특징

- **RISC-V 네이티브**: SpacemiT K1 SoC (8코어 X60) 최적화
- **Wayland 컴포지터**: wlroots 0.18 기반 모바일 전용 (제스처, 풀스크린)
- **웹 앱 런타임**: WebKitGTK 기반 — HTML/CSS/JS로 앱 개발
- **24개 시스템 서비스**: D-Bus IPC (전력, 센서, GPS, 전화, 알림, 접근성, 로깅 등)
- **16개 시스템 앱**: 홈, 잠금, 설정, 브라우저, 파일, 터미널, 카메라, 갤러리 등
- **앱별 i18n**: 공유 엔진(shared/i18n.js) + 앱별 번역 데이터 (ko/en/ja/zh/es)
- **5계층 보안**: namespace + seccomp + cgroup + network + D-Bus 정책
- **네이티브 에뮬레이터**: Tauri 데스크톱 앱 — IP 기반 위치, 실제 WiFi/BT, 카메라 녹화
- **A/B OTA**: 원자적 시스템 업데이트 + 자동 롤백

## 기술 스택

### Zyl OS

| 계층 | 기술 | 역할 |
|------|------|------|
| **커널** | Linux 6.6 (SpacemiT BSP) | RISC-V 하드웨어 드라이버 |
| **부트로더** | U-Boot + OpenSBI | RISC-V 부팅 |
| **디스플레이** | wlroots 0.18 + Wayland | 모바일 컴포지터 (제스처, 풀스크린) |
| **앱 런타임** | WebKitGTK 6.0 | 앱별 독립 WebView 프로세스 |
| **IPC** | D-Bus | 24개 시스템 서비스 통신 |
| **앱 프레임워크** | HTML/CSS/JS (ES5) | 시스템 앱 16개 |
| **HAL** | C (sysfs, wpa_supplicant, BlueZ, PipeWire) | 하드웨어 추상화 |
| **샌드박싱** | namespace + seccomp-bpf + cgroup v2 | 5계층 앱 격리 |
| **패키징** | .ospkg (RSA-2048 서명) | 앱 배포/검증 |
| **업데이트** | A/B 파티션 + SHA-256 | 원자적 OTA |
| **빌드** | Meson + Ninja, C11 | 네이티브/크로스 컴파일 |
| **크로스 컴파일** | riscv64-linux-gnu-gcc | RISC-V 타겟 빌드 |

### 에뮬레이터

| 계층 | 기술 | 역할 |
|------|------|------|
| **앱 프레임워크** | Tauri 2.x (Rust + WebView) | 독립 실행파일 (.app/.dmg/.deb) |
| **백엔드** | Rust | 리소스 예약, 파일시스템, 네트워크 조회 |
| **프론트엔드** | HTML/CSS/JS (ES5) | 설정 UI, 부팅 시퀀스, 디바이스 프레임 |
| **OS 이미지** | .img (HFS+/ext4) | 앱 번들 디스크 이미지 |
| **스토리지** | sparse 디스크 이미지 마운트 | 실제 파일시스템 예약 (4~32GB) |
| **메모리** | cgroup v2 (Linux) / rlimit (macOS) | 실제 RAM 예약 |
| **WiFi/BT** | airport/nmcli, system_profiler/bluetoothctl | 호스트 하드웨어 연동 |
| **설정 영속화** | JSON (마운트 포인트) | 재부팅 시 설정 유지 |
| **빌드** | Cargo + tauri-cli | macOS/Linux 배포 |

### 시스템 서비스 (24개, D-Bus)

| 서비스 | D-Bus 이름 | 기술 |
|--------|-----------|------|
| 알림 | org.zylos.Notification | 채널 기반 알림 시스템 |
| 전력 | org.zylos.PowerManager | sysfs 백라이트, CPU governor |
| 디스플레이 | org.zylos.DisplayManager | DRM/KMS, 자동 회전 |
| 입력 | org.zylos.InputService | evdev, 멀티터치 10점 |
| 센서 | org.zylos.SensorService | IIO (가속도/자이로/근접/조도/자기) |
| 위치 | org.zylos.LocationService | GPSD + GeoIP 퓨전 |
| 통화 | org.zylos.Telephony | ModemManager (통화/SMS/5G) |
| USB | org.zylos.UsbManager | configfs 가젯 (MTP/ADB) |
| 사용자 | org.zylos.UserManager | 멀티유저 프로필 |
| 자격증명 | org.zylos.CredentialManager | 암호화 키체인 |
| 접근성 | org.zylos.Accessibility | 고대비, 폰트 스케일 |
| 로깅 | org.zylos.Logger | JSON 구조화 로깅, 크래시 보고 |
| 파일시스템 | (서비스 라우터) | 마운트 디스크 이미지 I/O |
| 디바이스 | (서비스 라우터) | 디바이스 메타데이터 |
| 스토리지 | (서비스 라우터) | 디스크 사용량 조회 |
| 앱 레지스트리 | (서비스 라우터) | 설치된 앱 목록/관리 |
| 설정 | (서비스 라우터) | 영속 설정 JSON |
| 터미널 | (서비스 라우터) | 셸 명령 실행 (22패턴 필터링) |
| WiFi | (HAL 연동) | 호스트 WiFi 스캔/연결 |
| 블루투스 | (HAL 연동) | 호스트 BT 디바이스 조회 |
| 브라우저 | (앱 라우터) | URL 로딩/탐색 |
| 앱스토어 | (라이브러리) | 패키지 검증/설치 (.ospkg) |
| 업데이터 | (라이브러리) | OTA A/B 파티션 업데이트 |
| 샌드박스 | (라이브러리) | seccomp + namespace + cgroup |

## 빠른 시작

### 에뮬레이터 (하드웨어 불필요)
```bash
# 네이티브 앱 (Tauri — 실제 리소스 예약)
cd emulator-app && cargo tauri dev

# 브라우저 (간편 테스트)
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
compositor/          Wayland 모바일 컴포지터 (C, wlroots)
runtime/wam/         Web Application Manager (C, WebKitGTK)
runtime/hal/         Hardware Abstraction Layer (C)
runtime/services/    시스템 서비스 16개 (C, D-Bus) + 에뮬레이터 라우터 8개 = 24개
apps/                시스템 앱 16개 (HTML/CSS/JS)

emulator-app/        Tauri 네이티브 에뮬레이터 (Rust + HTML/CSS/JS)
system/              systemd, Plymouth, DTS, AppArmor, 복구 모드
tests/               단위/통합 테스트 (C, bash)
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
- [에뮬레이터](emulator-app/README.md)

## 라이선스

MIT License — [LICENSE](LICENSE) 참고

LGPL 의존성 정보: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
