<!--
  Zyl OS: Architecture Document
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 시스템 아키텍처

## 개요

Zyl OS는 BPI-F3 (SpacemiT K1 RISC-V) 보드를 위한 커스텀 모바일 운영체제입니다.
Linux 커널 위에 Wayland 컴포지터 + WebKitGTK 앱 런타임으로 구성됩니다.
375개 파일, ~66,300 LoC 규모의 코드베이스입니다.

## 시스템 스택

```
┌─────────────────────────────────────────────────────┐
│          Web Apps (HTML/CSS/JS, ES5)                 │  ← 홈, 설정, 브라우저, 전화 등 22개
├─────────────────────────────────────────────────────┤
│  OS 프레임워크 (apps/system/)                         │  ← 인텐트 해석, 권한 판정, 앱 레지스트리
│  Intent / ContentProvider / ZylPermissions          │
├─────────────────────────────────────────────────────┤
│     WebKitGTK Web Runtime (WAM)                     │  ← 앱별 WebView 프로세스
├─────────────────────────────────────────────────────┤
│   JS-Native Bridge (postMessage IPC + requestId)    │  ← Promise 매칭, 4모드 자동 감지
├─────────────────────────────────────────────────────┤
│  JS 서비스 모듈 (apps/system/services/ — 33개)        │  ← 비즈니스 로직 (OS 이미지 소유)
├─────────────────────────────────────────────────────┤
│      C 서비스 (D-Bus IPC — 32개 디렉토리)             │  ← 32개 D-Bus 데몬
├─────────────────────────────────────────────────────┤
│  Wayland Compositor (wlroots 기반)                   │  ← 윈도우/터치/제스처 관리
├─────────────────────────────────────────────────────┤
│       HAL (Hardware Abstraction — 15개)               │  ← Linux 드라이버 ↔ 서비스 연결
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
- 모듈: main, input/gesture, ipc/os_bus, output, view
- 제스처 → WAM D-Bus 메서드 GoHome·GoBack·SwitchApp (무엇이 앞에 올지는 WAM 의 셸 정책 —
  컴포지터는 창을 직접 올리지 않는다)
- 터치·클릭 → 전원 서비스 UserActivity (쓰는 동안 화면이 어두워지거나 꺼지지 않게, 초당 1회)

### WAM - Web Application Manager (runtime/wam/)
- WebKitGTK 기반 앱 실행 환경
- 앱별 독립 WebView 프로세스
- 모듈: manifest, lifecycle (suspend/resume/close), bridge, dbus_service, intent, shell
- 셸 정책(shell): 부팅 시 OOBE/잠금 화면, 화면 꺼짐 잠금, 홈·뒤로·앱 전환, 잠금 해제는
  잠금 화면만 — 에뮬레이터의 `apps/system/shell.js` 와 같은 규칙
- JS Bridge: webkit.messageHandlers → D-Bus → 시스템 서비스

### C 시스템 서비스 (32개 — runtime/services/, 주요 서비스)

서비스는 **시스템 버스**에 이름을 올립니다. 유닛이 `Type=dbus` + `BusName=` 이므로
systemd 가 기다리는 버스가 시스템 버스이고, 로그인 세션이 없는 기기 부팅에서 세션 버스를
띄워 줄 주체도 없기 때문입니다. 누가 이름을 갖고 누가 부를 수 있는지는
`system/dbus-1/zyl-os.conf` 한 곳이 정합니다 (기본 거부 — 서비스 데몬과 WAM 만 허용).
`tests/test_dbus_policy.sh` 가 그 정책을 올린 실제 버스에서 소유·호출·거부를 검증합니다.

#### D-Bus 데몬 서비스:

| 서비스 | D-Bus 이름 | 역할 |
|--------|-----------|------|
| accessibility | org.zylos.Accessibility | 고대비, 폰트 스케일링 |
| alarm | org.zylos.AlarmService | 알람 스케줄링 |
| audio | org.zylos.AudioService | PipeWire 오디오, 5개 볼륨 카테고리 |
| bluetooth | org.zylos.BluetoothService | BlueZ 연동, BT 스캔/연결 |
| camera | org.zylos.CameraService | V4L2 카메라 제어 |
| credential | org.zylos.CredentialManager | AES-256-GCM + PBKDF2 키체인 |
| display | org.zylos.DisplayManager | 해상도, 회전, DPI |
| input | org.zylos.InputService | 가상 키보드, IME, 하드웨어 키 |
| location | org.zylos.LocationService | GPS (GPSD) + GeoIP 퓨전 |
| logger | org.zylos.Logger | JSON 구조화 로깅 |
| notification | org.zylos.Notification | 알림 생성/조회/채널 관리 |
| power | org.zylos.PowerManager | Doze 상태머신, CPU 거버너, 웨이크락 |
| sensors | org.zylos.SensorService | 가속도계, 자이로, 근접, 조도 |
| telemetry | org.zylos.Telemetry | 익명 사용 통계 |
| telephony | org.zylos.Telephony | ModemManager 통합, 통화/SMS |
| usb | org.zylos.UsbManager | USB 가젯 모드 (MTP/PTP/ADB) |
| user | org.zylos.UserManager | 멀티유저, 앱별 UID |
| wifi | org.zylos.WifiService | wpa_supplicant 연동, WiFi 스캔/연결 |

appstore(`org.zylos.AppStore`)와 updater(`org.zylos.Updater`)는 D-Bus 데몬이다.

### JS 서비스 모듈 (33개 — apps/system/services/)

서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js` 라우터 + `services/*.js` 모듈)에 소유되며,
에뮬레이터는 순수 IPC 라우터로서 앱↔서비스 간 메시지만 전달합니다.

