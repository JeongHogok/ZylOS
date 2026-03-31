<!--
  Zyl OS: Architecture Document
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 시스템 아키텍처

## 개요

Zyl OS는 BPI-F3 (SpacemiT K1 RISC-V) 보드를 위한 커스텀 모바일 운영체제입니다.
Linux 커널 위에 Wayland 컴포지터 + WebKitGTK 앱 런타임으로 구성됩니다.
394개 파일, ~50,200 LoC 규모의 코드베이스입니다.

## 시스템 스택

```
┌─────────────────────────────────────────────────────┐
│          Web Apps (HTML/CSS/JS, ES5)                 │  ← 홈, 설정, 브라우저, 터미널 등 20개
├─────────────────────────────────────────────────────┤
│  OS 프레임워크 (apps/system/)                         │  ← Intent, ContentProvider, PermissionDialog
│  Intent / ContentProvider / ZylPermissions          │
├─────────────────────────────────────────────────────┤
│     WebKitGTK Web Runtime (WAM)                     │  ← 앱별 WebView 프로세스
├─────────────────────────────────────────────────────┤
│   JS-Native Bridge (postMessage IPC + requestId)    │  ← Promise 매칭, 4모드 자동 감지
├─────────────────────────────────────────────────────┤
│  JS 서비스 모듈 (apps/system/services/ — 29개)        │  ← 비즈니스 로직 (OS 이미지 소유)
├─────────────────────────────────────────────────────┤
│      C 서비스 (D-Bus IPC — 26개 디렉토리)             │  ← ~22개 D-Bus 데몬
├─────────────────────────────────────────────────────┤
│  Wayland Compositor (wlroots 기반)                   │  ← 윈도우/터치/제스처 관리
├─────────────────────────────────────────────────────┤
│       HAL (Hardware Abstraction — 7개 구현체)         │  ← Linux 드라이버 ↔ 서비스 연결
├─────────────────────────────────────────────────────┤
│   Linux Kernel (SpacemiT BSP 6.6)                   │  ← DRM, GPU, 터치, 센서, 모뎀
├─────────────────────────────────────────────────────┤
│        U-Boot + OpenSBI + Verified Boot             │  ← RISC-V 부트로더 + 부트체인 검증
└─────────────────────────────────────────────────────┘
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

### C 시스템 서비스 (26개 — runtime/services/)

#### D-Bus 데몬 서비스 (~22개):

| 서비스 | D-Bus 이름 | 역할 |
|--------|-----------|------|
| accessibility | org.zylos.Accessibility | 고대비, 폰트 스케일링 |
| account | org.zylos.AccountManager | 계정 관리 |
| alarm | org.zylos.AlarmManager | 알람 스케줄링 |
| audio | org.zylos.AudioService | PipeWire 오디오, 5개 볼륨 카테고리 |
| auth | org.zylos.AuthService | 생체/PIN 인증 |
| bluetooth | org.zylos.BluetoothManager | BlueZ 연동, BT 스캔/연결 |
| camera | org.zylos.CameraService | V4L2 카메라 제어 |
| crash | org.zylos.CrashHandler | 크래시 덤프 수집/보고 |
| credential | org.zylos.CredentialManager | AES-256-GCM + PBKDF2 키체인 |
| display | org.zylos.DisplayManager | 해상도, 회전, DPI |
| input | org.zylos.InputService | 가상 키보드, IME, 하드웨어 키 |
| location | org.zylos.LocationService | GPS (GPSD) + GeoIP 퓨전 |
| logger | org.zylos.Logger | JSON 구조화 로깅 |
| nfc | org.zylos.NFCManager | NFC 태그 읽기/쓰기 |
| notification | org.zylos.Notification | 알림 생성/조회/채널 관리 |
| power | org.zylos.PowerManager | Doze 상태머신, CPU 거버너, 웨이크락 |
| sensors | org.zylos.SensorService | 가속도계, 자이로, 근접, 조도 |
| telemetry | org.zylos.TelemetryService | 익명 사용 통계 |
| telephony | org.zylos.Telephony | ModemManager 통합, 통화/SMS |
| usb | org.zylos.UsbManager | USB 가젯 모드 (MTP/PTP/ADB) |
| user | org.zylos.UserManager | 멀티유저, 앱별 UID |
| wifi | org.zylos.WiFiManager | wpa_supplicant 연동, WiFi 스캔/연결 |

#### 라이브러리 서비스 (4개 — D-Bus 데몬 없음):

| 서비스 | 역할 |
|--------|------|
| appstore | RSA-2048+SHA-256 패키지 검증/설치 (libzip) |
| dbus | 비동기 D-Bus 유틸리티 |
| sandbox | namespace + libseccomp BPF + cgroup v2 |
| updater | A/B OTA, RSA-2048 서명 검증 |

### JS 서비스 모듈 (29개 — apps/system/services/)

서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js` 라우터 + `services/*.js` 모듈)에 소유되며,
에뮬레이터는 순수 IPC 라우터로서 앱↔서비스 간 메시지만 전달합니다.

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

