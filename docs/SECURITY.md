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

## 앱 패키지 서명 (봉인)

### 판정 (`runtime/services/appstore/app_seal.c`, 에뮬레이터는 `apps/system/services/app-seal.js`)
- 패키지마다 `.zyl-seal/seal.json`(모든 파일의 SHA-256) + RSA-SHA256 서명
- **공식 앱**: 스토어 키 봉인만 카탈로그로 설치
- **개발 빌드**: 스토어가 서명한 개발자 인증서(만료·폐기)와 프로비저닝 프로필
  (앱 id 패턴·기기 ID·만료)이 허락한 개발자 키 — 사용자 파일에서만 설치
- 파일 집합이 봉인과 정확히 같아야 함 (추가·삭제·변경·링크 → 거부)
- WAM 은 봉인이 맞는 사용자 앱만 등록하고, 파일을 내보낼 때마다 해시를
  봉인과 대조해 메모리에서 제공 (검사 시점과 사용 시점이 같음)
- 서명 우회(개발자 모드) 없음
- 패키지 해제: 절대 경로·`..`·링크·장치 파일 거부, 개수·크기 상한

### 서명 절차
```bash
./tools/zyl keygen acme                         # 개발자 키 (~/.zyl/keys)
python3 -I tools/build-store.py dev-cert …      # 스토어: 개발자 인증서
python3 -I tools/build-store.py profile …       # 스토어: 프로비저닝 프로필
./tools/zyl package --key … --cert … --profile …  # 개발자: 개발 빌드
python3 -I tools/build-store.py package --key store.key …   # 스토어: 공식 앱
```

---

## 프로세스 격리

### 앱 — WebKitGTK 샌드박스

앱 코드는 WAM 이 띄우는 WebKitGTK 웹 프로세스에서 돈다. 그 프로세스의 격리는 WebKit 이
직접 한다: bubblewrap 네임스페이스 + seccomp 시스콜 필터 + xdg-dbus-proxy.

- `webkitgtk-6.0`: 샌드박스가 **상시 ON** — 끄는 API 자체가 없다
- `webkit2gtk-4.1` 폴백: WAM 이 `webkit_web_context_set_sandbox_enabled()` 로 켠다
  (`runtime/wam/src/wam.c`)
- 이미지에 `bubblewrap`·`xdg-dbus-proxy` 가 없으면 격리가 **조용히 실패**한다 —
  [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) 의 필수 패키지

OS 가 자체 seccomp·네임스페이스 코드로 앱을 가두는 길은 쓰지 않는다. WAM 이 fork 하는
자식이 없어(웹 프로세스는 WebKit 이 띄운다) 걸 대상이 없고, 같은 일을 WebKit 이 이미
유지보수된 코드로 한다.

### 데몬 — systemd + AppArmor

OS 서비스는 프로세스마다 systemd 하드닝과 AppArmor 프로필로 격리한다
(`system/apparmor/zyl-*`). `zyl-apparmor.service` 가 부팅 때 `/etc/apparmor.d/zyl-*` 를
모두 로드하고 파서 오류면 부팅을 멈춘다 (fail closed).

---

## OTA 업데이트 검증

### updater.c
- **SHA-256 해시 무결성 검증** (다운로드 완료 후)
- **RSA-2048+SHA-256 서명 검증** (OpenSSL EVP API) — 중간자 공격 방지
- 검증 실패 시 업데이트 중단 — 검증을 통과하지 않은 패키지는 설치하지 않음
- 서버 통신은 HTTPS 필수
- 설치는 비활성 슬롯에만, 부트로더는 모든 기록이 끝난 뒤에야 새 슬롯을 시도.
  현재 부팅이 확정 전 시험 부팅이면 되돌아갈 슬롯을 덮지 않음
- 패키지 서명 키 = 부트 서명 키. 기기의 공개키는 `-Dota_public_key` 로 넣으며 없으면 업데이트 거부
- 새 슬롯의 부팅 자체는 U-Boot 의 FIT 서명 검증과 dm-verity 가 다시 확인

---

## Verified Boot

### 부트체인 무결성
각 단계가 다음 단계를 검증합니다. 키는 `tools/gen-boot-keys.sh` 가 만듭니다.

| 단계 | 누가 검증 | 키 | 만드는 곳 |
|---|---|---|---|
| FSBL(SPL) | Boot ROM — eFuse 에 루트 키 해시가 있을 때만 | `zylos-rotpk`(헤더) · `zylos-spl-key`(SPL) | `tools/build-bootloader.sh` |
| OpenSBI · U-Boot FIT | SPL (공개키가 FSBL 안 DTB 에 required) | `zylos-fw-key` | `tools/build-bootloader.sh` |
| 커널 · DTB · 램디스크 FIT | U-Boot (공개키가 U-Boot DTB 에 required) | `zylos-boot-key` | `tools/sign-image.sh` |
| rootfs | 커널 dm-verity (루트 해시는 서명된 커널 인자) | — | `tools/sign-image.sh` |
| 사용자 앱 | appstore · WAM 봉인 | 스토어 키 / 프로비저닝된 개발자 키 | `tools/build-store.py` |

- **부트 정책은 U-Boot 에 내장** (`board/bpi-f3/u-boot-env.txt`): 저장소의 환경에서는
  런타임 상태 변수만 읽고, 셸·자동 부팅 중단·fastboot 환경 접근·서명 없는(레거시·Android)
  이미지 부팅이 없다. 제조사 U-Boot 의 bootfs/네트워크 환경 가져오기·SD 자동 플래싱도 끈다
