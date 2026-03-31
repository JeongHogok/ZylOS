# Zyl OS

**Developer Preview v0.1.0** — BPI-F3 (SpacemiT K1 RISC-V) 기반 모바일 운영체제

> Zyl OS v0.1.0은 **개발자 프리뷰**입니다. OS 아키텍처와 앱 개발 환경을 평가하기 위한 목적이며, 일반 사용자용 배포가 아닙니다.

## 특징

- **RISC-V 네이티브**: SpacemiT K1 SoC (8코어 X60) 최적화
- **Wayland 컴포지터**: wlroots 0.18 기반 모바일 전용 (제스처, 풀스크린)
- **웹 앱 런타임**: WebKitGTK 기반 — HTML/CSS/JS로 앱 개발
- **26개 C 서비스 디렉토리 / 29개 JS 서비스 모듈**: OS 이미지 소유, 에뮬레이터는 IPC 라우터만 담당
- **22개 시스템 앱**: 홈, 잠금, 설정, 브라우저, 파일, 터미널, 카메라, 갤러리, 전화, 메시지, 연락처, 날씨, 시계, 음악, 계산기, 메모, 앱스토어, OOBE, 상태바, 키보드, 공유 라이브러리, 가상 키보드
- **디바이스 독립 아키텍처**: OS 이미지(apps/)가 에뮬레이터 없이 실제 디바이스에서 독립 동작
- **ZylBridge IPC**: requestId + Promise 매칭 방식, 4모드 자동 감지 (native/webkit/iframe/standalone)
- **ZylAppRegistry**: 동적 앱 로딩 — app.json 메타데이터 기반 앱 레지스트리 (단일 진실 소스)
- **Intent 시스템**: Android 스타일 인텐트 — 명시적/암시적 앱 간 통신
- **ContentProvider**: URI 기반 앱 간 데이터 공유 게이트웨이
- **런타임 퍼미션**: 위험 권한 요청 시 OS 수준 다이얼로그 (ZylPermissionDialog)
- **클립보드 API**: 앱 간 텍스트/mimeType 공유 (clipboard.js 서비스)
- **앱별 i18n**: 공유 엔진(shared/i18n.js) + 앱별 번역 데이터 (ko/en/ja/zh/es)
- **7계층 보안**: iframe sandbox + CSP + 권한 체크 + 파일 보호 + 도메인 화이트리스트 + 앱 워치독 + Verified Boot
- **네이티브 보안**: OpenSSL AES-256-GCM 크리덴셜, RSA-2048+SHA-256 앱 서명, libseccomp BPF (3프로필), dm-verity
- **네이티브 에뮬레이터**: Tauri 2.x (Rust) — IPC 라우터 + 컴포지터, aes-gcm 암호화
- **A/B OTA**: 원자적 시스템 업데이트 + RSA-2048 서명 검증 + 자동 롤백
- **Verified Boot**: FIT 이미지 서명 + dm-verity (rootfs 무결성)
- **zyl SDK CLI**: init/validate/package/sign/keygen 개발자 워크플로우
- **코드 품질 보증**: verify-all.sh (11섹션 자동 검증) + pre-commit hook
- **코드베이스 규모**: 375개 파일, ~66,300 LoC

## 최근 보강 내역 (v0.1.0)

- **앱 기능 보강**: calc 공학용 모드+이력, clock 세계시계+스누즈, messages 검색/삭제/읽음, weather 다중도시, music 재생목록+앨범아트, phone 즐겨찾기+DTMF, gallery 앨범+슬라이드쇼, browser 히스토리+프라이빗, store 카테고리+상세, terminal 멀티탭+테마, notes 검색/정렬/서식
- **OS 핵심 UI 보강**: home 배지+정렬, keyboard CapsLock+악센트, lockscreen 잠금카운트다운, statusbar 알림관리, oobe 역슬라이드

## 기술 스택

### Zyl OS