### OS 프레임워크 (apps/system/)

#### 권한 시스템 (permissions.js)
`ZylPermissions`가 모든 서비스 요청을 `app.json` 권한과 대조하여 **실행 시점에 차단**합니다.
미선언 권한은 즉시 에러 응답.

#### 보안 관리 (security.js)
파일시스템 보호, 자격증명 격리 등 OS 레벨 보안 정책을 관리합니다.

#### Intent 시스템 (intent.js)
Android 스타일 인텐트 — 명시적(targetApp 지정) / 암시적(action 매칭) 앱 간 통신.
표준 액션: VIEW, SEND, PICK, EDIT, DIAL, CAPTURE, SEARCH.
앱은 `ZylIntent.registerFilter(appId, action, mimeType)`로 인텐트 필터를 등록하고,
`ZylIntent.startActivity(intent)`로 다른 앱을 기동합니다.

#### ContentProvider (content-provider.js)
URI 기반 앱 간 데이터 공유 게이트웨이.
형식: `content://authority/path?query`
앱은 `ZylContentProvider.registerProvider(authority, impl)`로 데이터를 노출하고,
`ZylContentProvider.query(callerAppId, uri)`로 다른 앱 데이터에 접근합니다.
쿼리 시 `ZylPermissions`를 통한 권한 체크가 선행됩니다.

#### PermissionDialog (permission-dialog.js)
앱이 위험 권한(camera, location, contacts, messaging, telephony, storage, microphone, bluetooth)을
요청할 때 OS 수준 다이얼로그를 표시합니다.
`ZylPermissionDialog.requestPermission(appId, permission)` → Promise\<boolean\>.

#### 앱 레지스트리 (app-registry.js)
`app.json` 메타데이터 기반 동적 앱 로딩. 홈/런처의 단일 진실 소스.
`iconSvg` 필드로 인라인 SVG 아이콘 정의.

#### Zygote 패턴
신규 앱 WebView 프로세스는 기본 상태로 초기화된 Zygote 프로세스에서 fork하여
앱 기동 시간을 단축합니다.

### 서비스 코드패스 설계 (3중 환경)

일부 서비스(notification, credential 등)는 세 개의 코드패스가 존재합니다:

| 환경 | 코드패스 | 설명 |
|------|----------|------|
| **실기기** | `runtime/services/*.c` → D-Bus | 네이티브 C 서비스 |
| **에뮬레이터** | `emulator-app/src/commands/*.rs` → Tauri IPC | Rust 백엔드 |
| **브라우저** | `apps/system/services.js` → Web API fallback | 독립 실행 |

이것은 의도적 설계입니다:
- **실기기**는 D-Bus 데몬 서비스만 사용 (C 구현이 유일한 권위)
- **에뮬레이터**는 Rust 백엔드가 호스트 OS 리소스를 직접 관리 (파일시스템, 네트워크 등)
- **브라우저 모드**는 Web API로 가능한 범위까지만 폴백 (테스트/데모용)

`apps/system/services.js`의 `_invoke()` 추상화가 런타임에 올바른 코드패스를 선택합니다.

### HAL - Hardware Abstraction Layer (runtime/hal/)

7개 구현체:

| 파일 | 인터페이스 | 구현 |
|------|-----------|------|
| hal_wifi.c | WiFi 스캔/연결 | wpa_supplicant (실기기) |
| hal_bt.c | BT 스캔/페어링 | BlueZ (실기기) |
| hal_display.c | 해상도/밝기/회전 | DRM/KMS sysfs (실기기) |
| hal_audio.c | 볼륨/믹서 | PipeWire (실기기) |
| hal_battery.c | 배터리 상태/충전 | sysfs (실기기) |
| hal_storage.c | 블록 디바이스 I/O | sysfs (실기기) |
| hal_cpu.c | CPU 거버너/코어 온오프 | sysfs cpufreq (실기기) |

