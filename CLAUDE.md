# Zyl OS — 프로젝트 규칙 (CLAUDE.md)

이 파일은 AI 어시스턴트와 모든 개발자가 반드시 따라야 하는 프로젝트 규칙입니다.

## 아키텍처 경계 — 절대 규칙

### 1. OS 이미지(apps/)는 에뮬레이터(emulator-app/)에 절대 의존하지 않는다

**금지 패턴 (apps/ 디렉토리 내):**
- `window.__TAURI__` 참조 금지
- `__TAURI__.core.invoke` 호출 금지
- `window.parent.postMessage()` 직접 호출 금지 → 반드시 `ZylBridge.sendToSystem()` 사용
- 에뮬레이터의 Tauri 커맨드명(`boot_device` 등) 참조 금지
- `emulator-app/` 경로의 파일 import/참조 금지

**필수 패턴:**
- IPC는 반드시 `ZylBridge.sendToSystem()` 또는 `ZylBridge.requestService()` 경유
- 서비스 호출은 반드시 `ZylSystemServices.handleRequest()` 경유
- 하드웨어 접근은 반드시 `_invoke()` 추상화 경유 (Tauri가 아닌 HAL)
- `requestService()`는 **Promise를 반환**한다. `requestId` 기반 응답 매칭.

### 2. 에뮬레이터(emulator-app/)에 OS 로직을 넣지 않는다

에뮬레이터는 **빈 깡통 컨테이너** 역할만 한다:
- IPC 라우팅 (postMessage ↔ ZylSystemServices)
- iframe 컴포지터 (앱 생명주기, 화면 전환)
- Tauri 커맨드 브릿지 (Rust ↔ JS)

**금지:**
- 비즈니스 로직, 권한 검사, 보안 정책을 에뮬레이터에 구현
- 서비스 구현을 에뮬레이터에 배치
- 하드코딩된 앱 목록, 시스템앱 판별 로직을 에뮬레이터에 배치

### 3. ES5 호환성 필수

