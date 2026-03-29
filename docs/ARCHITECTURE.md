<!--
  Zyl OS: Architecture Document
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 시스템 아키텍처

## 개요

Zyl OS는 BPI-F3 (SpacemiT K1 RISC-V) 보드를 위한 커스텀 모바일 운영체제입니다.
Linux 커널 위에 Wayland 컴포지터 + WebKitGTK 앱 런타임으로 구성됩니다.

## 시스템 스택

```
┌─────────────────────────────────────────┐
│          Web Apps (HTML/CSS/JS)          │  ← 홈, 설정, 브라우저, 터미널 등
├─────────────────────────────────────────┤
│     WebKitGTK Web Runtime (WAM)         │  ← 앱별 WebView 프로세스
├─────────────────────────────────────────┤
│   JS-Native Bridge (postMessage IPC)    │  ← 하드웨어 접근 API
├─────────────────────────────────────────┤
│      System Services (D-Bus IPC)        │  ← 27개 시스템 서비스
├─────────────────────────────────────────┤
│  Wayland Compositor (wlroots 기반)      │  ← 윈도우/터치/제스처 관리
├─────────────────────────────────────────┤
│       HAL (Hardware Abstraction)        │  ← Linux 드라이버 ↔ 서비스 연결
├─────────────────────────────────────────┤
│   Linux Kernel (SpacemiT BSP 6.6)       │  ← DRM, GPU, 터치, 센서, 모뎀
├─────────────────────────────────────────┤
│        U-Boot + OpenSBI                 │  ← RISC-V 부트로더
└─────────────────────────────────────────┘
```

## 모듈 구조

### Compositor (compositor/)
- wlroots 0.18 기반 Wayland 컴포지터
- 모바일 전용: 풀스크린 앱 스택, 제스처, 상태바
- 모듈: main, input/gesture, output, view
- D-Bus 시그널: GoHome, GoBack, NotificationPanel, AppSwitcher

### WAM - Web Application Manager (runtime/wam/)
- WebKitGTK 기반 앱 실행 환경
- 앱별 독립 WebView 프로세스
- 모듈: manifest, lifecycle (suspend/resume/close), bridge, dbus_service
- JS Bridge: webkit.messageHandlers → D-Bus → 시스템 서비스

### System Services (27개)

#### D-Bus 데몬 서비스 (실기기 — runtime/services/):

| 서비스 | D-Bus 이름 | 역할 |
|--------|-----------|------|
| notification | org.zylos.Notification | 알림 생성/조회/채널 관리 |
| power | org.zylos.PowerManager | 전력 상태, 밝기, 서스펜드 |
| display | org.zylos.DisplayManager | 해상도, 회전, DPI |
| input | org.zylos.InputService | 가상 키보드, IME, 하드웨어 키 |
| sensors | org.zylos.SensorService | 가속도계, 자이로, 근접, 조도 |
| location | org.zylos.LocationService | GPS (GPSD) + GeoIP 퓨전 |
| telephony | org.zylos.Telephony | ModemManager 통합, 통화/SMS, 통화 이력 |
| contacts | org.zylos.Contacts | 연락처 CRUD, 검색, 그룹 관리 |
| messaging | org.zylos.Messaging | SMS/MMS 수신함, 대화 스레드 관리 |
| usb | org.zylos.UsbManager | USB 가젯 모드 (MTP/PTP/ADB) |
| user | org.zylos.UserManager | 멀티유저, 프로필 전환 |
| credential | org.zylos.CredentialManager | 암호화 키체인 (프로토타입: 평문 저장) |
| accessibility | org.zylos.Accessibility | 고대비, 폰트 스케일링 |
| logger | org.zylos.Logger | JSON 구조화 로깅, 크래시 보고 |
| appstore | (라이브러리) | 패키지 검증/설치 (.ospkg) |
| updater | (라이브러리) | OTA A/B 파티션 업데이트 |
| sandbox | (라이브러리) | seccomp + namespace + cgroup |
| dbus-utils | (라이브러리) | 비동기 D-Bus 유틸리티 |

#### OS 이미지 서비스 (apps/system/services.js — 27개 비즈니스 로직):

