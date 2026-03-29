# Zyl OS v0.1.0 보안 안내

## 보안 모델 개요

Zyl OS는 5계층 샌드박싱(namespace, seccomp, cgroup, network, D-Bus 정책)과
앱 서명 시스템을 통해 보안을 제공합니다.

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
- **현재**: 16개 시스템 앱은 App Store에서 삭제 불가
- **보호 대상**: home, lockscreen, statusbar, oobe, settings, browser, files, terminal, camera, gallery, music, clock, calc, notes, weather, store
- **적용**: 서비스 라우터에서 SYSTEM_APPS 리스트 확인 후 uninstall 차단

## 보안 취약점 신고

보안 취약점을 발견하시면 security@zyl-os.com으로 보고해 주세요.
