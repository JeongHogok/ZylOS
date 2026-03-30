# Zyl OS

**Developer Preview v0.1.0** — BPI-F3 (SpacemiT K1 RISC-V) 기반 모바일 운영체제

> Zyl OS v0.1.0은 **개발자 프리뷰**입니다. OS 아키텍처와 앱 개발 환경을 평가하기 위한 목적이며, 일반 사용자용 배포가 아닙니다.

## 특징

- **RISC-V 네이티브**: SpacemiT K1 SoC (8코어 X60) 최적화
- **Wayland 컴포지터**: wlroots 0.18 기반 모바일 전용 (제스처, 풀스크린)
- **웹 앱 런타임**: WebKitGTK 기반 — HTML/CSS/JS로 앱 개발
- **28개 시스템 서비스**: OS 이미지 소유 (apps/system/services.js), 에뮬레이터는 IPC 라우터만 담당
- **20개 시스템 앱**: 홈, 잠금, 설정, 브라우저, 파일, 터미널, 카메라, 갤러리, 전화, 메시지, 연락처, 날씨, 시계, 음악, 계산기, 메모, 앱스토어, OOBE, 상태바, 키보드
- **디바이스 독립 아키텍처**: OS 이미지(apps/)가 에뮬레이터 없이 실제 디바이스에서 독립 동작
- **ZylBridge IPC**: 4모드 자동 감지 (native/webkit/iframe/standalone)
- **ZylAppRegistry**: 동적 앱 로딩 — app.json 메타데이터 기반 앱 레지스트리 (단일 진실 소스)
- **앱별 i18n**: 공유 엔진(shared/i18n.js) + 앱별 번역 데이터 (ko/en/ja/zh/es)
- **6계층 보안**: iframe sandbox + CSP + 권한 체크 + 파일 보호 + 도메인 화이트리스트 + 앱 워치독
- **네이티브 에뮬레이터**: Tauri 데스크톱 앱 — IPC 라우터 + 컴포지터, 비동기 Rust 백엔드
- **A/B OTA**: 원자적 시스템 업데이트 + 자동 롤백
- **코드 품질 보증**: verify-all.sh (11섹션 자동 검증) + pre-commit hook

## 기술 스택

### Zyl OS

| 계층 | 기술 | 역할 |
|------|------|------|
| **커널** | Linux 6.6 (SpacemiT BSP) | RISC-V 하드웨어 드라이버 |
| **부트로더** | U-Boot + OpenSBI | RISC-V 부팅 |
| **디스플레이** | wlroots 0.18 + Wayland | 모바일 컴포지터 (제스처, 풀스크린) |
| **앱 런타임** | WebKitGTK 6.0 | 앱별 독립 WebView 프로세스 |
| **IPC** | D-Bus | 27개 시스템 서비스 통신 |
| **앱 프레임워크** | HTML/CSS/JS (ES5) | 시스템 앱 19개 |
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
| **백엔드** | Rust | 리소스 예약, 파일시스템 보호, 네트워크 조회 |
| **프론트엔드** | HTML/CSS/JS (ES5) | IPC 라우터 + 컴포지터 UI |
| **OS 이미지** | .img (HFS+/ext4) | 앱 번들 디스크 이미지 |
| **스토리지** | sparse 디스크 이미지 마운트 | 실제 파일시스템 예약 (4~32GB) |
| **메모리** | cgroup v2 (Linux) / rlimit (macOS) | 실제 RAM 예약 |
| **WiFi/BT** | airport/nmcli, system_profiler/bluetoothctl | 호스트 하드웨어 연동 |
| **설정 영속화** | JSON (마운트 포인트) | 재부팅 시 설정 유지 |
| **빌드** | Cargo + tauri-cli | macOS/Linux 배포 |

### 시스템 서비스 (28개)

| 서비스 | 역할 |
|--------|------|
| fs | 마운트 디스크 이미지 I/O |
| device | 디바이스 메타데이터 |
| storage | 디스크 사용량 조회 |
| apps | 설치된 앱 목록/관리 |
| settings | 영속 설정 JSON (localStorage 폴백) |
| terminal | 셸 명령 실행 (22패턴 위험 명령 필터링) |
| wifi | WiFi 스캔/연결 (비동기) |
| bluetooth | BT 디바이스 조회 (비동기) |
| network | HTTP fetch (비동기 curl / native fetch API 폴백) |
| browser | URL 로딩/탐색 |
| notification | 채널 기반 알림 (post/cancel/clear) |
| power | 밝기, 배터리, 절전 모드 |
| display | 해상도/회전/DPI |
| input | 키보드/IME 상태 |
| sensors | 가속도/자이로/근접/조도 |
| location | IP 기반 geolocation (비동기, Geolocation API 폴백) |
| telephony | 통화 발신/수신/종료, SIM 정보, 통화 이력 |
| contacts | 연락처 CRUD, 검색, 그룹 관리 |
| messaging | SMS/MMS 대화 스레드, 메시지 송수신 |
| usb | configfs 가젯 (MTP/ADB) |
| user | 사용자 프로필 |
| credential | 암호화 키체인 (AES-256-GCM) |
| appstore | .ospkg 패키지 검증/설치 (RSA-2048 + SHA-256) |
| updater | A/B 파티션 OTA 업데이트, 버전 비교 |
| sandbox | iframe sandbox + CSP + Permissions Policy |
| logger | JSON 구조화 로깅 |
| accessibility | 고대비, 폰트 스케일 |
| audio | 5개 볼륨 카테고리, 키클릭, 알림음, 진동 |