| 서비스 | 역할 |
|--------|------|
| accessibility | 고대비, 폰트 스케일 |
| apps | 설치된 앱 목록/관리 |
| appstore | 서명된 카탈로그, .zpkg 봉인 검증/설치·삭제, 개발 빌드(프로비저닝) |
| audio | 5개 볼륨 카테고리, 키클릭, 알림음, 진동 |
| bluetooth | BT 디바이스 조회 (비동기) |
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

15개:

| 파일 | 인터페이스 | 구현 |
|------|-----------|------|
| hal_wifi.c | WiFi 스캔/연결 | wpa_supplicant (실기기) |
| hal_bt.c | BT 스캔/페어링 | BlueZ (실기기) |
| hal_display.c | 해상도/밝기/회전 | DRM/KMS sysfs (실기기) |
| hal_audio.c | 볼륨/믹서 | PipeWire (실기기) |
| hal_battery.c | 배터리 상태/충전 | sysfs (실기기) |
| hal_storage.c | 블록 디바이스 I/O | sysfs (실기기) |
| hal_cpu.c | CPU 거버너/코어 온오프 | sysfs cpufreq (실기기) |
| hal_board.c | 보드 정보 (모델·SoC·메모리·기기 ID) | device tree, /proc (실기기) |
| hal_boot.c | A/B 슬롯·부트로더 잠금 상태 | U-Boot 환경 (실기기) |
| hal_torch.c | 손전등 | LED sysfs (실기기) |
| hal_vibrator.c | 진동 | force-feedback / sysfs (실기기) |
| hal_sensors.c | 가속도/자이로/근접/조도/자기장 | IIO sysfs (실기기) |
| hal_input.c | 하드웨어 키 | evdev — 키를 가진 장치 전부 (실기기) |
| hal_camera.c | 사진 한 장 | V4L2 (실기기). 보드가 `board_hal_camera` 로 교체 가능 |
| hal_emulator.c | 에뮬레이터 보드 | 하드웨어 없음 — 없다고 보고 |

에뮬레이터 구현: 브라우저 Web API (hal-browser.js)

### 폼팩터 아키텍처 (board × profile)

Zyl OS는 하나의 OS에 N개 폼팩터 프로파일을 둔다. 폼팩터마다 별도 OS를 두면
유지보수가 N배가 되고 `verify-all.sh`가 지켜주는 일관성이 깨진다.

**빌드 타임 — 무엇을 담을지**

```bash
meson setup builddir -Dboard=bpi-f3 -Dprofile=tv
```