- **부팅 영역 쓰기 보호**: U-Boot 가 OS 로 넘기기 전에 eMMC 부트 파티션(FSBL)을 전원이
  꺼질 때까지 쓰기 금지 — 침해된 OS 가 부트로더를 영구히 바꿀 수 없다
- **기기 ID**: 칩이 공장에서 퓨즈한 ID (`serial-number`) — 개발 빌드 프로비저닝의 기준
- **eFuse (빌드 선택)**: `--burn-rotpk` 로 빌드한 부트로더만 루트 키 해시를 굽는다.
  굽지 않은 기기의 Boot ROM 은 FSBL 서명을 보지 않으므로, 기기를 손에 쥔 공격자는
  SD 카드·USB 로 자기 FSBL 을 띄울 수 있다 — [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md)
- **dm-verity**: rootfs 파티션 블록 단위 해시 트리 — 변조된 블록 읽기 시 커널 패닉 또는 읽기 전용 마운트
- 에뮬레이터 환경에서는 비적용 (실기기 전용)
- 슬롯별 서명 이미지: 커널 인자(슬롯 파티션, dm-verity 루트 해시, `zyl.slot`)가 서명되는 DTB 안에 있어 환경 변수로 바꿀 수 없다
- **부트로더 잠금 해제** (GPLv3 §6): 설정에서 허용 + 부팅 시 기기 버튼 확인 + `/data` 삭제 후에만 전환, 해제 상태는 부팅마다 경고 — [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md)

---

## 앱 격리

앱별 Linux UID 는 쓰지 않는다 — WAM 한 프로세스가 모든 웹뷰를 띄우므로 UID 로는 앱이
갈라지지 않는다. 갈라야 하는 지점을 각각 맡는 것:

- **프로세스**: WebKitGTK 샌드박스 (위 "프로세스 격리")
- **저장소**: 앱마다 `zyl-app://<id>/` 출처와 `/data/appdata/<id>/webkit/` 세션 —
  웹 저장소가 섞이지 않는다
- **파일**: 제3자 앱의 `fs` 는 자기 컨테이너가 루트 (브릿지가 채우는 `container` in-arg)
- **앱 간 데이터**: ContentProvider URI 경유, 권한은 `ZylContentProvider` 가 검사
- **서비스**: 호출 가능 서비스와 필요 권한은 OS 가 판정
  (`runtime/wam/src/bridge/bridge.c` 의 `SERVICE_PERMISSIONS`, 에뮬레이터는
  `apps/system/permissions.js`)
- **제거**: 컨테이너를 지우면 앱이 남긴 것이 하나도 남지 않는다 (`tests/store_e2e.sh`)

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

### 화면 잠금
- 기본 PIN 없음 (밀어서 잠금 해제). OOBE 에서 PIN 설정을 권장합니다
- PIN 은 `lock` 서비스(기기: `runtime/services/lock`)만 다룬다 — PBKDF2-SHA256 솔트 해시로만
  `/data/lock` 에 저장(0600), 잠금화면·설정·OOBE 는 검증·변경을 요청할 뿐 PIN 을 보관하지 않는다.
  설정 저장소는 `security.pin` 쓰기를 거부한다
- 실패 5회부터 30초 대기, 5회마다 두 배(최대 1시간). 재부팅·앱 재시작으로 초기화되지 않으며
  대기 중에는 맞는 PIN 도 판정하지 않는다
- 한계: 4자리 PIN 은 해시만으로 오프라인 대입을 막지 못한다 — TEE 결합 전까지 실제 방어선은
  대기 정책과 `/data/lock` 접근 제한

### 터미널 명령 필터링
- 에뮬레이터 터미널에서 22개 위험 명령 패턴을 Rust 백엔드에서 차단
- 차단 패턴: rm -rf /, mkfs, dd if=, sudo, su -, passwd, shutdown, reboot, halt, init 0/6, chmod -R 777, fork bomb, curl/wget 파이프, nc/ncat, /etc/shadow, /etc/passwd
- 마운트 포인트 내에서만 명령 실행 허용

### 시스템 앱 보호 (SYSTEM_APPS)
- 22개 시스템 앱은 App Store에서 삭제 불가
- 보호 대상: home, lockscreen, statusbar, oobe, settings, browser, files, camera, gallery, music, clock, calc, notes, weather, store, phone, messages, contacts, keyboard, shared
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

### OOM Killer
- `/proc/meminfo` 의 MemAvailable 비율을 5초마다 확인 (`runtime/wam/src/oom`)
- 25% 경고 로그 → 15% 에서 `zyl:lowmemory` 로 상태 저장 기회를 주고 0.5초 뒤 종료 →
  10% 에서 즉시 종료
- 포그라운드 앱은 보호, suspended 앱을 먼저, 그다음 LRU 순으로 닫는다
- 앱별 메모리 상한(cgroup `memory.max`)은 없다: 앱 코드는 WebKit 이 띄우는 웹
  프로세스에서 돌고 WAM 이 그 PID 를 알 길이 없다. 어느 앱을 닫을지가 WAM 이 쥔 지렛대다

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
- **권고**: 프로덕션 배포 시 `tools/build-bootloader.sh`·`tools/sign-image.sh`로 서명 필수
- **eFuse**: 루트 키 해시를 굽지 않은 기기는 물리 접근(SD·USB 부팅)으로 체인의 첫 단계를
  우회할 수 있다. 굽기는 되돌릴 수 없고, Boot ROM 이 기대하는 해시 형식을 제조사가 문서화하지
  않아 실기기에서 검증되지 않았다 — 버려도 되는 보드로 먼저 확인할 것

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
