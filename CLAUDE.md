# Zyl OS — 프로젝트 규칙 (CLAUDE.md)

이 파일은 AI 어시스턴트와 모든 개발자가 반드시 따라야 하는 프로젝트 규칙입니다.

## 코드베이스 현황 (2026-03-31)

- **파일 수**: 360개
- **LoC**: ~68,400줄
- **C 서비스 디렉토리**: 26개 (runtime/services/)
- **systemd 서비스**: 25개 (system/*.service)
- **JS 서비스 모듈**: 29개 (apps/system/services/)
- **HAL 구현체**: 8개 (runtime/hal/)
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
- 앱이 스스로를 시스템앱으로 선언하는 것을 신뢰하지 않음
- `settings.json`, `.credentials/`, `.system/`은 보호 경로
- 서비스 권한 체크는 OS 레이어에서만 수행

### 9. Intent 시스템 경계 규칙

- 앱 간 직접 함수 호출 금지 → 반드시 `ZylIntent.startActivity()` 경유
- 인텐트 필터는 앱 초기화 시 `ZylIntent.registerFilter()`로 등록
- 인텐트 데이터 전달은 `extras` 객체로만 (DOM 공유 금지)
- 암시적 인텐트 resolve가 0개면 "앱 없음" 다이얼로그, 복수면 앱 선택 다이얼로그

### 10. ContentProvider 경계 규칙

- 앱 간 데이터 공유는 직접 파일 접근 금지 → 반드시 ContentProvider URI 경유
- `ZylContentProvider.query(callerAppId, uri)` — callerAppId 생략 금지
- 프로바이더 등록 시 `ZylPermissions` 체크 로직을 impl 내부에 포함하지 않음
  (권한 체크는 ZylContentProvider가 직접 수행)
- content:// URI 형식 엄수: `content://authority/path`

---

## 빌드 & 검증

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
```

### Git Pre-Commit Hook
커밋 시 자동으로 apps/ 변경분에 대해 검사 실행:
- Tauri 참조 차단
- postMessage 직접 호출 차단
- ES5 위반 차단