apps/ 디렉토리의 모든 JS 파일은 WebKitGTK RISC-V 호환을 위해 **ES5 문법만** 사용:
- `let` / `const` 사용 금지 → `var` 사용
- Arrow function (`=>`) 사용 금지 → `function()` 사용
- Template literal (`` ` ``) 사용 금지 → 문자열 연결 사용
- `class` 키워드 사용 금지 → 프로토타입 패턴 사용
- `async` / `await` 사용 금지 → Promise `.then()` 체인 사용
- Destructuring, spread, default parameter 사용 금지

### 4. i18n 완전 적용 필수

- 사용자에게 보이는 **모든 텍스트**는 `zylI18n.t('key')` 경유
- 하드코딩된 영어/한국어 문자열 금지
- 앱의 번역은 **ko·en·ja·zh·es** 를 함께 갖춘다 — 새 키는 다섯 언어에 동시 추가
  (verify-all A6, 제3자 앱은 `tools/zyl validate` 가 검사)
- i18n 키는 `{앱이름}.{키이름}` 네이밍 규칙 준수

### 5. Mock/Demo 데이터 절대 금지

- 하드코딩된 샘플 데이터 금지 (가짜 연락처, 메시지, 앱 목록 등)
- 모든 데이터는 실제 시스템 서비스를 통해 획득
- 빈 상태(empty state)는 허용하되, 가짜 데이터로 채우지 않음

### 6. CSS 소프트웨어 렌더링 호환

- `backdrop-filter` 사용 시 반드시 **불투명 배경색 폴백** 제공 (opacity ≥ 0.85)
- GPU 의존 효과(3D transform, heavy filter)는 점진적 향상으로만 사용
- 기본 레이아웃은 GPU 없이도 동작해야 함

### 7. 클린아키텍처 파일 헤더

모든 소스 파일 상단에 다음 헤더 필수:
```
// [Clean Architecture] {Layer} Layer - {Role}
// 역할: ...
// 수행범위: ...
// 의존방향: ...
// SOLID: ...
```

### 8. 보안 원칙

- 시스템 앱은 OS 이미지에 들어간 앱이다 — 기기는 이미지의 앱 디렉터리(`meson.build` 의
  `install_subdir`), 에뮬레이터는 `ZylPermissions.SYSTEM_APPS`. 두 집합은 같아야 한다
  (verify-all 대조). 이미지 밖에서 온 앱(스토어·개발 빌드)은 무엇을 선언하든 제3자 앱
- 어느 앱이 앞에 올지(OOBE·잠금 화면·복구 화면·홈·최근 앱·앱 전환)는 OS 만 정한다: 에뮬레이터
  `apps/system/shell.js`, 기기 `runtime/wam/src/shell` (같은 규칙, `tests/test_shell.c`
  ·`test_services.js` 가 같은 시나리오로 검증). 에뮬레이터·컴포지터는 그 답대로 띄우기만
  하고 앱 id 로 분기하지 않는다. 앞에 앱을 올리는 새 경로는 반드시 그 판단을 거칠 것.
  잠금 해제는 잠금 화면, 설정 완료는 OOBE 가 보낸 것만 — 보낸 앱은 OS 가 말한다
- 시스템 UI(상태바·알림판·빠른 설정·새 알림 배너·최근 앱)는 OS 앱 `apps/statusbar` 가
  모든 앱 위의 표면으로 그린다 — 기기는 WAM 이 투명한 전체 화면 창으로(입력 영역만
  바꾼다), 에뮬레이터는 두 번째 iframe 으로. 얼마나 보일지(bar·banner·full)는 셸
  정책(`panelMode`)이 정한다. 에뮬레이터·컴포지터가 이런 UI 를 직접 그리지 말 것
- 서비스 이벤트 중 한 앱의 것(알림, 지오펜스, 알람)은 제3자 앱에 자기 것만 간다 —
  `permissions.js` 의 `OWNED_EVENTS` 가 권위, `bridge.c` 사본(verify-all 대조)
- `settings.json`, `.credentials/`, `.system/`은 보호 경로
- 서비스 권한 체크는 OS 레이어에서만 수행
- 사용자가 설정에서 끈 권한(`settings` 의 `app_permissions`)은 두 게이트 모두에서
  거부한다 — 에뮬레이터 `ZylPermissions`, 기기 `bridge.c`(WAM 이 settings 의
  `CategoryChanged` 로 따라간다). 시스템 앱의 권한은 끌 수 없다.
- 위험 권한(`DANGEROUS_PERMISSIONS` — 카메라·마이크·위치·연락처·메시지·전화·문자)은 앱이
  처음 쓸 때 묻는다. 판단은 같은 두 게이트(`consentNeeded`, `bridge.c` 의 `consent_needed`),
  묻는 창은 시스템 UI(`apps/statusbar`)가 그린다 — 앱 자기 화면에 그리면 브리지가 앱과 같은
  JS 세계에 있어 앱이 스스로 "허용"을 보낼 수 있다. 답은 시스템 UI 가 보낸 것(`permission.answer`,
  그 질문의 토큰)만, 앞에 있는 앱의 질문만 띄운다(에뮬레이터 `apps/system/consent.js`, 기기 WAM).
  답은 남는다 — 허용은 `app_grants`, 거부는 `app_permissions`(설정이 바꿀 때까지)
- 앱이 닿을 수 있는 곳은 OS 가 권한으로 만든 CSP 가 정한다 — 기기 `app_scheme.c` 의
  `zyl_app_scheme_csp`(WAM 이 모든 응답 헤더로), 에뮬레이터 `sandbox.js` 의
  `getContentPolicy`(같은 문자열, `test_app_scheme.c`·`test_services.js`). 코드는 앱 자기
  파일에서만 — 바깥 스크립트·eval·인라인 스크립트를 어느 앱에도 열지 말 것(네트워크와
  합치면 받아 온 코드가 봉인을 건너뛰고, 시스템 앱은 남이 보낸 문자열 — 알림·파일 이름·
  Wi-Fi 이름 — 을 그린다. verify-all A3b). `network` 는 https/wss 만.
  권한이 바뀐 앱은 실행 중이면 닫는다 — CSP 는 문서와 함께 오므로
- 앱 페이지가 OS 에 닿는 길은 그 앱 자기 최상위 문서뿐 — WAM 이 웹뷰마다 만든 토큰을
  브리지 스크립트(`runtime/wam/src/bridge/bridge.js`)만 갖는다. 앱이 띄운 프레임이나
  옮겨 간 페이지에 브리지를 열지 말 것(에뮬레이터 `emulator.js` 의 `senderOf` 도 같은 규칙)
- 앱은 웹을 자기 문서에 프레이밍하지 않는다 — 웹 콘텐츠는 OS 가 소유하는 별도 뷰
  (`webcontent` 서비스, 브리지 없는 WebKitWebView·별도 웹 프로세스)가 띄운다. 임베드는
  시스템 앱만(예약 권한 `webcontent`). 브라우저(크롬)는 그 서비스로만 콘텐츠를 몰고
  url·title·loading 을 이벤트로 받을 뿐 콘텐츠 DOM 에 닿지 않는다. 기기는 WAM 이 앱 창
  안에 합성(`wam.c`), 에뮬레이터는 별도 iframe 으로 근사(교차 출처라 title·url 은 못 봄).
  앱 문서에 `<iframe>` 으로 웹을 띄우는 경로를 되살리지 말 것
- 화면에 없는 앱에 닿는 길은 푸시(`org.zylos.Push`, 서버 연결 하나)와 알람(절전 중에도
  깨움)뿐 — 앱마다 지속 연결이나 백그라운드 실행 장치를 새로 만들지 말 것. 푸시 토픽은
  ntfy 의 UnifiedPush 꼴(`up`+12자)이어야 서버가 앱 서버가 아닌 기기 쪽 한도로 센다
- 부트 정책은 `board/boot-policy.txt`(모든 보드) + `board/<id>/u-boot-env.txt`(보드의
  상수·훅)가 단일 출처(`tests/test_boot_env.sh` 가 보드마다 실행), 부트로더는
  `tools/build-bootloader.sh` 로만 빌드·서명. 저장소가 쓸 수 있는 변수는 정책의
  "Runtime state" 뿐이다. 저장소·USB·파일이 부팅을
  바꾸는 경로를 되살리지 말 것. eFuse 굽기는 기본 off(`--burn-rotpk` 일 때만)

### 8-1. 사실의 단일 출처 — 같은 사실을 두 곳에 적지 않는다

아래는 각각 **한 곳**만 고치고, 나머지는 그 파일을 읽는다. 어긋나면 테스트가 막는다.

| 사실 | 단일 출처 | 읽는 쪽 |
|------|-----------|---------|
| 파티션 레이아웃·부트 환경의 자리·커널 콘솔 | `board/<id>/partitions.conf` | `build-image.sh`(fstab·`fw_env.config`), `sign-image.sh`, `build-bootloader.sh`(U-Boot 환경 설정 대조), Boot HAL(`runtime/hal/meson.build`), `u-boot-env.txt`(테스트가 대조) |
| 부트 정책 | `board/boot-policy.txt` + `board/<id>/u-boot-env.txt` | U-Boot 기본 환경(`build-bootloader.sh`), `tests/test_boot_env.sh`, `tests/verified_boot.sh` |
| 실행 시점 패키지 | `system/packages.list` | `build-image.sh` |
| 보드·아키텍처·프로파일·prefix | meson → `builddir/zyl-board.conf` | `build-image.sh`, `run-qemu.sh` |
| 서비스 집합 | `meson.build` 의 `zyl_core_services` + `zyl_profile_extras` | 생성되는 `zyl-os.target` |
| 버스 접근 권한 | `system/dbus-1/zyl-os.conf` | dbus-daemon, `tests/test_dbus_policy.sh` |
| 서비스 권한 정책 | `apps/system/permissions.js` | `bridge.c`(verify-all 이 대조) |
| 교차 툴체인 | `tools/cross/<arch>.ini` | 보드의 `board_cross_file` |
| 둘 이상이 컴파일하는 C 코드 (앱 컨테이너·봉인·서명 검증·JSON 문자열 읽기 `zyl_json_str`) | `runtime/lib/` | appdata·fs·appstore·updater·WAM·HAL·서비스 (`zyl_*_dep`) |
| 푸시 서버 주소 | `system/push.json` | `zyl-push`(`/etc/zyl-os`), 에뮬레이터 `push.rs`(include_str) |
| 사용자 루트·소유자 uid·소유자 파일 트리 | `runtime/lib/users.h` | user·fs·appstore, 에뮬레이터 `user.js`(test_services 대조) |

서비스 디렉터리는 다른 서비스의 라이브러리가 되지 않는다 — 함께 쓰는 코드는
`runtime/lib/` 로 옮기고 각 쪽이 그 의존성을 링크한다.

서비스는 **시스템 버스**를 쓴다 — 유닛이 `Type=dbus` + `BusName=` 이므로 systemd 가
기다리는 버스가 시스템 버스고, 로그인 세션이 없는 기기 부팅에는 세션 버스가 없다.
새 서비스도 `G_BUS_TYPE_SYSTEM` 으로 이름을 올릴 것.

### 8-2. 저장소 — 루트는 읽기 전용, 남는 것은 /data

- 루트는 읽기 전용이다(기기에서는 dm-verity). `/var`·`/tmp` 는 휘발(tmpfs)이고,
  부팅을 넘어 남을 상태는 전부 `/data/<서비스>` 에 둔다 — 디렉터리는 tmpfiles
  `zyl-data.conf`, 유닛의 `ReadWritePaths=`, AppArmor 프로필이 같은 경로를 말해야 한다
  (verify-all D2·D3 대조)
- 다른 데몬이 `/var`·`/etc` 에 두는 상태도 설정이나 drop-in 으로 `/data` 로
  (bluez·NetworkManager 처럼 — `system/`). 이미지 빌드 때 굽는 캐시는 `/usr` 쪽에
- 잠금 전환과 공장 초기화는 `/data` 를 통째로 지운다 — 남겨야 할 것을 다른 곳에 두지 말 것
- `/data` 는 비어 있을 때만 만든다(makefs + `zyl-data-blank`). 열지 못하면 멈추지 않고 복구
  부팅(tmpfs, `zyl-recovery.service`)으로 복구 화면만 띄우고, 지우는 것은 사용자가 그 화면에서
  고른 초기화뿐이다(`storage.eraseUserData` → `zyl-erase-data.service`, 복구 부팅에서만 —
  verify-all D6, `tests/recovery_boot.sh`). 손상된 `/data` 를 묻지 않고 지우는 길을 만들지 말 것
- 사용자 데이터(앱 컨테이너·사용자 파일·연락처·메시지·키체인)는 PIN 으로 잠긴다(fscrypt).
  어느 디렉터리인지와 키는 잠금 서비스(`lock.c` 의 `ZYL_CE_DIRS`, verify-all D5)의 것이고,
  부팅 뒤 첫 맞는 PIN 전에는 읽을 수도 쓸 수도 없다. 그 디렉터리를 쓰는 서비스는 잠긴 동안
  실패로 답하거나 `zyl_fscrypt_when_unlocked` 로 기다린다 — 잠긴 동안 들어온 것(문자)은
  잠기지 않는 곳에 두었다가 옮긴다. 잠긴 채로 돌아야 하는 상태(알람·통화 기록·설정·망)는
  그 밖에 둘 것

### 9. Intent 시스템 경계 규칙

- 앱 간 직접 함수 호출 금지 → 반드시 `ZylIntent.startActivity()` 경유
  (명시적: `{ targetApp }`, 암시적: `{ action, mimeType, data, extras, files }`)
- 인텐트 필터는 `app.json` 의 `intents` 로 선언 (`[{ action, mimeType }]`) —
  실행 중이 아닌 앱도 고를 수 있어야 하므로 런타임 등록이 아님.
  `tests/test_manifests.sh` 가 검증
- 받는 쪽은 `ZylIntent.onIntent(fn)`. 처리하지 않는 action 은 선언하지 않는다
- 해석은 OS 만: 에뮬레이터 `apps/system/intent.js`, 기기 `runtime/wam/src/intent`
  (같은 규칙). `sourceApp` 은 OS 가 채운다
- 인텐트 데이터 전달은 `data`(텍스트·URL)·`extras` 객체·`files`(파일)로만 (DOM 공유 금지).
  파일 경로를 `data` 에 싣지 말 것 — `files` 는 OS 가 받는 앱의 사본(`Inbox/`, 시스템 앱이면
  `Downloads/`)으로 바꿔 건넨다(기기 `fs.c` 의 `Handoff`, 에뮬레이터 `fs._handoff`).
  앱 브리지로 부를 수 있는 복사 경로를 만들지 말 것
- 사용자의 파일을 앱에 주는 길은 PICK 뿐 — 처리 앱은 시스템 앱만(OS 화면에서 고름),
  답(`intent.result`)은 대기 중인 그 처리 앱만, 받는 앱에는 사본으로
- 암시적 인텐트 resolve가 0개면 "앱 없음" 다이얼로그, 복수면 앱 선택 다이얼로그
  (시작한 앱 화면에 OS 공용 코드 `shared/bridge.js` 가 그림, 고른 앱은 제시된 것만)

### 10. 앱 간 데이터 공유

- 앱의 데이터는 그 앱 컨테이너 안에만 있다(규칙 12). 다른 앱과 나누는 길은 둘뿐:
  OS 서비스(권한이 지킨다 — 예: 연락처는 `contacts` 서비스와 `contacts.read`)와
  인텐트의 `extras`(규칙 9)
- 앱이 다른 앱의 파일·저장소·DOM 을 직접 읽는 경로를 만들지 말 것

### 11. 폼팩터 — 1 OS × N 프로파일

- 폼팩터는 하드웨어다 — 제조사가 빌드한 프로파일(`-Dprofile`)이지 사용자 설정이
  아니다. 기기는 WAM 이 빌드 프로파일을 선언하고(`window.__zylFormFactor`),
  에뮬레이터는 하드웨어(입력·화면)만 알리며 OS 가 판정한다. 설정 등 UI 에서
  기기 유형을 바꾸는 경로를 만들지 말 것
- 새 앱의 `app.json` 에는 `formFactors` 선언 필수 (`["*"]` = 범용).
  유효 ID는 `apps/shared/form-factor.js` 의 `PROFILES` 가 유일한 권위이며
  `tests/test_manifests.sh` 가 검증한다
- 폼팩터별 스타일은 `html.ff-<id>` 선택자로만 작성. 화면 크기 분기를 앱 JS에
  하드코딩 금지 → `ZylFormFactor.getProfile()` 경유
- 그리드/레이아웃 기하는 CSS 변수가 단일 출처. JS가 상수로 중복 보관 금지
  (`home.js` 가 `--grid-cols`/`--grid-rows` 를 읽는 방식 참조)
- CSS에서 `zoom: var(...)` 사용 금지 — legacy 속성이라 var() 치환이 무시되고
  조용히 1이 된다. 루트 스케일링은 `ZylFormFactor.apply()` 소관
- apps/ CSS에서 raw `vh`/`vw` 사용 금지 → `var(--ff-vh)` / `var(--ff-vw)` 경유.
  루트 zoom 이 뷰포트 단위를 스케일하지 않아 확대 폼팩터에서 Z배 넘친다
- 새 폼팩터 추가 시: `PROFILES` + `meson_options.txt` choices +
  `meson.build` 의 `zyl_profile_extras` + 보드의 `board_form_factors` 를 함께 갱신
- 새 보드 추가 시: `board/<id>/meson.build` 에 `board_*` 계약 전부 선언.
  지원 못 하는 폼팩터는 `board_form_factors` 에서 빼서 빌드가 거부하게 할 것

### 12. 앱 컨테이너 — 지우면 아무것도 남지 않는다 (iOS 방식)

- 앱이 만드는 모든 것은 컨테이너 `/data/appdata/<앱 id>/` 에만:
  비공개 데이터(`appdata` 서비스), 파일(제3자 앱의 `fs` 는 브리지의 `container`
  로 자기 컨테이너가 루트), 웹 저장소(WAM 이 앱마다 `zyl-app://<id>/` 출처와
  `webkit/` 세션). 앱이 공유 위치에 흔적을 남기는 API 를 새로 만들지 말 것