| 계층 | 기술 | 역할 |
|------|------|------|
| **커널** | Linux 6.6 (SpacemiT BSP) | RISC-V 하드웨어 드라이버 |
| **부트로더** | U-Boot + OpenSBI | RISC-V 부팅 |
| **Verified Boot** | FIT 이미지 서명 + dm-verity | 부트체인 무결성 검증 |
| **디스플레이** | wlroots 0.18 + Wayland | 모바일 컴포지터 (제스처, 풀스크린) |
| **앱 런타임** | WebKitGTK 6.0 | 앱별 독립 WebView 프로세스 |
| **IPC** | D-Bus | C 서비스 간 통신 (~22개 D-Bus 데몬) |
| **앱 프레임워크** | HTML/CSS/JS (ES5) | 시스템 앱 22개 |
| **HAL** | C (sysfs, wpa_supplicant, BlueZ, PipeWire) | 하드웨어 추상화 (9개 구현체) |
| **샌드박싱** | namespace + libseccomp BPF (3프로필 46규칙) + cgroup v2 | 앱 격리 |
| **크리덴셜** | OpenSSL AES-256-GCM + PBKDF2-HMAC-SHA256 | 인증 정보 암호화 |
| **패키징** | .ospkg (RSA-2048+SHA-256 서명, OpenSSL EVP) | 앱 배포/검증 |
| **업데이트** | A/B 파티션 + RSA-2048 서명 검증 | 원자적 OTA |
| **빌드** | Meson + Ninja, C11 | 네이티브/크로스 컴파일 |
| **크로스 컴파일** | riscv64-linux-gnu-gcc | RISC-V 타겟 빌드 |
| **Docker 빌드** | reproducible build 컨테이너 | 재현 가능한 빌드 환경 |

### 에뮬레이터

| 계층 | 기술 | 역할 |
|------|------|------|
| **앱 프레임워크** | Tauri 2.x (Rust + WebView) | 독립 실행파일 (.app/.dmg/.deb) |
| **백엔드** | Rust (tokio) | 리소스 예약, 파일시스템 보호, 네트워크 조회 |
| **크리덴셜 암호화** | aes-gcm + pbkdf2 (Rust crate) | 에뮬레이터 키체인 암호화 |
| **프론트엔드** | HTML/CSS/JS (ES5) | IPC 라우터 + 컴포지터 UI |
| **OS 이미지** | .img (HFS+/ext4) | 앱 번들 디스크 이미지 |
| **스토리지** | sparse 디스크 이미지 마운트 | 실제 파일시스템 예약 (4~32GB) |
| **메모리** | cgroup v2 (Linux) / rlimit (macOS) | 실제 RAM 예약 |
| **WiFi/BT** | airport/nmcli, system_profiler/bluetoothctl | 호스트 하드웨어 연동 |
| **설정 영속화** | JSON (마운트 포인트) | 재부팅 시 설정 유지 |
| **빌드** | Cargo + tauri-cli | macOS/Linux 배포 |

### C 서비스 디렉토리 (26개 — runtime/services/)