## 빠른 시작

### 방법 1: 빌드된 에뮬레이터 다운로드 (가장 간편)

> [**v0.1.0 릴리즈 다운로드**](https://github.com/JeongHogok/ZylOS/releases/tag/v0.1.0)

| 파일 | 플랫폼 |
|------|--------|
| `Zyl OS Emulator_0.1.0_aarch64.dmg` | macOS (Apple Silicon) |

1. `.dmg` 파일 다운로드 → 더블클릭 → Applications에 드래그
2. 앱 실행 → 디바이스 선택 → 부팅 → OOBE(초기 설정) → 홈 화면

> Linux / Intel Mac은 아래 소스 빌드 방법을 사용하세요.

### 방법 2: 소스에서 빌드

#### 사전 요구사항
- **Rust** 1.70+ (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`)
- **Tauri CLI** (`cargo install tauri-cli`)
- macOS: Xcode Command Line Tools / Linux: `libwebkit2gtk-4.1-dev libgtk-3-dev`

#### 빌드 및 실행
```bash
# 1. 저장소 클론
git clone https://github.com/JeongHogok/ZylOS.git
cd ZylOS

# 2. 개발 모드로 에뮬레이터 실행 (핫 리로드 지원)
cd emulator-app
cargo tauri dev

# 3. 릴리즈 빌드 (배포용 .app / .deb / .AppImage 생성)
cargo tauri build
```

#### 에뮬레이터 사용 흐름
1. **디바이스 선택** — F3 Gesture / F3 Lite / F3 Classic 중 선택
2. **OS 이미지** — 자동 생성 또는 기존 이미지 불러오기
3. **부팅** → **OOBE** (언어 선택, WiFi, PIN 설정) → **홈 화면**
4. 20개 시스템 앱 자유롭게 사용

#### 릴리즈 빌드 산출물
```
macOS: target/release/bundle/macos/Zyl OS Emulator.app
       target/release/bundle/dmg/Zyl OS Emulator_0.1.0_aarch64.dmg
Linux: target/release/bundle/deb/zyl-os-emulator_0.1.0_amd64.deb
       target/release/bundle/appimage/zyl-os-emulator_0.1.0_amd64.AppImage
```

### 코드 검증
```bash
# 전체 코드베이스 11섹션 검증 (에러 0이면 배포 가능)
bash tests/verify-all.sh

# OS 이미지 독립성만 검증
bash tests/check-os-independence.sh
```

### 네이티브 빌드 (실제 디바이스용)
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
apps/                시스템 앱 20개 (HTML/CSS/JS, ES5)
  apps/system/         OS 서비스 프레임워크 (28개 서비스)
    services.js          서비스 비즈니스 로직 + 타임아웃 + 워치독
    permissions.js       권한 관리 (SYSTEM_APPS 화이트리스트)
    security.js          파일 보호 (settings.json, .credentials)
    sandbox.js           iframe sandbox + CSP + 도메인 화이트리스트
    app-registry.js      동적 앱 레지스트리 (dock, hidden, undeletable)
  apps/shared/         공유 런타임
    bridge.js            IPC 추상화 (4모드: native/webkit/iframe/standalone)
    i18n.js              다국어 엔진 (ko/en/ja/zh/es)
    touch-scroll.js      통합 터치 엔진 (scroll, swipe, drag, gesture bar)
  apps/keyboard/       가상 키보드 (QWERTY/두벌식/Spanish)

emulator-app/        Tauri 네이티브 에뮬레이터 (IPC 라우터 + 컴포지터)
  src/                 Rust 백엔드 (비동기 I/O — tokio)
  ui/                  프론트엔드 (컴포지터, HAL)

compositor/          Wayland 모바일 컴포지터 (C, wlroots)
runtime/wam/         Web Application Manager (C, WebKitGTK)
runtime/hal/         Hardware Abstraction Layer (C)
runtime/services/    시스템 서비스 18개 (C, D-Bus)
system/              systemd, Plymouth, DTS, AppArmor, 복구 모드
tests/               단위/통합 + verify-all.sh (11섹션 자동 검증)
tools/               빌드/프로파일링 도구
docs/                문서
CLAUDE.md            프로젝트 규칙 (AI/개발자 공통)
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

**GNU General Public License v3.0 (GPL-3.0)** — [LICENSE](LICENSE) 참고

Zyl OS를 수정하거나 포함하여 배포하는 프로젝트는 전체 소스 코드를 GPL-3.0으로 공개해야 합니다.

의존성 라이선스 정보: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
