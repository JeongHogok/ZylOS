# Zyl OS — 프로젝트 규칙 (CLAUDE.md)

이 파일은 AI 어시스턴트와 모든 개발자가 반드시 따라야 하는 프로젝트 규칙입니다.

## 코드베이스 현황 (2026-10-15)

- **파일 수**: 528개 (`git ls-files`)
- **LoC**: ~74,600줄 (추적 중인 .c .h .js .css .html .rs .sh .py)
- **C 서비스 디렉토리**: 32개 (runtime/services/, 전부 D-Bus 데몬)
- **systemd 서비스**: 35개 (system/*.service — 서비스 32 + wam·compositor·apparmor)
- **JS 서비스 모듈**: 33개 (apps/system/services/)
- **HAL 구현체**: 15개 (runtime/hal/)
- **시스템 앱**: 22개 (apps/, shared 포함)
- **에뮬레이터**: Tauri 2.x (Rust), aes-gcm 암호화

## 아키텍처 경계 — 절대 규칙

### 1. OS 이미지(apps/)는 에뮬레이터(emulator-app/)에 절대 의존하지 않는다

**금지 패턴 (apps/ 디렉토리 내):**
- `window.__TAURI__` 참조 금지
- `__TAURI__.core.invoke` 호출 금지
- `window.parent.postMessage()` 직접 호출 금지 → 반드시 `ZylBridge.sendToSystem()` 사용
- 에뮬레이터 전용 커맨드명(`boot_device`, `shutdown_device` 등) 참조 금지
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
- 새 키 추가 시 반드시 **5개 언어** (ko, en, ja, zh, es) 동시 추가
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

- 시스템앱 판별은 OS 화이트리스트(`ZylPermissions.SYSTEM_APPS`)가 유일한 권위
- 어느 앱이 앞에 올지(OOBE·잠금 화면·홈·최근 앱·앱 전환)는 OS 만 정한다: 에뮬레이터
  `apps/system/shell.js`, 기기 `runtime/wam/src/shell` (같은 규칙, `tests/test_shell.c`
  ·`test_services.js` 가 같은 시나리오로 검증). 에뮬레이터·컴포지터는 그 답대로 띄우기만
  하고 앱 id 로 분기하지 않는다. 앞에 앱을 올리는 새 경로는 반드시 그 판단을 거칠 것.
  잠금 해제는 잠금 화면, 설정 완료는 OOBE 가 보낸 것만 — 보낸 앱은 OS 가 말한다
- 시스템 UI(상태바·알림판·빠른 설정·새 알림 배너·최근 앱)는 OS 앱 `apps/statusbar` 가
  모든 앱 위의 표면으로 그린다 — 기기는 WAM 이 투명한 전체 화면 창으로(입력 영역만
  바꾼다), 에뮬레이터는 두 번째 iframe 으로. 얼마나 보일지(bar·banner·full)는 셸
  정책(`panelMode`)이 정한다. 에뮬레이터·컴포지터가 이런 UI 를 직접 그리지 말 것
- 서비스 이벤트 중 한 앱의 것(알림, 지오펜스)은 제3자 앱에 자기 것만 간다 —
  `permissions.js` 의 `OWNED_EVENTS` 가 권위, `bridge.c` 사본(verify-all 대조)
- 앱이 스스로를 시스템앱으로 선언하는 것을 신뢰하지 않음
- `settings.json`, `.credentials/`, `.system/`은 보호 경로
- 서비스 권한 체크는 OS 레이어에서만 수행
- 사용자가 설정에서 끈 권한(`settings` 의 `app_permissions`)은 두 게이트 모두에서
  거부한다 — 에뮬레이터 `ZylPermissions`, 기기 `bridge.c`(WAM 이 settings 의
  `CategoryChanged` 로 따라간다). 시스템 앱의 권한은 끌 수 없다.
  첫 사용 동의 창은 없다 — 넣는다면 자리는 이 두 게이트이고, 그리는 쪽은 앱 화면에
  OS 공용 코드(인텐트 선택 시트와 같은 방식)다
- 부트 정책은 `board/bpi-f3/u-boot-env.txt` 단일 출처(`tests/test_boot_env.sh`),
  부트로더는 `tools/build-bootloader.sh` 로만 빌드·서명. 저장소·USB·파일이 부팅을
  바꾸는 경로를 되살리지 말 것. eFuse 굽기는 기본 off(`--burn-rotpk` 일 때만)

### 8-1. 사실의 단일 출처 — 같은 사실을 두 곳에 적지 않는다

아래는 각각 **한 곳**만 고치고, 나머지는 그 파일을 읽는다. 어긋나면 테스트가 막는다.

| 사실 | 단일 출처 | 읽는 쪽 |
|------|-----------|---------|
| 파티션 레이아웃 | `board/<id>/partitions.conf` | `build-image.sh`, `sign-image.sh`, `u-boot-env.txt`(테스트가 대조) |
| 실행 시점 패키지 | `system/packages.list` | `build-image.sh`, DEPLOYMENT_GUIDE |
| 보드·아키텍처·프로파일·prefix | meson → `builddir/zyl-board.conf` | `build-image.sh`, `run-qemu.sh` |
| 서비스 집합 | `meson.build` 의 `zyl_profile_extras` | 생성되는 `zyl-os.target` |
| 버스 접근 권한 | `system/dbus-1/zyl-os.conf` | dbus-daemon, `tests/test_dbus_policy.sh` |
| 서비스 권한 정책 | `apps/system/permissions.js` | `bridge.c`(verify-all 이 대조) |
| 교차 툴체인 | `tools/cross/<arch>.ini` | 보드의 `board_cross_file` |
| 둘 이상이 컴파일하는 C 코드 (앱 컨테이너·봉인·서명 검증) | `runtime/lib/` | appdata·fs·appstore·updater·WAM (`zyl_*_dep`) |
| 사용자 루트·소유자 uid·소유자 파일 트리 | `runtime/lib/users.h` | user·fs·appstore, 에뮬레이터 `user.js`(test_services 대조) |

서비스 디렉터리는 다른 서비스의 라이브러리가 되지 않는다 — 함께 쓰는 코드는
`runtime/lib/` 로 옮기고 각 쪽이 그 의존성을 링크한다.

서비스는 **시스템 버스**를 쓴다 — 유닛이 `Type=dbus` + `BusName=` 이므로 systemd 가
기다리는 버스가 시스템 버스고, 로그인 세션이 없는 기기 부팅에는 세션 버스가 없다.
새 서비스도 `G_BUS_TYPE_SYSTEM` 으로 이름을 올릴 것.

### 9. Intent 시스템 경계 규칙

- 앱 간 직접 함수 호출 금지 → 반드시 `ZylIntent.startActivity()` 경유
  (명시적: `{ targetApp }`, 암시적: `{ action, mimeType, data, extras }`)
- 인텐트 필터는 `app.json` 의 `intents` 로 선언 (`[{ action, mimeType }]`) —
  실행 중이 아닌 앱도 고를 수 있어야 하므로 런타임 등록이 아님.
  `tests/test_manifests.sh` 가 검증
- 받는 쪽은 `ZylIntent.onIntent(fn)`. 처리하지 않는 action 은 선언하지 않는다
- 해석은 OS 만: 에뮬레이터 `apps/system/intent.js`, 기기 `runtime/wam/src/intent`
  (같은 규칙). `sourceApp` 은 OS 가 채운다
- 인텐트 데이터 전달은 `extras` 객체로만 (DOM 공유 금지)
- 암시적 인텐트 resolve가 0개면 "앱 없음" 다이얼로그, 복수면 앱 선택 다이얼로그
  (시작한 앱 화면에 OS 공용 코드 `shared/bridge.js` 가 그림, 고른 앱은 제시된 것만)

### 10. ContentProvider 경계 규칙

- 앱 간 데이터 공유는 직접 파일 접근 금지 → 반드시 ContentProvider URI 경유
- `ZylContentProvider.query(callerAppId, uri)` — callerAppId 생략 금지
- 프로바이더 등록 시 `ZylPermissions` 체크 로직을 impl 내부에 포함하지 않음
  (권한 체크는 ZylContentProvider가 직접 수행)
- content:// URI 형식 엄수: `content://authority/path`


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
  `_forgetApp(appId)`. `tests/store_e2e.sh` 가 삭제 후 흔적 0 을 검증한다
- 앱이 누구인지는 OS 만 말한다: in-arg `caller`(항상 호출 앱), `container`
  (시스템 앱이면 "", 아니면 그 앱), `app_id`(제3자 앱이면 그 앱) — 요청 값 무시
- 스토어 패키지·카탈로그는 스토어 키 서명(패키지 전체의 SHA-256) 없이는 설치하지
  않는다 (`docs/APPSTORE_API.md`, `tools/build-store.py`)
- 사용자 앱은 봉인(`.zyl-seal/`)이 맞을 때만 설치·등록·실행: 공식 앱은 스토어 키,
  개발 빌드는 스토어가 서명한 인증서·프로필이 허락한 기기·앱에서만. 판정은 기기
  `app_seal.c`, 에뮬레이터 `apps/system/services/app-seal.js` 두 곳뿐이고 규칙이
  같아야 한다(한쪽을 바꾸면 다른 쪽과 `tests/test_app_seal.c`·`test_services.js` 도).
  봉인을 건너뛰는 경로(개발자 모드 등)를 만들지 말 것

---

## 빌드 & 검증

### CI 는 로컬에서 — 푸시 전 관문
```bash
tools/ci-local.sh                 # main 대비 바뀐 것(커밋 안 한 것 포함)에 걸리는 잡만 act 로
tools/ci-local.sh --dry-run       # 무엇이 걸리는지만
tools/ci-local.sh lint-js fuzz    # 잡 이름으로 골라서 / --all 은 전부
```
워크플로는 관심사별로 나뉘어 있고(web·native·image·riscv64·docker·licenses·emulator·
bootloader), **각 워크플로의 `pull_request.paths` 가 "어떤 변경이 이 검사를
부르는가" 의 유일한 출처**다 — 원격 PR 과 `ci-local.sh` 가 같은 목록을 읽는다
(`tests/test_ci_select.sh` 가 규칙을 검증). 새 검사를 넣으면 그 검사가 읽는
경로를 그 워크플로의 paths 에 함께 적을 것.
**로컬에서 녹색을 본 뒤에만 푸시한다** — 원격 GitHub Actions 는 같은 워크플로의
기록일 뿐 검증 수단이 아니다. 워크플로를 바꾸면 로컬에서도 돌아야 한다
(ARM 맥: Arch 이미지 잡만 amd64 에뮬레이션, 나머지는 네이티브).

### 전체 검증 (모든 영역)
```bash
bash tests/verify-all.sh
```
11개 섹션에서 다음을 검사합니다:
- **[1] OS 이미지 독립성** — Tauri 참조, postMessage 직접호출, 에뮬레이터 참조
- **[2] ES5 호환성** — let/const/arrow function
- **[3] i18n 완전성** — 하드코딩 문자열, 5개 언어 키 균등
- **[4] Clean Architecture 헤더** — 모든 JS/CSS 파일
- **[5] CSS 렌더링 호환** — backdrop-filter 폴백
- **[6] Mock/Demo 금지** — 가짜 데이터 키워드
- **[7] 앱 매니페스트** — app.json 필수 필드
- **[8] 에뮬레이터 규칙** — 비즈니스 로직 금지, Rust 빌드
- **[9] runtime/ C 코드** — TODO/FIXME, 빈 함수, CA 헤더
- **[10] systemd 서비스** — unit 파일 존재
- **[11] 전체 기술부채** — TODO 수, console.log, 하드코딩 비밀

### 개별 검증
```bash
bash tests/check-os-independence.sh  # OS 이미지만
bash tests/test_js_syntax.sh         # JS 문법
bash tests/test_manifests.sh         # app.json
bash tests/test_image_profiles.sh    # 폼팩터 8종이 이미지로 성립하는지 (meson 필요)
node tests/perf/run.js               # 앱 성능 예산 (Chrome 필요, CI 'perf' 잡)
```

### 보드 없이 전체 경로 검증 (리눅스/컨테이너, root)
```bash
meson setup builddir --prefix=/usr -Dboard=emulator -Dprofile=phone
ninja -C builddir
tools/build-image.sh --builddir builddir --out out/image --with-kernel
#   --with-kernel: QEMU 게스트는 이미지 안의 커널·모듈로 부팅해야 DRM·입력이
#   보인다. 실기기는 서명 FIT 으로 커널을 받으므로 기본은 끔
tools/run-qemu.sh --image out/image/zyl-os-emulator-phone.img \
                  --builddir builddir --check                  # 부팅 + 자기점검
```
자기점검은 유닛·버스·정책·앱 디렉터리·`/data` 와 화면 체인(DRM → Wayland 소켓
→ WAM 이 띄운 앱)까지 본다. riscv64(실기기와 같은 아키텍처·HAL 백엔드)로도 같은 레인을 돌린다 — 교차 빌드
(`--cross-file tools/cross/riscv64.ini -Dboard=generic-riscv64`) + binfmt 2단계
부트스트랩. CI 의 `qemu-boot`·`qemu-boot-riscv64` 잡이 매 커밋 같은 일을 한다.
이 레인이 **검증하지 않는 것**:
U-Boot·서명 FIT·dm-verity·A/B 전환(부트로더 빌드와 `tools/sign-image.sh`·
`tests/test_boot_env.sh` 소관), SoC 고유 경로(GPU·모뎀·ISP·eFuse).
기기에서도 같은 자기점검을 돌릴 수 있다: `systemctl start zyl-selftest`.

### 성능 예산
앱마다 시작 시간·메인스레드 블로킹·힙·DOM 노드·레이아웃 수를 CPU 스로틀 +
GPU 없이 측정해 `tests/perf/budgets.json` 과 비교한다. 의도된 변경으로 수치가
바뀌면 `node tests/perf/run.js --update` 로 예산을 갱신하고 그 이유를 커밋에 적는다.
예산을 올리는 것으로 회귀를 덮지 말 것.

### Git Pre-Commit Hook
커밋 시 자동으로 apps/ 변경분에 대해 검사 실행:
- Tauri 참조 차단
- postMessage 직접 호출 차단
- ES5 위반 차단