서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js`)에 소유되며,
에뮬레이터는 순수 IPC 라우터로서 앱↔서비스 간 메시지만 전달합니다.

| 서비스 | 역할 |
|--------|------|
| fs | 마운트 디스크 이미지 파일 I/O |
| device | 디바이스 메타데이터 |
| storage | 디스크 사용량 조회 |
| apps | 설치된 앱 목록/관리 |
| settings | 영속 설정 JSON (마운트 포인트) |
| terminal | 셸 명령 실행 (22패턴 필터링) |
| wifi | 호스트 WiFi 스캔/연결 |
| bluetooth | 호스트 BT 디바이스 조회 |
| audio | 볼륨 키, OSD, 알림 사운드, 진동 |
| contacts | 연락처 저장/검색/그룹 |
| messaging | SMS/MMS 대화 스레드 |
| + 18개 D-Bus 서비스 | 위 테이블의 모든 서비스 포함 |

#### 권한 시스템 (apps/system/permissions.js):

앱의 서비스 호출은 `app.json`에 선언된 권한과 대조하여 **실행 시점에 차단**됩니다.
`ZylPermissions`가 서비스 요청마다 권한을 검증합니다.

#### 보안 관리 (apps/system/security.js):

파일시스템 보호, 자격증명 격리 등 OS 레벨 보안 정책을 관리합니다.

### HAL - Hardware Abstraction Layer (runtime/hal/)
- C 인터페이스 (hal.h): WiFi, BT, Display, Audio, Battery, Storage
- 실기기 구현: wpa_supplicant, BlueZ, PipeWire, sysfs
- 에뮬레이터 구현: 브라우저 Web API (hal-browser.js)

### Apps (apps/ — 19개 + 시스템 2개)
- 모든 앱은 HTML/CSS/JS (WebKitGTK에서 실행)
- 시스템 앱: home, lockscreen, statusbar, oobe, settings, browser, files, terminal, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts
- **ZylAppRegistry**: 앱 메타데이터의 단일 진실 소스 — 각 앱의 `app.json`(iconSvg 필드 포함)을 기반으로 동적 앱 로딩
- **앱 아이콘**: 각 앱의 `app.json`에 `iconSvg` 필드로 인라인 SVG 아이콘 정의 (중앙 아이콘 레지스트리 없음)
- **OS 시스템 서비스**: `apps/system/` — services.js(27개 서비스 비즈니스 로직), permissions.js(권한 시행), security.js(보안 정책)
- **가상 키보드**: `apps/keyboard/` — OS 이미지 내 가상 키보드 시스템 앱, 에뮬레이터 컴포지터가 마운트하고 postMessage로 키 입력 전달
- **OS 런타임 레이어**: `apps/shared/` — touch-scroll.js(터치 스크롤), runtime.css(런타임 스타일), bridge.js(IPC 브릿지)
- 공유 모듈: i18n, clock, gesture, bridge (apps/shared/)
- OS 이미지의 services.js를 통해 시스템 데이터 수신
- postMessage IPC로 서비스와 통신

### i18n 아키텍처
- **공유 엔진**: `apps/shared/i18n.js` — 번역, DOM 자동 번역, 로케일 감지
- **앱별 데이터**: 각 앱이 `addTranslations(locale, keys)`로 자체 번역 키 등록
- **에뮬레이터 i18n**: `emulator-app/ui/js/emu-i18n.js` — 컴포지터 UI (QS 패널, 알림, 최근 앱) 번역
- **지원 언어**: ko, en, ja, zh, es (5개)
- **흐름**: shared/i18n.js 로드 → 앱 i18n.js에서 addTranslations() → DOM data-i18n 자동 적용

### Emulator — IPC 라우터 + 컴포지터 (emulator-app/)
- Tauri 2.x (Rust + WebView) 기반 독립 실행파일
- **역할 변경**: 서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js`)로 이동, 에뮬레이터는 순수 IPC 라우터 + 컴포지터
- 프리부팅 설정: 디바이스 프로필, OS 이미지, 저장공간/RAM 선택
- 실제 리소스 예약: sparse 디스크 이미지 마운트, cgroup/rlimit 메모리 제한
- Rust 백엔드: 파일시스템 보호 (settings.json, .credentials/, .system/ 접근 차단), 리소스 관리
- 가상 키보드: OS 이미지의 `apps/keyboard/`를 마운트하여 컴포지터에서 표시, postMessage로 키 전달
- OOBE 격리: 최근 앱에서 제외, 네비게이션 차단, 전원 토글 시 잠금 없음
- 에뮬레이터 전용 i18n: emu-i18n.js (QS 패널, 알림 패널 번역)
- 배포: macOS (.app/.dmg), Linux (.deb/.AppImage)

## 데이터 흐름

### 실기기
```
앱 (JS) → ZylBridge.notify()
  → webkit.messageHandlers
  → WAM Bridge Dispatch (핸들러 레지스트리)
  → D-Bus 메서드 호출
  → 시스템 서비스 처리
  → D-Bus 시그널 응답
  → WAM → JS 콜백
```

### 에뮬레이터
```
앱 (JS) → postMessage IPC
  → 에뮬레이터 IPC 라우터 (메시지 전달만)
  → OS 서비스 (apps/system/services.js — 비즈니스 로직)
  → ZylPermissions 권한 검증 (apps/system/permissions.js)
  → Rust 백엔드 (파일시스템 보호, 리소스 관리)
  → postMessage 응답 → 앱 콜백
```

## 보안 모델

### 6계층 보안
1. **파일시스템**: mount namespace + bind mount
2. **시스콜**: seccomp-bpf (Strict/Default/Permissive)
3. **네트워크**: network namespace (권한 없으면 차단)
4. **리소스**: cgroup v2 (메모리/CPU/PID 제한)
5. **IPC**: D-Bus 정책 XML (권한별 서비스 접근)
6. **앱 권한 시행**: ZylPermissions — 서비스 호출 시 app.json 권한 실시간 검증, 미선언 권한은 차단

### 파일시스템 보호 (Rust 백엔드)
- `settings.json`: fs 서비스에서 직접 접근 차단 (settings 서비스만 허용)
- `.credentials/`: 자격증명 디렉토리 격리
- `.system/`: 시스템 설정 디렉토리 격리
- Rust 레벨에서 경로 필터링 후 차단

### 앱 서명
- RSA-2048 + SHA-256 패키지 서명
- 개발자 인증서 신뢰 저장소
- 서명 없는 앱 설치 차단 (개발자 모드 예외)

## 부팅 순서

```
U-Boot → OpenSBI → Linux Kernel → systemd
  → Plymouth 스플래시
  → zyl-compositor.service (Wayland)
  → zyl-wam.service (앱 매니저)
  → zyl-notification.service
  → zyl-power.service
  → zyl-sensors.service
  → 잠금화면 표시
  → PIN 입력 → 홈 화면
```

## OTA 업데이트

- A/B 파티션 기반 원자적 업데이트
- 비활성 파티션에 적용 → 부트로더 플래그 전환
- 부팅 3회 실패 시 자동 롤백
- 업데이트 유형: FULL, DELTA, APPS_ONLY, KERNEL