- 앱별 상태를 갖는 서비스를 추가하면 삭제 때 정리 경로도 함께:
  기기 `org.zylos.AppStore` 의 `AppRemoved` 시그널 구독, 에뮬레이터 모듈의
  `_forgetApp(appId)`. 삭제 후 흔적 0 은 `tests/store_e2e.sh` 와 그 서비스의 단위 테스트가 검증한다
- 앱이 누구인지는 OS 만 말한다: in-arg `caller`(항상 호출 앱), `container`
  (시스템 앱이면 "", 아니면 그 앱), `app_id`(제3자 앱이면 그 앱) — 요청 값 무시
- 스토어 키는 2단이다: 이미지의 루트 키(`store.json` `rootKey`)는 서명 키 인증서만,
  나머지(카탈로그·패키지·봉인·개발자 인증서)는 그 서명 키가. 스토어 패키지·카탈로그는
  서명 키 서명(패키지 전체의 SHA-256) 없이는 설치하지 않는다 (`docs/APPSTORE_API.md`,
  `tools/build-store.py`). 회수(앱·개발자 인증서·서명 키)는 기기가 지키고, 이미 본 것보다
  오래된 카탈로그로 되돌리지 않는다
- 사용자 앱은 봉인(`.zyl-seal/`)이 맞을 때만 설치·등록·실행: 공식 앱은 서명 키,
  개발 빌드는 서명 키가 서명한 인증서·프로필이 허락한 기기·앱에서만. 판정은 기기
  `app_seal.c`, 에뮬레이터 `apps/system/services/app-seal.js` 두 곳뿐이고 규칙이
  같아야 한다(한쪽을 바꾸면 다른 쪽과 `tests/test_app_seal.c`·`test_services.js` 도).
  봉인을 건너뛰는 경로(개발자 모드 등)를 만들지 말 것

