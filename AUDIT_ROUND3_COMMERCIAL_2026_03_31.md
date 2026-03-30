# ZylOS v0.1.0 — 3차 감사: 상용 OS 품질 (다관점)

**일자**: 2026-03-31
**관점**: 신뢰성, 데이터 무결성, 에러 핸들링, 입력 검증, API 일관성, 접근성, 성능, IPC 안정성

---

## 발견 항목

### R: Reliability (신뢰성)

#### R1. JS 서비스 Promise 체인 — catch 누락 [HIGH]
- **위치**: contacts.js(8건), messaging.js(7건), appstore.js(6건), telephony.js(3건) 등
- **현상**: `.then()` 체인에 `.catch()` 없음 → 에러 시 unhandled rejection
- **영향**: 서비스 호출 실패 시 앱이 응답 없이 멈춤
- **패치**: 모든 서비스 모듈의 Promise 체인에 `.catch(function() { return fallback; })` 추가

#### R2. settings.js — 동시 쓰기 경합 [MEDIUM]
- **위치**: `apps/system/services/settings.js:41`
- **현상**: `updateSetting()`이 비동기 `save_settings` 호출 후 결과 대기 없이 `.catch(function(){})` → 순차 쓰기 보장 안 됨
- **영향**: 빠른 연속 설정 변경 시 마지막 값 외 손실 가능
- **패치**: 쓰기 큐 또는 debounce 적용

#### R3. C 서비스 — SIGTERM 미처리 [MEDIUM]
- **위치**: camera.c, audio.c, bluetooth.c, wifi.c — 모두 `g_main_loop_run()` 후 정리
- **현상**: SIGTERM 시 `g_main_loop_quit()` 호출 없이 즉시 종료 → D-Bus unown, 리소스 해제 누락
- **패치**: GLib unix signal source로 SIGTERM/SIGINT 처리

### D: Data Integrity (데이터 무결성)

#### D1. settings — fsync 없음 [HIGH]
- **위치**: `emulator-app/src/commands/settings.rs:75` — `fs::write()` 후 fsync 없음
- **현상**: 쓰기 직후 전원 손실 시 settings.json 손상/공백
- **영향**: OOBE 상태, PIN, 로케일 등 모든 설정 유실 가능
- **패치**: fsync 또는 atomic write (tmp → rename)

#### D2. contacts/messaging — JSON 파싱 에러 시 데이터 유실 [MEDIUM]
- **위치**: contacts.js:21, messaging.js:23
- **현상**: `JSON.parse(content)` 실패 시 `catch`에서 null 반환 → 해당 연락처/대화 무시
- **영향**: 파일이 일부 손상되면 정상 데이터까지 접근 불가
- **패치**: 파싱 실패 시 복구 시도 또는 백업에서 복원

### V: Input Validation (입력 검증)

#### V1. display — rotation/scale 범위 미검증 [MEDIUM]
- **위치**: display.js:28, 33
- **현상**: `parseInt(p.rotation)` — 0/90/180/270 외 값도 허용
- **패치**: 유효 회전값 화이트리스트

#### V2. audio — parseInt 후 범위 클램핑 누락 [MEDIUM]
- **위치**: audio.js:24-28
- **현상**: `_loadFromSettings`에서 볼륨 0-100 범위 강제 없음
- **패치**: `Math.max(0, Math.min(100, val))` 적용

#### V3. logger — getRecent count 상한 없음 [LOW]
- **위치**: logger.js:42
- **현상**: `parseInt(p.count)` — 무제한 요청 가능 → 대용량 응답
- **패치**: Math.min(count, MAX_LOG_LINES)

### A: Accessibility (접근성)

#### A6. HTML — aria/role 속성 누락 [MEDIUM]
- **위치**: 20개 앱의 index.html — 대부분 button/input에 aria-label 없음
- **패치**: 주요 인터랙티브 요소에 aria-label + role 추가

### P: Performance

#### P1. apps.getInstalled — 중복 호출 [LOW]
- **위치**: services.js init에서 `apps.getInstalled()` + settings 로드 후 다시 호출
- **패치**: _cache 이미 있으면 스킵 (기존 구현으로 해결됨 — 확인만)

### I: IPC Stability

#### I21. bridge.js — IPC 메시지 버전 없음 [MEDIUM]
- **위치**: `apps/shared/bridge.js`
- **현상**: 메시지에 버전 필드 없음 → OS 업데이트 시 하위 호환 불가
- **패치**: `{ type, version: 1, ... }` 필드 추가

---

## 총계: 12건

| 심각도 | 건수 |
|--------|------|
| HIGH | 2 (R1, D1) |
| MEDIUM | 8 |
| LOW | 2 |