| 서비스 | D-Bus 이름 | 역할 |
|--------|-----------|------|
| accessibility | org.zylos.Accessibility | 고대비, 폰트 스케일링 |
| account | org.zylos.AccountManager | 계정 관리 |
| alarm | org.zylos.AlarmManager | 알람 스케줄링 |
| appstore | (라이브러리) | RSA-2048+SHA-256 패키지 검증/설치 |
| audio | org.zylos.AudioService | PipeWire 오디오, 볼륨 카테고리 |
| auth | org.zylos.AuthService | 생체/PIN 인증 |
| bluetooth | org.zylos.BluetoothManager | BlueZ 연동, BT 스캔/연결 |
| camera | org.zylos.CameraService | V4L2 카메라 제어 |
| crash | org.zylos.CrashHandler | 크래시 덤프 수집/보고 |
| credential | org.zylos.CredentialManager | AES-256-GCM + PBKDF2 키체인 |
| dbus | (라이브러리) | 비동기 D-Bus 유틸리티 |
| display | org.zylos.DisplayManager | 해상도, 회전, DPI |
| input | org.zylos.InputService | 가상 키보드, IME, 하드웨어 키 |
| location | org.zylos.LocationService | GPS (GPSD) + GeoIP 퓨전 |
| logger | org.zylos.Logger | JSON 구조화 로깅 |
| nfc | org.zylos.NFCManager | NFC 태그 읽기/쓰기 |
| notification | org.zylos.Notification | 알림 생성/조회/채널 관리 |
| power | org.zylos.PowerManager | Doze 상태머신, CPU 거버너, 웨이크락 |
| sandbox | (라이브러리) | namespace + libseccomp BPF + cgroup v2 |
| sensors | org.zylos.SensorService | 가속도계, 자이로, 근접, 조도 |
| telemetry | org.zylos.TelemetryService | 익명 사용 통계 수집 |
| telephony | org.zylos.Telephony | ModemManager 통합, 통화/SMS |
| updater | (라이브러리) | A/B OTA, RSA-2048 서명 검증 |
| usb | org.zylos.UsbManager | USB 가젯 모드 (MTP/PTP/ADB) |
| user | org.zylos.UserManager | 멀티유저, 프로필 전환, 앱별 UID |
| wifi | org.zylos.WiFiManager | wpa_supplicant 연동, WiFi 스캔/연결 |

### JS 서비스 모듈 (29개 — apps/system/services/)

| 서비스 | 역할 |
|--------|------|
| accessibility | 고대비, 폰트 스케일 |
| apps | 설치된 앱 목록/관리 |
| appstore | .ospkg 패키지 검증/설치 |
| audio | 5개 볼륨 카테고리, 키클릭, 알림음, 진동 |
| bluetooth | BT 디바이스 조회 (비동기) |
| browser | URL 로딩/탐색 |
| clipboard | 텍스트/mimeType 클립보드 copy/paste/clear |
| contacts | 연락처 CRUD, 검색, 그룹 관리 |
| credential | 암호화 키체인 |
| device | 디바이스 메타데이터 |
| display | 해상도/회전/DPI |
| fs | 마운트 디스크 이미지 I/O |
| input | 키보드/IME 상태 |
| location | IP 기반 geolocation (비동기) |
| logger | JSON 구조화 로깅 |
| messaging | SMS/MMS 대화 스레드 |
| network | HTTP fetch (비동기) |
| notification | 채널 기반 알림 (post/cancel/clear) |
| power | 밝기, 배터리, 절전 모드 |
| sandbox | iframe sandbox + CSP + Permissions Policy |
| sensors | 가속도/자이로/근접/조도 |
| settings | 영속 설정 JSON |
| storage | 디스크 사용량 조회 |
| telephony | 통화 발신/수신/종료, SIM 정보 |
| terminal | 셸 명령 실행 (22패턴 위험 명령 필터링) |
| updater | A/B 파티션 OTA 업데이트, 버전 비교 |
| usb | configfs 가젯 (MTP/ADB) |
| user | 사용자 프로필 |
| wifi | WiFi 스캔/연결 (비동기) |

### HAL 구현체 (8개 — runtime/hal/)

| 파일 | 역할 |
|------|------|
| hal_linux.c | HAL Registry 팩토리 (모듈 조립/초기화) |
| hal_wifi.c | wpa_supplicant 연동 |
| hal_bt.c | BlueZ 연동 |
| hal_display.c | DRM/KMS 디스플레이 |
| hal_audio.c | PipeWire 오디오 |
| hal_battery.c | sysfs 배터리 상태 |
| hal_storage.c | 블록 디바이스 I/O |
| hal_cpu.c | CPU 거버너 + 코어 온오프 |

### 시스템 프레임워크 (apps/system/)

| 파일 | 역할 |
|------|------|
| intent.js | Android 스타일 인텐트 — 명시적/암시적 앱 간 라우팅 |
| content-provider.js | URI 기반 앱 간 데이터 공유 게이트웨이 |
| permission-dialog.js | 런타임 위험 권한 요청 다이얼로그 |
| app-registry.js | 동적 앱 레지스트리 (app.json 기반) |
| permissions.js | ZylPermissions — 서비스 호출 시 권한 실시간 검증 |
| security.js | 파일 보호, 자격증명 격리, OS 보안 정책 |
| sandbox.js | iframe sandbox + CSP + 도메인 화이트리스트 |

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

