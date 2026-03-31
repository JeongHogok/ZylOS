# Zyl OS v0.1.0 보안 안내

## 보안 모델 개요

Zyl OS는 7계층 보안(Verified Boot, namespace, libseccomp BPF, cgroup, network, D-Bus 정책, 앱 권한 시행)과
OpenSSL 기반 암호화, 앱 서명 시스템을 통해 보안을 제공합니다.

### 서비스 아키텍처 보안

서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js` 라우터 + `services/*.js` 모듈)에 소유됩니다.
에뮬레이터는 순수 IPC 라우터로서 메시지 전달만 담당하며, 서비스 로직에 접근할 수 없습니다.

- **권한 시행** (`apps/system/permissions.js`): `ZylPermissions`가 모든 서비스 요청을 `app.json` 권한과 대조하여 미선언 권한은 즉시 차단
- **보안 관리** (`apps/system/security.js`): OS 레벨 보안 정책 관리

---

## 인증 정보 저장소

### 실기기 C 서비스 (credential.c)
- **AES-256-GCM** 암호화 (OpenSSL EVP API)
- **PBKDF2-HMAC-SHA256**, 100,000 iterations 키 파생
- 저장 포맷: `[salt(16B)][iv(12B)][ciphertext(N)][tag(16B)]`
- 마스터 키 파생: 사용자 PIN → PBKDF2 → 32바이트 마스터 키
- 파일별 독립 키: 마스터 키 + 파일별 랜덤 salt → PBKDF2 → per-file 키

### 에뮬레이터 Rust 백엔드
- **aes-gcm** + **pbkdf2** Rust crate 사용
- 실기기 C 구현과 동일한 암호화 스킴

---

## 앱 패키지 서명

### 서명 검증 (appstore.c)
- **RSA-2048+SHA-256** 서명 검증 (OpenSSL EVP API)
- 개발자 인증서 신뢰 저장소 (`trust_store_path`)
- 서명 없는 앱 설치 차단 (개발자 모드 예외)
- **ZIP 매직 바이트 검증**: PK\x03\x04 확인
- **경로 순회 공격 탐지**: `..` 포함 파일명 스캔, 설치 전 차단

### 서명 절차
```bash
# 개발자 키 생성
./tools/zyl keygen "My Name"   # 또는

# 개인 키 서명
openssl dgst -sha256 -sign developer.key -out SIGNATURE manifest.hash

# zyl SDK로 패키징
./tools/zyl sign developer.key developer.crt
```

---

## seccomp 시스콜 필터

### libseccomp BPF 3프로필 (sandbox.c)

| 프로필 | 대상 | 설명 |
|--------|------|------|
| **STRICT** | 서드파티 앱 | DEFAULT 규칙 + 추가 제한 |
| **DEFAULT** | 일반 시스템 앱 | 위험 시스콜 차단 (기본 집합, ~46개 규칙 총합) |
| **PERMISSIVE** | 권한 있는 시스템 서비스 | 최소 규칙만 적용 |

차단 대상 시스콜 예시 (DEFAULT):
`ptrace`, `mount`, `umount2`, `reboot`, `kexec_load`, `init_module`, `delete_module`,
`finit_module`, `pivot_root`, `swapon`, `swapoff`, `acct`, `settimeofday`, `clock_settime`,
`adjtimex`, `personality`, `unshare`, `setns`, `keyctl`, `add_key`, ...

모든 프로필에서 `PR_SET_NO_NEW_PRIVS` 설정.

---

## OTA 업데이트 검증

### updater.c
- **SHA-256 해시 무결성 검증** (다운로드 완료 후)
- **RSA-2048+SHA-256 서명 검증** (OpenSSL EVP API) — 중간자 공격 방지
- 검증 실패 시 업데이트 중단, 비활성 파티션 클린업
- 서버 통신은 HTTPS 필수

---

## Verified Boot

### 부트체인 무결성
- **FIT 이미지 서명**: 커널 + DTB + 램디스크를 하나의 FIT 이미지로 묶어 RSA-2048 서명
- **dm-verity**: rootfs 파티션 블록 단위 해시 트리 — 변조된 블록 읽기 시 커널 패닉 또는 읽기 전용 마운트
- 부트 키 생성: `tools/gen-boot-keys.sh`
- 이미지 서명 + verity 생성: `tools/sign-image.sh`
- 에뮬레이터 환경에서는 비적용 (실기기 전용)

---

## 앱 격리

### 앱별 UID
- 각 앱은 독립된 Linux UID로 실행 (`user` 서비스의 `zyl_user_get_app_uid()`)
- 앱 간 파일 접근은 UID 기반으로 커널에서 차단
- 앱 제거 시 해당 UID의 모든 리소스 정리

### 프로세스 격리
- **mount namespace**: 앱별 독립 파일시스템 뷰
- **network namespace**: 권한 없으면 네트워크 차단
- **cgroup v2**: 메모리/CPU/PID 제한

---

## 런타임 권한 (ZylPermissionDialog)

위험 권한(camera, location, contacts, messaging, telephony, storage, microphone, bluetooth)은
앱이 처음 사용을 시도할 때 OS 수준 다이얼로그를 통해 사용자 승인이 필요합니다.

- `ZylPermissionDialog.requestPermission(appId, permission)` → `Promise<boolean>`
- 승인된 권한은 `ZylPermissions` 런타임 상태에 기록
- 설정 앱에서 언제든 권한 취소 가능
- 취소된 권한으로 서비스 호출 시 즉시 에러 응답

---

## 보안 관련 설정

### 개발자 모드
- 개발자 모드 활성화 시 앱 서명 검증이 우회됩니다
- 프로덕션 환경에서는 반드시 비활성화하세요

### 화면 잠금
- 기본 PIN: 0000
- 첫 부팅 시 OOBE에서 PIN 변경을 권장합니다

### 터미널 명령 필터링
- 에뮬레이터 터미널에서 22개 위험 명령 패턴을 Rust 백엔드에서 차단
- 차단 패턴: rm -rf /, mkfs, dd if=, sudo, su -, passwd, shutdown, reboot, halt, init 0/6, chmod -R 777, fork bomb, curl/wget 파이프, nc/ncat, /etc/shadow, /etc/passwd
- 마운트 포인트 내에서만 명령 실행 허용

### 시스템 앱 보호 (SYSTEM_APPS)
- 22개 시스템 앱은 App Store에서 삭제 불가
- 보호 대상: home, lockscreen, statusbar, oobe, settings, browser, files, terminal, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts, keyboard, shared
- OS 서비스(`apps/system/services.js`)에서 SYSTEM_APPS 리스트 확인 후 uninstall 차단

### 파일시스템 보호 (Rust 백엔드)
- `settings.json`: settings 서비스를 통해서만 접근 (fs 서비스 직접 접근 차단)
- `.credentials/`: credential 서비스를 통해서만 접근
- `.system/`: 직접 접근 차단

### 앱 권한 시행 (ZylPermissions)
- `apps/system/permissions.js`에서 실행 시점 권한 검증
- 앱이 서비스를 호출할 때마다 `app.json`의 `permissions` 배열과 대조
- 미선언 권한으로 서비스 호출 시 즉시 에러 응답

### 웨이크락 타임아웃
- 웨이크락 최대 보유 시간: 600초
- 타임아웃 시 자동 release (power 서비스 타이머 관리)
- 무한 웨이크락으로 인한 배터리 소진 방지

### OOM Killer (cgroup v2)
- cgroup v2 `memory.max` 기반 앱별 메모리 상한 적용
- `memory.pressure` 이벤트 기반 Low Memory Killer (LMK)
- 백그라운드 앱 → 캐시 앱 → 사용자 앱 순으로 OOM 처리
- 앱별 UID + cgroup v2 조합으로 메모리 격리 강제

### systemd 서비스 보안 하드닝
- 25개 systemd 서비스 유닛에 보안 하드닝 전체 적용
- `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateTmp=yes`
- `NoNewPrivileges=yes`, `CapabilityBoundingSet=` 최소 권한
- `SystemCallFilter=@system-service` seccomp 필터
- 서비스별 `ReadWritePaths=` 최소 범위 지정

### OOBE 격리
- OOBE 앱은 최근 앱(Recents) 목록에서 제외
- OOBE 진행 중 네비게이션(홈/백) 차단
- 전원 토글 시 잠금화면 표시하지 않음

### 연락처/메시징 서비스 권한
- `contacts` 서비스: `contacts` 권한 필요 — 미선언 시 연락처 접근 차단
- `messaging` 서비스: `messaging` 권한 필요 — 미선언 시 메시지 접근 차단
- 두 서비스 모두 **높음** 위험도로 분류, 설치 시 명시적 사용자 승인 필요

---

## v0.1.0 알려진 제한사항

### Verified Boot
- **현재**: 실기기 전용. 에뮬레이터 환경에서는 FIT 서명/dm-verity 비적용
- **영향**: 에뮬레이터에서 OS 이미지 변조 감지 불가
- **권고**: 프로덕션 배포 시 `tools/sign-image.sh`로 서명 필수

### 생체 인증 (auth 서비스)
- **현재**: auth 서비스는 구현되어 있으나 BPI-F3 지문 센서 하드웨어 연동은 BSP 드라이버에 의존
- **영향**: 지문 인증은 드라이버 통합 전까지 PIN 폴백만 동작
- **계획**: 공식 SpacemiT BSP 지문 드라이버 릴리즈 후 통합

### NFC (nfc 서비스)
- **현재**: Linux libnfc 연동 구현 완료, 실기기 NFC 하드웨어 테스트 미완료
- **영향**: NFC 기능 실기기 동작 미보장
- **권고**: 테스트된 NFC 하드웨어 구성 확인 후 사용

### Telemetry
- **현재**: 기본 비활성화
- **영향**: 없음
- **정책**: 사용자 명시적 동의 후에만 활성화

---

## 보안 취약점 신고

보안 취약점을 발견하시면 security@zyl-os.com으로 보고해 주세요.
