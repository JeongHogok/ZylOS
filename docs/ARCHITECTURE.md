# Zyl OS 아키텍처

Zyl OS 가 어떻게 나뉘고 각 부분이 무엇을 책임지는지, 그 나눔이 지켜야 하는 불변식을 적는다.
지켜야 할 규칙의 목록은 [CLAUDE.md](../CLAUDE.md), 보안 모델은 [SECURITY.md](SECURITY.md),
이미지 조립·배포는 [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md), 앱 작성은
[APP_DEVELOPMENT_GUIDE.md](APP_DEVELOPMENT_GUIDE.md) 에 있다.

## 큰 그림

Zyl OS 는 리눅스 위의 웹 앱 OS 다. 앱은 HTML/CSS/ES5 JS 이고 WebKit 에서 돈다.
같은 앱(`apps/`)이 두 곳에서 돈다.

- **기기** — 커널 → systemd → 컴포지터·WAM·C 서비스. 실제 OS 다.
- **에뮬레이터** — 개발 호스트의 Tauri 앱. 앱을 프레임에 띄우는 컨테이너일 뿐이고,
  OS 의 판단은 OS 이미지의 JS(`apps/system/`)가 한다.

```
              앱 (apps/<앱>/)  +  OS 공용 코드 (apps/shared/)
                              │ ZylBridge
           ┌──────────────────┴───────────────────┐
  에뮬레이터 (emulator-app/)                 기기
  프레임 · IPC 라우팅                         WAM (runtime/wam/) ── 컴포지터 (compositor/)
           │                                       │ D-Bus 시스템 버스
  OS 계층 JS (apps/system/)                   C 서비스 (runtime/services/)
           │ Tauri 커맨드                          │
  호스트의 사실 (emulator-app/src/)           HAL (runtime/hal/) → 커널 드라이버
```

## 계층

### 앱 — `apps/<앱>/`

- 앱은 `app.json` 으로 자신을 선언한다(권한, 인텐트 필터, 지원 폼팩터, 런처 노출).
  선언은 요청일 뿐이고 허락은 OS 가 한다.
- OS 와는 `ZylBridge` 로만, 다른 앱과는 인텐트로만 이야기한다. 다른 앱의 코드·파일·DOM 에
  직접 닿지 않는다.
- 홈·잠금 화면·첫 설정(OOBE)·상태바도 앱이다. 셸 정책이 이 앱들의 역할을 알 뿐이다.

### OS 공용 코드 — `apps/shared/`

모든 앱이 포함하는 OS 의 코드다. 두 호스트에서 같은 파일이 돈다.

- `bridge.js` — `ZylBridge`·`ZylIntent`. 호스트를 알아보고 전송로를 고른다(기기: WebKit
  메시지 핸들러, 에뮬레이터: 부모 프레임). OS 가 시키면 인텐트 선택 시트와 "앱 없음" 안내를
  앱 화면에 그리고, 텍스트 입력에 포커스가 가면 화면 키보드를 앱 페이지 안에 불러 띄운다.
- 화면 키보드(`apps/keyboard/`)도 OS 코드다. `bridge.js` 는 자기가 읽힌 `shared/` 옆의
  `keyboard/` 에서 그것을 불러오고, 앱의 출처에서는 `/shared/` 와 함께 `/keyboard/` 도 이미지의
  것이 나간다.