#### 사전 요구사항 — 에뮬레이터 (Rust)
- **Rust** 1.70+ (`curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh`)
- **Tauri CLI** (`cargo install tauri-cli`)
- macOS: Xcode Command Line Tools / Linux: `libwebkit2gtk-4.1-dev libgtk-3-dev`

#### 사전 요구사항 — 네이티브 빌드 (C)
```bash
sudo apt install -y \
  meson ninja-build pkg-config \
  libglib2.0-dev libjson-glib-dev \
  libseccomp-dev libzip-dev \
  libssl-dev libcurl4-openssl-dev \
  gcc-riscv64-linux-gnu g++-riscv64-linux-gnu
```

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

#### Docker 재현 가능 빌드
```bash
docker build -t zylos-build .
docker run --rm -v $(pwd)/output:/output zylos-build
```

#### 에뮬레이터 사용 흐름
1. **디바이스 선택** — F3 Gesture / F3 Lite / F3 Classic 중 선택
2. **OS 이미지** — 자동 생성 또는 기존 이미지 불러오기
3. **부팅** → **OOBE** (언어 선택, WiFi, PIN 설정) → **홈 화면**
4. 22개 시스템 앱 자유롭게 사용

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

### zyl SDK CLI
```bash
# 새 앱 프로젝트 생성
./tools/zyl init my-app

# app.json + 코드 검증
./tools/zyl validate

# .ospkg 패키지 생성
./tools/zyl package

# 패키지 서명 (RSA-2048)
./tools/zyl sign developer.key developer.crt

# 개발자 키 쌍 생성
./tools/zyl keygen "My Name"
```

### Verified Boot 키 생성 및 이미지 서명
```bash
# RSA-2048 부트 키 생성
./tools/gen-boot-keys.sh board/bpi-f3/keys

# FIT 이미지 서명 + dm-verity 해시 트리 생성
./tools/sign-image.sh rootfs.img build/signed
```

## 보안

### 구현된 보안 기능

| 기능 | 구현 상태 | 비고 |
|------|-----------|------|
| 크리덴셜 암호화 | **AES-256-GCM** + PBKDF2-HMAC-SHA256 (100,000회) | OpenSSL EVP API |
| 앱 패키지 서명 | **RSA-2048+SHA-256 실검증** | OpenSSL EVP, libzip |
| OTA 서명 검증 | **RSA-2048+SHA-256 실검증** | OpenSSL EVP |
| seccomp 필터 | **libseccomp BPF — 3프로필, 46개 규칙** | DEFAULT/STRICT/PERMISSIVE |
| Verified Boot | **FIT 이미지 서명 + dm-verity** | gen-boot-keys.sh + sign-image.sh |
| 앱 격리 | **namespace + cgroup v2 + 앱별 UID** | 프로세스별 독립 네임스페이스 |
| 런타임 퍼미션 | **ZylPermissionDialog** | 위험 권한 사용자 승인 필수 |

### 알려진 제한사항

- **Verified Boot**: 에뮬레이터 환경에서는 비적용 (실기기 전용)
- **생체 인증**: auth 서비스가 하드웨어 종속적 — BPI-F3 지문 센서 연동은 BSP 드라이버 필요
- **NFC**: nfc 서비스는 Linux libnfc 연동 구현, 실기기 테스트 미완료
- **Telemetry**: 기본 비활성화, 사용자 동의 후 활성화

## 빌드 의존성

### C 빌드 (Meson)
| 라이브러리 | 용도 |
|-----------|------|
| openssl | AES-256-GCM, RSA-2048, SHA-256 |
| libseccomp | seccomp BPF 샌드박스 |
| libzip | .ospkg 패키지 압축/해제 |
| json-glib | JSON 파싱 |
| glib-2.0, gio-2.0 | D-Bus IPC, 유틸리티 |
| libcurl | HTTP 클라이언트 (OTA, 앱스토어) |

