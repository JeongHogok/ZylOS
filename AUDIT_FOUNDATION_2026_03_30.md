# ZylOS v0.1.0 — 배포판 재단 코드베이스 감사 보고서

**일자**: 2026-03-30
**감사 범위**: 전체 코드베이스 (280파일, ~41,000 LoC)
**기준**: 배포판 관리 재단 — 제로 기술부채 원칙

---

## 분류 체계

| 코드 | 분류 | 설명 |
|------|------|------|
| S | Security | 보안 취약점 |
| I | Incomplete | 부실/스텁 구현 |
| A | Architecture | 아키텍처 위반/불일치 |
| F | Fidelity | 에뮬레이터-실기기 정합성 괴리 |
| B | Bug | 코드 결함 |
| X | Infrastructure | 빌드/테스트/배포 인프라 부재 |

---

## S: Security (8건)

### S1. 크리덴셜 저장소 — XOR "암호화"가 암호화가 아님 [CRITICAL]

**위치**: `runtime/services/credential/credential.c:50`, `emulator-app/src/commands/emulated_services.rs:166`

XOR cipher는 암호학적으로 무의미. `key[i % key_len]` 반복 패턴 → known-plaintext attack으로 즉시 키 복원.
마스터 키 미설정 시 `0x5A` 반복 하드코딩. Rust 에뮬레이터는 `.enc` 확장자 + 완전 평문 저장.

**영향**: PIN, WiFi 비밀번호, 토큰 등 모든 크리덴셜 평문 노출.

**패치 방향**: OpenSSL EVP_aes_256_gcm + PBKDF2 도입. 에뮬레이터도 동일 수준.

### S2. 앱 서명 검증 — verify_rsa_signature()가 항상 true [CRITICAL]

**위치**: `runtime/services/appstore/appstore.c:143`

```c
return signature_b64 != NULL && public_key_pem != NULL
    && strlen(signature_b64) > 0 && strlen(public_key_pem) > 0;
```

아무 문자열이나 SIGNATURE에 넣으면 검증 통과.

**패치 방향**: OpenSSL RSA_verify() 실제 구현. 서명 없는 패키지 설치 거부 (dev mode 제외).

### S3. SHA-256 해시 — 실제 SHA-256이 아님 [CRITICAL]

**위치**: `runtime/services/appstore/appstore.c:120`

XOR 폴딩 + `*31` 연산. 충돌 저항성 없음.

**패치 방향**: OpenSSL SHA256() 함수 사용.

### S4. 패키지 설치 시 system() 호출 — Command Injection [HIGH]

**위치**: `runtime/services/appstore/appstore.c:310, 410`

`system("unzip -o -q '%s' -d '%s'")` — 작은따옴표 이스케이프 불완전.

**패치 방향**: libzip 직접 통합. system() 완전 제거.

### S5. seccomp — BPF 필터 미적용 [CRITICAL]

**위치**: `runtime/services/sandbox/sandbox.c:123`

`prctl(PR_SET_NO_NEW_PRIVS)` 후 실제 BPF 필터 로드 없이 리턴. "적용됨" 로그만 출력.

**패치 방향**: libseccomp 통합. STRICT/DEFAULT/PERMISSIVE 각각 실제 시스콜 차단 규칙 구현.

### S6. 터미널 위험 명령 필터링 — 우회 가능 [HIGH]

**위치**: `emulator-app/src/commands/terminal.rs:32`

`contains()` 기반 매칭 → 공백/개행 삽입, 래퍼 명령, 환경변수, 순서 변경으로 우회.

**패치 방향**: 정규식 기반 + 명령 파싱 후 검증. allowlist 방식 전환 고려.

### S7. postMessage origin 미검증 [MEDIUM]

**위치**: `emulator-app/ui/js/emulator.js` 전역 message listener

`e.origin` 검증 없이 모든 메시지 처리.

**패치 방향**: 허용된 origin만 처리. iframe의 경우 `e.source === appFrame.contentWindow` 검증.

### S8. OTA 업데이트 — 서명 미검증 [HIGH]

**위치**: `runtime/services/updater/updater.c`

SHA-256 해시만 검증. 해시+이미지 동시 교체 가능.

**패치 방향**: 업데이트 매니페스트에 RSA-2048 서명 적용.

---

## I: Incomplete (13건)

### I1. WAM 브릿지 — service.request D-Bus 디스패치 없음 [HIGH]

**위치**: `runtime/wam/src/bridge/bridge.c`

`app.close`, `app.launch` 핸들러만 존재. 앱의 `service.request` → D-Bus 라우팅 미구현.