- `form-factor.js` — 폼팩터 판정과 적용 ([폼팩터](#폼팩터--보드--프로파일)).
- `i18n.js` — 번역 엔진. 각 앱은 자기 키를 `addTranslations` 로 등록한다.

### OS 계층 — 같은 규칙, 두 구현

OS 의 판단 — 무엇이 앞에 올지, 누가 인텐트를 받을지, 어느 앱이 무엇을 부를 수 있는지,
설치된 앱의 코드가 서명된 그대로인지 — 은 두 번 구현된다. 에뮬레이터에서는 `apps/system/` 의
JS, 기기에서는 `runtime/` 의 C 다(`apps/system/` 은 기기 이미지에 설치되지 않는다).
둘이 어긋나면 에뮬레이터에서 되는 일이 기기에서 안 되므로, 쌍마다 둘을 대조하는 검사가 있다.

| 규칙 | 에뮬레이터 (JS) | 기기 (C) | 같음을 지키는 것 |
|---|---|---|---|
| 셸 정책 | `apps/system/shell.js` | `runtime/wam/src/shell/` | `tests/test_shell.c` 와 `tests/test_services.js` 의 같은 시나리오 |
| 인텐트 해석 (PICK 은 시스템 앱만, 건넬 파일) | `apps/system/intent.js` | `runtime/wam/src/intent/` (선택·PICK 대기는 `wam.c`) | `tests/test_intent.c`, `tests/test_services.js` |
| 앱 사이 파일 건네기 (받는 앱 루트로 복사) | `apps/system/services/fs.js` `_handoff` → `filesystem.rs` | `runtime/services/fs/fs.c` 의 `Handoff` 인터페이스 | `tests/test_fs_service.c`, `filesystem.rs` 테스트 |
| 서비스·메서드 권한, 받을 수 있는 이벤트 | `apps/system/permissions.js` (권위) | `runtime/wam/src/bridge/bridge.c` 의 표 | `tests/verify-all.sh` 가 표를 대조 |
| 네트워크 허용 도메인 | `apps/system/sandbox.js` (권위) | `runtime/services/network/network.c` | `tests/verify-all.sh` |
| 웹 권한 요청 (카메라·마이크, 그 밖은 거부) | `apps/system/sandbox.js` 의 `getPolicy` | `runtime/wam/src/wam.c` 의 `on_permission_request` | `tests/test_services.js` |
| 앱의 CSP (권한 → 닿는 곳) | `apps/system/sandbox.js` 의 `getContentPolicy` | `runtime/wam/src/scheme/app_scheme.c` 의 `zyl_app_scheme_csp` | `tests/test_app_scheme.c`, `tests/test_services.js` (같은 문자열) |
| 앱 봉인 | `apps/system/services/app-seal.js` | `runtime/lib/app_seal.c` | `tests/test_app_seal.c`, `tests/test_services.js` |
| 앱이 부르는 서비스 계약 | 앱의 `requestService` 호출 | 서비스의 D-Bus 메서드·인자 | `tests/verify-all.sh` 의 계약 공백 검사 |

한쪽을 바꾸면 다른 쪽과 그 검사를 같은 변경에서 바꾼다.

서비스도 두 벌이다. 에뮬레이터는 `apps/system/services/` 의 모듈(`apps/system/services.js` 가
라우팅), 기기는 `runtime/services/` 의 데몬이다. 앱이 보는 서비스·메서드 이름은 같다.

### 에뮬레이터 — `emulator-app/`

빈 깡통 컨테이너다 — 프레임을 띄우고, 메시지를 OS 계층 JS 에 넘겨 그 답대로 띄우고,
호스트에서만 알 수 있는 사실을 준다. 정책은 두지 않는다(CLAUDE.md 규칙 2). 하는 일과
실행법은 [emulator-app/README.md](../emulator-app/README.md#하는-일).

### 기기

#### systemd 와 시스템 버스

부트로더가 검증한 커널이 systemd 를 띄우고, systemd 는 `zyl-os.target` 을 올린다.
이 타깃은 빌드가 생성한다(`system/zyl-os.target.in`). 프로파일이 설치한 유닛만 들어가므로
목록이 빌드와 어긋날 수 없다. 컴포지터·WAM 과 `meson.build` 의 `zyl_core_services` 는 모든
프로파일에 들어가고, 프로파일마다 다른 것은 `zyl_profile_extras` 뿐이다.

모든 구성요소는 **D-Bus 시스템 버스**에서 만난다. 서비스 유닛이 `Type=dbus` + `BusName=` 이라
systemd 가 기다리는 버스가 시스템 버스이고, 로그인 세션이 없는 기기 부팅에는 세션 버스가 없다.
누가 `org.zylos.*` 이름을 갖고 누가 부를 수 있는지는 `system/dbus-1/zyl-os.conf` 한 곳이
정한다(기본 거부, 서비스 데몬과 WAM 만 허용). 앱은 버스에 닿지 않는다 — 앱의 호출은 WAM 이
받아 판정한 뒤 대신 보낸다.

#### C 서비스 — `runtime/services/<이름>/`

- 서비스마다 데몬 하나, 버스 이름 하나(`org.zylos.*`), 유닛 하나(`system/zyl-<이름>.service`).
- 하드웨어에는 HAL 로만 닿는다. 명령행 도구나 sysfs 를 직접 부르지 않는다.
- 누가 불렀는지는 WAM 이 채운 인자로 안다([SECURITY.md §1](SECURITY.md#1-앱이-누구인지는-os-가-말한다)).
- 상태가 바뀌면 시그널을 낸다. WAM 이 그 서비스를 부를 수 있는 앱에만 전달한다.
- 앱별 상태를 가지면 앱이 지워질 때(`org.zylos.AppStore` 의 `AppRemoved`) 함께 지운다.

#### WAM — `runtime/wam/`

앱 창과 OS 사이의 유일한 관문이다.

| 모듈 | 책임 |
|---|---|
| `manifest/` | 시스템 앱 디렉터리와 사용자 앱 디렉터리의 `app.json` 을 읽는다. 사용자 앱은 시스템 앱의 id 를 차지할 수 없다 |
| `lifecycle/` | 앱마다 창·웹뷰 하나. 실행·일시정지·재개·종료, 시스템 UI 의 표면 |
| `bridge/` | 웹뷰에 브리지를 주입하고 메시지를 받는다. 권한 판정, 신원 인자 채우기, D-Bus 호출, 서비스 시그널 전달 |
| `shell/` | 셸 정책 — 설정·잠금 상태, 앞에 올 수 있는 앱, 시스템 UI 크기 |
| `intent/` | 인텐트 해석 |
| `scheme/` | `zyl-app://<id>/` — 앱마다 자기 출처. `/shared/`·`/keyboard/` 는 이미지의 OS 코드, 나머지는 앱 디렉터리이고 그 밖은 내주지 않는다 |
| `oom/` | 메모리가 모자라면 앞에 있지 않은 앱을 오래된 순으로 닫는다 |
| `dbus/` | `org.zylos.WebAppManager` — 앱 실행·목록, 컴포지터가 부르는 내비게이션 |

봉인 확인과 내주는 바이트, 앱의 웹 저장소, 웹 프로세스 격리는 [SECURITY.md](SECURITY.md)
§5–7.

#### 컴포지터 — `compositor/`

wlroots 기반 Wayland 컴포지터다. 창을 그리고 입력을 나눌 뿐, 무엇이 앞에 올지는 정하지 않는다.

- **입력** — 터치·포인터·키를 포커스된 앱 표면에 보낸다. 화면 가장자리에서 시작한 터치가
  제스처로 판정되면 앱의 터치를 취소하고 WAM 의 내비게이션 메서드(홈·뒤로·앱 전환·알림판·
  최근 앱)를 부른다. 판정은 wlroots 없는 순수 함수다(`compositor/src/input/gesture_detect.c`).
- **활성화** — WAM 이 창을 앞으로 부르면(xdg-activation) 그대로 따른다. 판단은 이미 셸 정책이
  했다. 컴포지터가 스스로 창을 올리면 잠금 화면 위로 앱이 올라올 수 있다.
- **시스템 UI** — 시스템 UI 앱의 표면은 언제나 맨 위, 앱 창은 상태바 아래에 놓는다.
- **사용자 활동** — 입력이 있으면 전원 서비스에 `UserActivity` 를 알린다(묶어서). 쓰는 동안
  화면이 어두워지거나 잠기지 않는다.

#### HAL — `runtime/hal/`

- `hal.h` 는 서비스가 하드웨어에 닿는 유일한 포트다.
- 각 모듈은 그 하드웨어가 이 보드에 있는지도 답한다. 없으면 기능은 남고 비활성으로
  보인다 — 지어내지 않는다.
- 구현은 보드 계약의 `board_hal_backend` 가 고른다. `linux` 는 커널 드라이버와
  PipeWire·NetworkManager·BlueZ·sysfs(`hal_<모듈>.c`), `emulator` 는 VM·개발 호스트용
  (`hal_emulator.c`)이다. `emulator` 백엔드는 `-Dboard=emulator` 이미지(QEMU)의 것이고
  Tauri 에뮬레이터와는 관계없다.
- 보드 고유 경로가 필요한 모듈은 보드 계약이 소스를 바꿔 끼운다. 카메라가 그렇다 —
  `board_hal_camera` 가 없으면 V4L2(`hal_camera.c`).

#### 공유 C 코드 — `runtime/lib/`

둘 이상의 구성요소가 쓰는 코드는 여기서 한 번 컴파일한다. 한 서비스 디렉터리가 다른
서비스의 라이브러리가 되지 않게 하기 위해서다. 앱 컨테이너 경로, 서명 검증(OTA·스토어
패키지·봉인), 앱 봉인, 사용자와 그 파일의 위치가 여기 있다.

## 앱의 호출이 지나가는 길

```
앱 ─ ZylBridge.requestService(service, method, params) ─▶ Promise (requestId 로 짝짓기)
 │
 ├─ 에뮬레이터: postMessage → 컨테이너가 보낸 프레임으로 앱을 정한다
 │     → ZylSystemServices.handleRequest (apps/system/services.js)
 │        권한(ZylPermissions) → 보호 경로 → 서비스 모듈 → 필요하면 Tauri 커맨드
 │     → service.response
 │
 └─ 기기: WebKit 메시지 핸들러 → WAM (bridge.c)
       권한(서비스·메서드 표) → 신원 인자 채우기 → 시스템 버스 메서드 호출
       → C 서비스 (→ HAL) → 답을 앱의 콜백으로
```

서비스에서 앱으로 가는 이벤트(`service.event`)도 OS 가 거른다. 보낸 앱이 누구인지, 신원
인자, 시스템 앱 판별은 [SECURITY.md §1](SECURITY.md#1-앱이-누구인지는-os-가-말한다), 권한과
이벤트를 받을 앱은 [§3](SECURITY.md#3-권한).

### 인텐트

앱이 `ZylIntent.startActivity()` 로 인텐트를 시작하면 OS 가 받을 앱을 정한다. 명시적이면
`targetApp`, 암시적이면 `action`·`mimeType` 을 각 앱 `app.json` 의 `intents` 와 맞춘다(시작한
앱은 제외). 하나면 그 앱을 띄우고, 여럿이면 시작한 앱 화면에 선택 시트를 그리게 하고(고른 앱은
제시된 것만 받는다), 없으면 "앱 없음"을 알린다. 받는 앱은 `ZylIntent.onIntent()` 로
`action`·`data`·`mimeType`·`extras` 와 OS 가 채운 `sourceApp` 을 받는다. 앞에 올 수 있는지는
여기서도 셸 정책이 정한다. 규칙은 CLAUDE.md 규칙 9.

### 앱의 자리 — 컨테이너

앱이 만드는 모든 것은 그 앱의 컨테이너에만 있고, 앱을 지우면 아무것도 남지 않는다 —
[SECURITY.md §5](SECURITY.md#5-앱-컨테이너--지우면-아무것도-남지-않는다).

## 시스템 UI

- 상태바·알림판·빠른 설정·새 알림 배너·최근 앱은 OS 앱 `apps/statusbar` 하나가 그린다.
  모든 앱 위의 표면이다.
- 표면이 얼마나 보일지(bar·banner·full)는 셸 정책이 정한다. 시스템 UI 가 요청하면 셸이 답하고
  ([SECURITY.md §4](SECURITY.md#4-셸-정책--무엇이-앞에-오고-누가-잠금을-푸는가)), 결과는 시스템
  UI 에 `panel.mode` 로 알린다.
- 막대와 배너의 높이는 `runtime/wam/src/shell/shell.h` 의 `ZYL_SHELL_BAR_PX`·`ZYL_SHELL_BANNER_PX`
  가 정한다. 그것을 그리는 CSS(상태바 앱, 에뮬레이터 프레임)가 같은지는 `tests/verify-all.sh` 가 본다.
- 에뮬레이터는 두 번째 프레임의 크기를 바꾼다. 기기에서는 WAM 이 이 앱을 화면 크기의 투명한
  창으로 띄우고 입력을 받는 영역만 바꾸며, 컴포지터가 그 창을 언제나 맨 위에 둔다.

## 폼팩터 — 보드 × 프로파일

하나의 OS 에 폼팩터 프로파일이 여럿 있다. 폼팩터는 하드웨어이므로 제조사의 빌드가 정한다.
사용자 설정이 아니다.

**빌드 시점**

- `-Dboard=<id>` 는 `board/<id>/meson.build` 의 보드 계약을 읽는다. SoC·아키텍처·HAL 백엔드·
  디바이스 트리·교차 툴체인·지원 폼팩터를 선언한다. 계약 항목은 `board/meson.build` 에 있다.
- `-Dprofile=<id>` 는 이미지에 들어갈 서비스 집합과 WAM 이 선언할 폼팩터를 정한다.
- 보드가 지원한다고 선언하지 않은 조합은 configure 에서 실패한다.
- `emulator` 보드는 물리 제약이 없어 모든 프로파일을 받는다. 그래서 실기기 없이도 모든
  프로파일이 설정되고 그 유닛마다 바이너리가 있는지 검사한다(`tests/test_image_profiles.sh`).

**실행 시점**

- 기기는 WAM 이 빌드 프로파일을 `window.__zylFormFactor` 로 모든 앱에 선언한다.
- 에뮬레이터는 하드웨어(화면 크기, 입력 장치 `?zylInput=`)만 알리고 `form-factor.js` 가 판정한다.
- `form-factor.js` 가 `<html>` 에 `ff-<id>` 클래스와 `--ff-*` 변수를 건다. 앱은 `html.ff-<id>`
  선택자와 `ZylFormFactor.getProfile()` 로 적응한다. 유효한 ID 는 `PROFILES` 가 유일한 권위다.
  규칙은 CLAUDE.md 규칙 11.

## 부팅 체인

신뢰는 아래에서 위로 이어진다. 각 단계는 다음 단계의 서명을 검증한 뒤에만 넘긴다.

```
Boot ROM → FSBL (루트 키) → OpenSBI · U-Boot (서명된 FIT)
         → OS FIT: 커널 · 디바이스 트리 · initramfs (OS 키)
         → rootfs (dm-verity, A/B 슬롯) → systemd → zyl-os.target
```

- 부트 정책은 `board/bpi-f3/u-boot-env.txt` 한 곳에 있다. 부트로더는 `tools/build-bootloader.sh`
  로만 빌드·서명한다. 파티션 레이아웃은 `board/<id>/partitions.conf` 에 있다.
- 시스템 파티션은 읽기 전용이고 검증된다. 그래서 시스템 앱 디렉터리에 있다는 사실이 시스템 앱의
  근거가 된다. 바뀌는 것은 전부 `/data` 에 있다.
- 키·서명·verity 는 [SECURITY.md](SECURITY.md#9-검증-부팅-verified-boot), 부트로더 잠금과
  eFuse 는 [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md), 이미지 조립·서명·OTA 는
  [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md), 보드별 사실은 `board/<id>/README.md`.

## 경계를 지키는 것

이 문서의 경계는 검사로 지킨다. `tests/verify-all.sh` 가 경계 규칙과 두 구현의 표를 대조하고,
위 표의 테스트가 쌍마다 같은 규칙을 확인한다. QEMU 레인(`tools/run-qemu.sh --check`)이 이미지를
부팅해 유닛·버스부터 화면 체인까지 본다 —
[DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#자기점검-toolszyl-selftest). 실행법은
[CONTRIBUTING.md](../CONTRIBUTING.md#테스트-돌리기).