에뮬레이터 구현: 브라우저 Web API (hal-browser.js)

### Apps (apps/ — 20개 시스템 앱)
- 모든 앱은 HTML/CSS/JS (ES5, WebKitGTK에서 실행)
- 시스템 앱: home, lockscreen, statusbar, oobe, settings, browser, files, terminal, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts, keyboard

### i18n 아키텍처
- **공유 엔진**: `apps/shared/i18n.js` — 번역, DOM 자동 번역, 로케일 감지
- **앱별 데이터**: 각 앱이 `addTranslations(locale, keys)`로 자체 번역 키 등록
- **에뮬레이터 i18n**: `emulator-app/ui/js/emu-i18n.js` — 컴포지터 UI 번역
- **지원 언어**: ko, en, ja, zh, es (5개)

### Emulator — IPC 라우터 + 컴포지터 (emulator-app/)
- Tauri 2.x (Rust + WebView) 기반 독립 실행파일
- **역할**: 서비스 비즈니스 로직은 OS 이미지 소유, 에뮬레이터는 순수 IPC 라우터 + 컴포지터
- Rust 백엔드: 파일시스템 보호 (settings.json, .credentials/, .system/ 접근 차단)
- 크리덴셜 암호화: aes-gcm + pbkdf2 Rust crate
- 배포: macOS (.app/.dmg), Linux (.deb/.AppImage)

## 데이터 흐름

### IPC 흐름 (requestId + Promise 매칭)
```
앱 (JS) → ZylBridge.requestService(service, method, params)
  → UUID requestId 생성
  → postMessage { type: 'service.request', requestId, service, method, params }
  → [IPC 라우터가 서비스로 전달]
  → 서비스 처리 완료
  → postMessage { type: 'service.response', requestId, result }
  → _pendingRequests[requestId].resolve(result)
  → 앱 Promise 해결
```

### 실기기 (D-Bus 경로)
```
앱 (JS) → ZylBridge.notify()
  → webkit.messageHandlers
  → WAM Bridge Dispatch (핸들러 레지스트리)
  → D-Bus 메서드 호출
  → C 서비스 처리 (HAL 경유)
  → D-Bus 시그널 응답
  → WAM → JS 콜백
```

### 에뮬레이터 (Tauri 경로)
```
앱 (JS) → postMessage IPC
  → 에뮬레이터 IPC 라우터 (메시지 전달만)
  → OS 서비스 (apps/system/services.js — 비즈니스 로직)
  → ZylPermissions 권한 검증 (apps/system/permissions.js)
  → Rust 백엔드 (파일시스템 보호, 리소스 관리)
  → postMessage { type: 'service.response', requestId, result }
  → 앱 Promise 해결
```

### Intent 흐름
```
앱 A → ZylIntent.startActivity({ action: 'zyl.intent.action.VIEW', mimeType: 'image/*' })
  → ZylIntent.resolve() — 인텐트 필터 레지스트리 검색
  → 매칭된 앱 목록 반환
  → 단일 매칭: 즉시 기동 / 복수 매칭: 앱 선택 다이얼로그
  → ZylAppRegistry.launch(resolvedAppId, extras)
  → 앱 B 기동 (extras 데이터 전달)
```

### ContentProvider 흐름
```
앱 A → ZylContentProvider.query('com.zylos.contacts', 'content://com.zylos.contacts/all')
  → ZylPermissions.check(appA.id, 'contacts') → 권한 없으면 차단
  → _providers['com.zylos.contacts'].query(path, projection)
  → 연락처 앱이 등록한 프로바이더 실행
  → Promise<rows>
```

### 런타임 퍼미션 흐름
```
앱 → 위험 권한 서비스 호출
  → ZylPermissions 실행 시점 검증
  → 권한 미부여 → ZylPermissionDialog.requestPermission(appId, permission)
  → OS 수준 다이얼로그 표시 (사용자 승인/거부)
  → 승인 → ZylPermissions 상태 업데이트 → 서비스 요청 처리
  → 거부 → 에러 응답
```

## 보안 모델