### I2. 네트워크 namespace — loopback/veth 미설정 [MEDIUM]

**위치**: `runtime/services/sandbox/sandbox.c:227`

`unshare(CLONE_NEWNET)` 호출 후 loopback 미설정 → 네트워크 완전 차단.

### I3. 사용자 권한 드롭 — 주석 처리 [HIGH]

**위치**: `runtime/services/sandbox/sandbox.c:236`

`setuid/setgid` 코드 주석. 앱이 root로 실행.

### I4. D-Bus 정책 XML — 생성만, 적용 없음 [MEDIUM]

**위치**: `runtime/services/sandbox/sandbox.c:zyl_sandbox_generate_dbus_policy()`

### I5. AppArmor 프로필 — 설치만, 로드 없음 [MEDIUM]

**위치**: `meson.build` (install), 부팅 시퀀스 (부재)

### I6. Recovery 모드 — 접근 경로 없음 [MEDIUM]

**위치**: `system/recovery/`

부트로더 진입, 하드웨어 키 조합, systemd target 전환 조건 미정의.

### I7. 멀티유저 — 스텁 [LOW→MEDIUM]

**위치**: `runtime/services/user/user.c`

### I8. GPS/GPSD 통합 없음 [MEDIUM]

**위치**: `runtime/services/location/location.c`

IP geolocation만. GPSD 연결 코드 없음.

### I9. 카메라 서비스 — 전무 [HIGH]

V4L2 캡처 없음. `runtime/services/camera/` 디렉토리 자체 부재.

### I10. 오디오 서비스 — 네이티브 구현 없음 [HIGH]

PipeWire/PulseAudio 통합 없음. `runtime/services/audio/` 부재.

### I11. Bluetooth — 연결 관리 없음 [MEDIUM]

페어링/프로필 관리 미구현. 서비스 디렉토리 부재.

### I12. WiFi — 연결 불가 [HIGH]

스캔만 가능. wpa_supplicant 제어 미구현.

### I13. 모뎀/전화 — ModemManager 통합 없음 [HIGH]

통화/SMS 불가. 상태 시뮬레이션만.

---

## A: Architecture (4건)

### A1. list_installed_apps — hidden 앱 권한 등록 우연적 동작 [MEDIUM]

**위치**: `emulator-app/src/commands/config.rs:219`, `apps/system/services.js:165`

### A2. 서비스 이중 경로 — JS/Rust/C 동기화 없음 [MEDIUM]

credential, notification 등이 두 곳에서 처리.

### A3. HAL 추상화 — 헤더만, 구현 없음 [MEDIUM]

**위치**: `runtime/hal/hal.h`

### A4. services.js — 28개 서비스 단일 파일 (SRP 위반) [MEDIUM]

**위치**: `apps/system/services.js` (~800줄)

---

## F: Fidelity (4건)

### F1. 센서 — 정적 노이즈 [LOW→MEDIUM]
### F2. 배터리 — 세 경로 다른 포맷 [MEDIUM]
### F3. 파일시스템 VFS 폴백 — 스토리지 초과 가능 [MEDIUM]
### F4. 시간 동기화 없음 [MEDIUM]

---

## B: Bug (6건)

### B1. 워치독 — hang된 Promise 카운트 영구 점유 [LOW→MEDIUM]
### B2. credential.c — 마스터 키 설정 진입점 없음 [HIGH]
### B3. appstore.c — system() 반환값 검사 불완전 [LOW→MEDIUM]
### B4. list_installed_apps — 경로 탐색 순서 문제 [MEDIUM]
### B5. OOBE → 홈 전환 시 PIN 상태 미완 [MEDIUM]
### B6. power_get_state — Rust/JS 밝기 비동기 [LOW→MEDIUM]

---

## X: Infrastructure (10건)

### X1. JS 앱 unit test 없음
### X2. Fuzzing 없음
### X3. 라이선스 감사 미검증
### X4. Reproducible build 미보장
### X5. Crash reporting 미구현
### X6. Telemetry 미구현
### X7. 접근성 테스트 없음
### X8. RTL 언어 지원 없음
### X9. DTS 보드 검증 미확인
### X10. CI/CD 파이프라인 내용 미확인

---

## 총계

| 심각도 | 건수 |
|--------|------|
| CRITICAL | 4 (S1, S2, S3, S5) |
| HIGH | 9 (S4, S6, S8, I1, I3, I9, I10, I12, I13, B2) |
| MEDIUM | 22 |
| **전체** | **45** |