| 축 | 소유 | 역할 |
|---|---|---|
| `-Dboard` | `board/<id>/meson.build` | SoC, arch, HAL 백엔드, DTS, 크로스 파일, **지원 폼팩터 선언** |
| `-Dprofile` | `meson.build` 의 `zyl_profile_extras` | 빌드/설치할 C 서비스 집합 |

보드가 선언하지 않은 조합은 configure 단계에서 실패한다
(`generic-x86_64 × phone` → 모뎀/PMIC 없음, `bpi-f3 × watch` → 전력 예산 불가).

보드 3종: `bpi-f3`(실기기), `generic-x86_64`(이식성 기준선),
`emulator`(에뮬레이션·CI 전용, 물리 제약이 없어 8개 폼팩터 전부 지원).
실기기 하드웨어가 정해지기 전에도 모든 폼팩터를 빌드·검증할 수 있는 경로이며,
CI 가 매 빌드마다 8개 프로파일의 configure 를 확인한다.
`system/zyl-os.target` 은 `.target.in` 에서 생성되므로 `Wants=` 가 실제 빌드된
유닛 집합과 어긋날 수 없다.

프로파일별 서비스 수: phone 26 / auto 24 / pda 24 / tablet 24 / desktop 23 /
watch 22 / tv 21 / appliance 16.

**런타임 — 어떻게 보일지**

| 파일 | 역할 |
|---|---|
| `apps/shared/form-factor.js` | 8개 프로파일 테이블(navModel/inputModel/fontScale/touchTarget), 기기 선언(WAM 이 빌드 프로파일을 `window.__zylFormFactor` 로) 또는 하드웨어 감지, `<html>` 에 `.ff-<id>` + `--ff-*` 적용 — 사용자 설정이 아님 |
| `apps/shared/focus-nav.js` | 방향 포커스 내비게이션 — 포인터 없는 기기(리모컨 D-pad, 로터리, 크라운)용 공간 탐색 |
| `apps/shared/responsive.css` | 폼팩터 변수, 포커스 링, 상태바/선택 정책 |
| `app.json` 의 `formFactors` | 앱이 지원하는 기기 종류. `app-registry.isHiddenFromGrid` 가 걸러낸다 |

두 모듈은 로드 시 자가 적용되므로 앱은 opt-in 코드가 필요 없다.

**폼팩터 결정 우선순위**

1. 사용자/프로비저닝 오버라이드 (설정 > 기기 유형) — 커스텀 단말·PDA·TV 등
2. 기하 + 입력 능력 감지 — phone/tablet/tv/desktop/watch 자동 판별

Zyl OS는 디바이스 픽셀을 레이아웃 픽셀로 1:1 매핑하므로 크기로는 폰과 태블릿을
구분할 수 없다. 종횡비가 유일한 신호다(폰 16:9 이상, 태블릿 16:10 이하).
`auto`/`appliance`/`pda` 는 기하적 서명이 없어 오버라이드가 필요하다.

**루트 스케일링 주의**

앱 CSS는 px로 작성되어 있다(1,776개 px 값, rem 0개). 폼팩터 적응은 루트 `zoom`
으로 처리하는데, legacy `zoom` 속성은 표준 캐스케이드 밖에서 파싱되어 `var()`
치환을 **무시한다**. 따라서 `ZylFormFactor.apply()` 가 인라인으로 적용하며
CSS에 `zoom: var(...)` 를 쓰면 조용히 1이 된다. `zoom` 은 vw/vh 를 스케일하지
않으므로(앱 CSS에 27개) 해당 값은 폼팩터 변수로 바꿔야 한다.

### Apps (apps/ — 22개 시스템 앱)
- 모든 앱은 HTML/CSS/JS (ES5, WebKitGTK에서 실행)
- 시스템 앱: home, lockscreen, statusbar, oobe, settings, browser, files, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts, keyboard, shared

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