### Rust 빌드 (Cargo)
| crate | 용도 |
|-------|------|
| tauri 2.x | 데스크톱 앱 프레임워크 |
| aes-gcm | AES-256-GCM 암호화 |
| pbkdf2 | 키 파생 |
| tokio | 비동기 런타임 |

## CI/CD

GitHub Actions 파이프라인 (`.github/workflows/`):

| 잡 | 설명 |
|----|------|
| build-native | Meson + Ninja 네이티브 빌드 |
| build-docker | Docker 재현 가능 빌드 |
| lint-html | HTML DOCTYPE 검증 |
| lint-js | ES5 호환성 + 아키텍처 규칙 검증 |
| test-js | JavaScript 단위 테스트 |
| audit-licenses | 라이선스 호환성 검사 |
| fuzz | libFuzzer 기반 퍼징 |
| test-integration | 통합 테스트 |

## 프로젝트 구조

```
apps/                          시스템 앱 22개 (HTML/CSS/JS, ES5)
  apps/system/                 OS 서비스 프레임워크
    services.js                  서비스 라우터 + 비즈니스 로직
    intent.js                    Android 스타일 인텐트 시스템
    content-provider.js          URI 기반 앱 간 데이터 공유
    permission-dialog.js         런타임 권한 요청 다이얼로그
    permissions.js               ZylPermissions (서비스 호출 시 권한 검증)
    security.js                  파일 보호, 자격증명 격리
    sandbox.js                   iframe sandbox + CSP + 도메인 화이트리스트
    app-registry.js              동적 앱 레지스트리 (app.json 기반)
    services/                    JS 서비스 모듈 29개
  apps/shared/                 공유 런타임
    bridge.js                    IPC 추상화 (requestId + Promise, 4모드)
    i18n.js                      다국어 엔진 (ko/en/ja/zh/es)
    touch-scroll.js              통합 터치 엔진
  apps/keyboard/               가상 키보드 (QWERTY/두벌식/Spanish)

emulator-app/                  Tauri 2.x 네이티브 에뮬레이터
  src/                           Rust 백엔드 (tokio, aes-gcm)
  ui/                            프론트엔드 (컴포지터, HAL)

compositor/                    Wayland 모바일 컴포지터 (C, wlroots)
runtime/wam/                   Web Application Manager (C, WebKitGTK)
runtime/hal/                   Hardware Abstraction Layer (C, 8개)
runtime/services/              시스템 서비스 26개 C 디렉토리 (D-Bus)
system/                        systemd (25개 서비스), Plymouth, DTS, AppArmor, 복구
tools/                         빌드/개발 도구
  zyl                            SDK CLI (init/validate/package/sign/keygen)
  sign-ospkg.sh                  앱 패키지 서명 스크립트
  gen-boot-keys.sh               Verified Boot RSA-2048 키 생성
  sign-image.sh                  FIT 이미지 서명 + dm-verity
tests/                         단위/통합 + verify-all.sh (11섹션 자동 검증)
docs/                          문서
CLAUDE.md                      프로젝트 규칙 (AI/개발자 공통)
Dockerfile                     Docker 재현 가능 빌드
```

## 문서

- [앱 개발 가이드](docs/APP_DEVELOPMENT_GUIDE.md)
- [아키텍처](docs/ARCHITECTURE.md)
- [배포 가이드](docs/DEPLOYMENT_GUIDE.md)
- [보안 안내](docs/SECURITY.md)
- [기여 가이드](CONTRIBUTING.md)
- [에뮬레이터](emulator-app/README.md)

## 라이선스

**GNU General Public License v3.0 (GPL-3.0)** — [LICENSE](LICENSE) 참고

Zyl OS를 수정하거나 포함하여 배포하는 프로젝트는 전체 소스 코드를 GPL-3.0으로 공개해야 합니다.

의존성 라이선스 정보: [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md)