---

## 빌드 & 검증

명령과 잡 목록은 [CONTRIBUTING.md](CONTRIBUTING.md)(로컬 CI·테스트·퍼저·성능 예산·훅),
보드 없이 이미지를 만들어 QEMU 로 부팅하는 레인은 [docs/DEPLOYMENT_GUIDE.md](docs/DEPLOYMENT_GUIDE.md)
에 있다. 여기에는 지킬 것만 적는다.

- **로컬에서 녹색을 본 뒤에만 푸시한다** — `tools/ci-local.sh`. 원격 GitHub Actions 는
  같은 워크플로의 기록일 뿐 검증 수단이 아니다
- 각 워크플로의 `pull_request.paths` 가 "어떤 변경이 이 검사를 부르는가" 의 유일한
  출처다(원격 PR 과 `ci-local.sh` 가 같은 목록을 읽고, `tests/test_ci_select.sh` 가 규칙을
  검증). 새 검사를 넣으면 그 검사가 읽는 경로를 그 워크플로의 paths 에 함께 적을 것.
  워크플로를 바꾸면 로컬에서도 돌아야 한다
- `tests/verify-all.sh`(CI `verify-all`)는 이 파일의 규칙과 사본 대조의 기계 검사다.
  규칙이나 사본을 더하면 검사도 더할 것. 무엇을 보는지는 스크립트가 출처다
- 테스트는 판정 코드의 복사본이 아니라 실제 소스를 시험한다
- 성능 예산(`tests/perf/budgets.json`)을 올려 회귀를 덮지 말 것. 의도한 변경으로 갱신하면
  그 이유를 커밋에 적는다
- riscv64 레인은 기기와 같은 부팅 체인(부트 정책을 가진 U-Boot → 서명 FIT → dm-verity
  → A/B 업데이트·롤백)을 메인라인 U-Boot 로 돈다(`tests/verified_boot.sh`). 레인이 보지
  못하는 것 — 제조사 부트로더(FSBL·서명 체인), eMMC 부팅 영역 쓰기 보호, eFuse, SoC 고유
  경로(GPU·모뎀·ISP) — 은 부트로더 빌드의 자체 검증과 보드가 맡는다. 레인이 녹색이라고
  그 경로가 검증된 것이 아니다
