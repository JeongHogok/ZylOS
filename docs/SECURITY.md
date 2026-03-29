# Zyl OS v0.1.0 보안 안내

## 보안 모델 개요

Zyl OS는 6계층 보안(namespace, seccomp, cgroup, network, D-Bus 정책, 앱 권한 시행)과
앱 서명 시스템을 통해 보안을 제공합니다.

### 서비스 아키텍처 보안

서비스 비즈니스 로직은 OS 이미지(`apps/system/services.js`)에 소유됩니다.
에뮬레이터는 순수 IPC 라우터로서 메시지 전달만 담당하며, 서비스 로직에 접근할 수 없습니다.

- **권한 시행** (`apps/system/permissions.js`): `ZylPermissions`가 모든 서비스 요청을 `app.json` 권한과 대조하여 미선언 권한은 즉시 차단
- **보안 관리** (`apps/system/security.js`): OS 레벨 보안 정책 관리

## v0.1.0 알려진 제한사항

### 인증 정보 저장소
- **현재**: 에뮬레이터에서 평문 JSON 저장 (Rust 백엔드); 실기기 C 서비스는 XOR 프로토타입
- **영향**: 저장된 비밀번호/토큰의 보안이 프로덕션 수준이 아님
- **권고**: 민감한 인증 정보는 외부 보안 저장소 사용 권장
- **계획**: v0.2.0에서 OpenSSL EVP_aes_256_gcm + PBKDF2 도입

### 앱 패키지 서명
- **현재**: RSA 서명 검증이 스텁 구현 (항상 통과)
- **영향**: 서명되지 않은 앱도 설치 가능
- **권고**: 신뢰할 수 있는 출처의 앱만 설치
- **계획**: v0.2.0에서 OpenSSL RSA-2048+SHA-256 실제 검증

### OTA 업데이트 검증
- **현재**: SHA-256 해시 검증만 수행, 서명 미검증
- **영향**: 중간자 공격으로 위조된 업데이트 설치 가능
- **권고**: 업데이트 서버 HTTPS 사용 필수
- **계획**: v0.2.0에서 업데이트 패키지 서명 검증

### seccomp 시스콜 필터
- **현재**: `PR_SET_NO_NEW_PRIVS`만 설정, BPF 필터 미적용
- **영향**: 앱이 위험한 시스콜 호출 가능
- **권고**: 시스템 앱만 실행 권장
- **계획**: v0.2.0에서 libseccomp 통합

## 보안 관련 설정

### 개발자 모드
- 개발자 모드 활성화 시 서명 검증이 우회됩니다
- 프로덕션 환경에서는 반드시 비활성화하세요

### 화면 잠금
- 기본 PIN: 0000
- 첫 부팅 시 OOBE에서 PIN 변경을 권장합니다

### 터미널 명령 필터링
- **현재**: 에뮬레이터 터미널에서 22개 위험 명령 패턴을 Rust 백엔드에서 차단
- **차단 패턴**: rm -rf /, mkfs, dd if=, sudo, su -, passwd, shutdown, reboot, halt, init 0/6, chmod -R 777, fork bomb, curl/wget 파이프, nc/ncat, /etc/shadow, /etc/passwd
- **적용**: 명령 실행 전 패턴 매칭, 마운트 포인트 내에서만 실행 허용

### 시스템 앱 보호 (SYSTEM_APPS)
- **현재**: 19개 시스템 앱은 App Store에서 삭제 불가
- **보호 대상**: home, lockscreen, statusbar, oobe, settings, browser, files, terminal, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts
- **적용**: OS 서비스(apps/system/services.js)에서 SYSTEM_APPS 리스트 확인 후 uninstall 차단

### 파일시스템 보호 (Rust 백엔드)
- **보호 대상**: `settings.json`, `.credentials/`, `.system/`
- **적용**: Rust 백엔드에서 fs 서비스의 파일 경로를 검사하여, 보호 대상 파일/디렉토리 접근을 차단
- `settings.json`: settings 서비스를 통해서만 접근 가능 (fs 서비스 직접 접근 차단)
- `.credentials/`: 자격증명 저장소 — credential 서비스를 통해서만 접근
- `.system/`: 시스템 설정 디렉토리 — 직접 접근 차단

### 앱 권한 시행 (ZylPermissions)
- **현재**: OS 이미지의 `apps/system/permissions.js`에서 실행 시점 권한 검증
- **적용**: 앱이 서비스를 호출할 때마다 `app.json`의 `permissions` 배열과 대조
- **차단**: 미선언 권한으로 서비스 호출 시 즉시 에러 응답, 요청 무시
- 이전 버전의 권고 수준 권한에서 **강제 시행**으로 변경

### 연락처/메시징 서비스 권한
- **contacts 서비스**: `contacts` 권한 필요 — 미선언 시 연락처 접근 차단
- **messaging 서비스**: `messaging` 권한 필요 — 미선언 시 메시지 접근 차단
- 두 서비스 모두 **높음** 위험도로 분류, 설치 시 명시적 사용자 승인 필요

### OOBE 격리
- OOBE 앱은 최근 앱(Recents) 목록에서 제외
- OOBE 진행 중 네비게이션(홈/백) 차단
- 전원 토글 시 잠금화면 표시하지 않음

## 보안 취약점 신고

보안 취약점을 발견하시면 security@zyl-os.com으로 보고해 주세요.