### 7계층 보안
1. **Verified Boot**: FIT 이미지 서명 (RSA-2048) + dm-verity (rootfs 무결성)
2. **파일시스템**: mount namespace + bind mount
3. **시스콜**: libseccomp BPF (3프로필 — STRICT/DEFAULT/PERMISSIVE, 총 46개 규칙)
4. **네트워크**: network namespace (권한 없으면 차단)
5. **리소스**: cgroup v2 (메모리/CPU/PID 제한)
6. **IPC**: D-Bus 정책 XML (권한별 서비스 접근)
7. **앱 권한 시행**: ZylPermissions + ZylPermissionDialog — 서비스 호출 시 권한 실시간 검증

### 앱 격리 (seccomp 프로필)
| 프로필 | 대상 | 차단 규칙 수 |
|--------|------|------------|
| STRICT | 서드파티 앱 | DEFAULT + 추가 규칙 (총 최다) |
| DEFAULT | 일반 시스템 앱 | 위험 시스콜 차단 (기본 집합) |
| PERMISSIVE | 권한 있는 시스템 앱 | 최소 규칙만 적용 |

### 앱별 UID
각 앱은 독립된 Linux UID로 실행됩니다.
`user` 서비스의 `zyl_user_get_app_uid(appId)`로 앱별 UID 조회.
앱 간 파일 접근은 UID 기반으로 커널에서 차단됩니다.

### 크리덴셜 보안
- 저장 포맷: `[salt(16B)][iv(12B)][ciphertext(N)][tag(16B)]`
- 암호화: AES-256-GCM (OpenSSL EVP API)
- 키 파생: PBKDF2-HMAC-SHA256, 100,000 iterations

### Verified Boot
- 부트 키 생성: `tools/gen-boot-keys.sh` (RSA-2048)
- FIT 이미지 서명 + dm-verity: `tools/sign-image.sh`
- dm-verity: rootfs 블록 단위 해시 트리, 변조 감지 시 패닉 또는 읽기 전용 마운트

### 앱 서명
- RSA-2048 + SHA-256 패키지 서명 (OpenSSL EVP API)
- 개발자 인증서 신뢰 저장소
- 서명 없는 앱 설치 차단 (개발자 모드 예외)
- ZIP 매직 바이트 검증 + 경로 순회 공격 탐지

### 파일시스템 보호 (Rust 백엔드)
- `settings.json`: settings 서비스에서만 접근 허용 (fs 서비스 직접 접근 차단)
- `.credentials/`: credential 서비스를 통해서만 접근
- `.system/`: 시스템 설정 디렉토리 — 직접 접근 차단

## 전력 관리

### Doze 상태머신
```
ACTIVE → [화면 OFF 5분] → DOZE → [추가 대기] → SUSPEND
  ↑                                    ↓
  └──────── [웨이크락 획득 / 인터럽트] ──┘
```
- Doze 진입 후 CPU 거버너를 powersave로 전환
- 웨이크락 최대 보유 시간: 600초 (자동 release 타이머)

### CPU 거버너 (hal_cpu.c)
- 상태별 거버너: ACTIVE → performance/schedutil, DOZE/SUSPEND → powersave
- 코어 온오프: `zyl_cpu_set_core_online(core, online)` via sysfs

### LMK (Low Memory Killer)
- cgroup v2 memory.pressure 이벤트 기반
- 백그라운드 앱 → 캐시 앱 → 사용자 앱 순으로 OOM 처리

## 부팅 순서

```
U-Boot
  → OpenSBI
  → Verified Boot 검증 (FIT 서명 + dm-verity)
  → Linux Kernel 6.6
  → systemd (24개 서비스 유닛)
    → Plymouth 부트 스플래시
    → zyl-compositor.service (Wayland)
    → zyl-wam.service (WAM)
    → zyl-notification.service
    → zyl-power.service
    → zyl-credential.service
    → zyl-auth.service
    → zyl-wifi.service
    → ... (나머지 서비스)
    → 잠금화면 표시
    → PIN 입력 → 홈 화면
```

systemd 서비스 유닛: 24개 (system/*.service)

## OTA 업데이트

- A/B 파티션 기반 원자적 업데이트
- 비활성 파티션에 적용 → 부트로더 플래그 전환
- **RSA-2048+SHA-256 서명 검증** (OpenSSL EVP API) — 중간자 공격 방지
- SHA-256 해시 무결성 검증
- 부팅 3회 실패 시 자동 롤백
- 업데이트 유형: FULL, DELTA, APPS_ONLY, KERNEL