### 권한 판정 흐름
```
앱 → 서비스 호출 (ZylBridge)
  → OS 가 판정: 시스템 앱이면 통과, 아니면 app.json 선언 + 사용자 설정
     (에뮬레이터 ZylPermissions.checkPermission, 기기 bridge.c app_may_call)
  → 설정에서 끈 권한이면 거부 (settings 서비스의 app_permissions —
     기기에서는 WAM 이 그 변경을 따라간다)
  → 통과 → 서비스 요청 처리 / 거부 → 에러 응답
```

첫 사용 시 사용자에게 묻는 동의 창(iOS·안드로이드 식)은 **없습니다**. 권한은 설치
시점에 `app.json` 선언으로 정해지고, 사용자는 설정에서 끌 수 있습니다.

## 보안 모델

### 6계층 보안
1. **Verified Boot**: FIT 이미지 서명 (RSA-2048) + dm-verity (rootfs 무결성)
2. **앱 프로세스**: WebKitGTK 샌드박스 — bubblewrap 네임스페이스 + seccomp
   (webkitgtk-6.0 은 상시 ON, 4.1 폴백은 WAM 이 켠다)
3. **데몬**: systemd 하드닝 + AppArmor 프로필 (`system/apparmor/zyl-*`)
4. **앱 컨테이너**: `/data/appdata/<id>/` — 파일·웹 저장소·출처가 앱마다 분리
5. **IPC**: D-Bus 버스 정책 (`system/dbus-1/zyl-os.conf` — org.zylos.* 소유·호출은 서비스와 WAM 만)
6. **앱 권한 시행**: 서비스 호출마다 OS 가 판정 (ZylPermissions / bridge.c), 사용자가 설정에서 끈 권한은 거부

### 앱 격리
앱 코드는 WAM 이 띄우는 WebKitGTK 웹 프로세스에서 돌고, 그 프로세스는 WebKit 이 직접
격리합니다 (bubblewrap + seccomp + xdg-dbus-proxy). 앱별 Linux UID 는 쓰지 않습니다 —
WAM 한 프로세스가 모든 웹뷰를 띄우므로 UID 로는 앱이 갈라지지 않고, 갈라야 하는 지점은
따로 있습니다: 저장소는 `zyl-app://<id>/` 출처와 앱별 WebKit 세션, 파일은 컨테이너,
서비스 호출은 브릿지가 채운 `caller` 기준 OS 권한 검사.
자세한 내용은 [SECURITY.md](SECURITY.md) 참조.

### 크리덴셜 보안
- 저장 포맷: `[salt(16B)][iv(12B)][ciphertext(N)][tag(16B)]`
- 암호화: AES-256-GCM (OpenSSL EVP API)
- 키 파생: PBKDF2-HMAC-SHA256, 100,000 iterations

### Verified Boot
- 부트로더: `tools/build-bootloader.sh` — FSBL(루트 키) → OpenSBI·U-Boot FIT → OS FIT,
  부트 정책 내장, eMMC 부트 영역 쓰기 보호, eFuse 굽기는 빌드 선택 (docs/SECURITY.md)
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
  → systemd (35개 서비스 유닛)
    → Plymouth 부트 스플래시
    → zyl-compositor.service (Wayland)
    → zyl-wam.service (WAM)
    → zyl-notification.service
    → zyl-power.service
    → zyl-credential.service
    → zyl-wifi.service
    → ... (나머지 서비스)
    → 잠금화면 표시
    → PIN 입력 → 홈 화면
```


## OTA 업데이트

- A/B 파티션 기반 원자적 업데이트 — `zyl-updater` (org.zylos.Updater), 설정 > 소프트웨어 업데이트
- 패키지(`tools/build-ota.sh`): rootfs + 슬롯별 verity 트리·서명 FIT. 비활성 슬롯에만 기록한 뒤 부트로더 플래그 전환
- **RSA-2048+SHA-256 서명 검증** (OpenSSL EVP API) — 서명 없는 패키지 거부
- SHA-256 해시 무결성 검증
- 부팅 후 상태 점검 통과 시 슬롯 확정, 3회 안에 확정되지 않으면 U-Boot 가 이전 슬롯으로 롤백
- 전체 이미지만 설치 (dm-verity rootfs 와 맞출 수 없는 delta·앱만·커널만은 거부)
