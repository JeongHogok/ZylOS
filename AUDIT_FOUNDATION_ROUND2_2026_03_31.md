# ZylOS v0.1.0 — 배포판 재단 2차 감사 보고서

**일자**: 2026-03-31
**범위**: 1차 감사 패치 이후 전체 코드베이스 (350파일, ~46,700 LoC)
**기준**: 배포판 관리 재단 — 제로 기술부채

---

## 2차 감사에서 발견된 신규 항목

### S: Security

#### S9. emulated_services.rs credential — 여전히 평문 [HIGH]
- **위치**: `emulator-app/src/commands/emulated_services.rs:238`
- **현상**: S1에서 네이티브 credential.c는 AES-256-GCM으로 교체했으나, Rust 에뮬레이터의 credential_store/lookup은 그대로 평문 `.enc` 파일
- **패치**: 에뮬레이터도 ring 또는 aes-gcm crate로 암호화 적용

#### S10. system() 잔존 — wifi.c, updater.c, hal_audio.c [MEDIUM]
- **위치**: wifi.c(4건), updater.c(15건+), hal_audio.c(1건)
- **현상**: 1차에서 appstore.c의 system()은 제거했으나 다른 파일에 27건 잔존
- **패치**: nmcli/wpctl 등 인자가 고정이라 injection 위험은 낮으나, 원칙적으로 posix_spawn 전환 또는 D-Bus API 직접 호출로 대체

### I: Incomplete (잔존 TODO/스텁)

#### I14. appstore.c — 루트 CA 인증서 로드 미구현 [MEDIUM]
- **위치**: `appstore.c:569` — `/* TODO: 루트 CA 인증서 로드 */`
- **패치**: 신뢰 저장소 경로에서 PEM 파일 로드 → register_cert

#### I15. appstore.c — WAM 앱 등록/제거 D-Bus 알림 미구현 [MEDIUM]
- **위치**: `appstore.c:910, 949`
- **패치**: D-Bus 시그널 또는 메서드 호출로 WAM에 통지

#### I16. appstore.c — 폐기 인증서로 서명된 앱 비활성화 미구현 [MEDIUM]
- **위치**: `appstore.c:1066`
- **패치**: 인증서 폐기 시 설치된 앱 스캔 → cert_fingerprint 매칭 → 비활성화

#### I17. appstore.c — 간이 JSON 파서 → json-glib 전환 [LOW→MEDIUM]
- **위치**: `appstore.c:294` — `json_get_string()` 수동 파서
- **문제**: 이스케이프 처리 불완전, 중첩 객체 미지원
- **패치**: json-glib (이미 WAM에서 사용) 통합

#### I18. updater.c — curl CLI → libcurl 전환 [MEDIUM]
- **위치**: `updater.c:430`
- **패치**: libcurl CURLOPT_PROGRESSFUNCTION 사용

#### I19. compositor gesture.c — D-Bus 시그널 미연결 [MEDIUM]
- **위치**: `compositor/src/input/gesture.c:75`
- **패치**: GoHome/GoBack/AppSwitcher D-Bus 시그널 실발송

#### I20. sandbox.c — CPU 사용률 계산 부정확 [LOW]
- **위치**: `sandbox.c:516`
- **현상**: `usage_usec % 100000 / 1000.0` — 두 시점 간 차이 미사용
- **패치**: 이전 측정값 저장 후 delta 계산

### A: Architecture

#### A5. services.js Public API — device/storage/fs/apps/settings가 null [MEDIUM]
- **위치**: `apps/system/services.js` 마지막 return 블록
- **현상**: `device: null, storage: null, fs: null, apps: null, settings: null` 반환
- **문제**: init() 후에도 이 속성들이 null로 남아 외부에서 `ZylSystemServices.device.getInfo()` 호출 시 에러
- **패치**: init() 완료 후 public API에 모듈 참조 바인딩

### B: Bug

#### B7. hal_linux.c — init() NULL 체크 후 호출 [LOW]
- **위치**: `runtime/hal/hal_linux.c:37`
- **현상**: `if (reg->wifi && reg->wifi->init() != 0)` — `init`이 NULL이면 crash
- **패치**: `reg->wifi->init` NULL 체크 추가

---

## 총계: 12건

| 심각도 | 건수 |
|--------|------|
| HIGH | 1 (S9) |
| MEDIUM | 9 (S10, I14~I19, A5) |
| LOW | 2 (I20, B7) |
