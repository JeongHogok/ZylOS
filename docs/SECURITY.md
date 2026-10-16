# Zyl OS 보안 모델

이 문서는 Zyl OS 가 지키는 보안 불변식과, 각각을 **어디서 시행하는지**를 적습니다.
표·목록·수치는 그 사실을 가진 파일을 가리킵니다 ([CLAUDE.md](../CLAUDE.md) §8-1).

## 원칙

- **판정은 OS 가 한다.** 앱의 말(자기 id, 시스템 앱이라는 선언, 요청에 적은 신원)은 믿지
  않습니다. 에뮬레이터는 판정하지 않고 메시지를 나를 뿐입니다.
- **같은 규칙, 두 구현.** 기기(C, `runtime/`)와 에뮬레이터(JS, `apps/system/`)가 같은
  규칙을 따로 구현하고, 둘이 어긋나면 검사(`tests/verify-all.sh` 와 각 단위 테스트)가 막습니다.
- **모르면 닫는다.** 표에 없는 서비스, 서명 없는 패키지, 읽지 못한 설정은 허용이 아니라
  거부로 끝납니다.

## 1. 앱이 누구인지는 OS 가 말한다

- 보낸 앱은 메시지가 아니라 통로로 정합니다. 기기는 메시지가 온 웹뷰의 매니페스트,
  에뮬레이터는 메시지가 온 프레임(시스템 UI 프레임이면 시스템 UI, 아니면 앞에 있는 앱)입니다.
- 서비스에 넘기는 신원 인자는 요청 값을 버리고 OS 가 채웁니다: `caller` 는 언제나 부른 앱,
  `container` 는 시스템 앱이면 `""`(사용자의 공유 파일) 아니면 부른 앱, `app_id`(에뮬레이터
  에서는 `appId`)는 제3자 앱이면 부른 앱 자신 — 시스템 앱만 다른 앱을 가리킬 수 있습니다.
  시행: `runtime/wam/src/bridge/bridge.c` 의 `params_to_dbus_args`, 에뮬레이터
  `apps/system/services.js`·`services/fs.js`.
- 시스템 앱인지도 OS 가 정합니다. 기기는 검증된 시스템 파티션의 앱 디렉터리
  (`/usr/share/zyl-os/apps`)에 있는지로, 에뮬레이터는 `ZylPermissions.SYSTEM_APPS` 로 봅니다.
  두 집합(`meson.build` 가 이미지에 설치하는 앱과 `SYSTEM_APPS`)이 같은지는
  `tests/verify-all.sh` 가 봅니다. 사용자 앱은 시스템 앱의 id 를 쓸 수 없습니다
  (`runtime/wam/src/manifest/manifest.c`, 스토어의 `system_app` 거부).

## 2. 서비스로 가는 길은 하나다

- 기기에서 서비스에 닿는 앱 메시지는 `service.request` 하나뿐이고, 언제나 라우트 표 →
  권한 표 → 시스템 버스 순서로 지납니다 (`bridge.c`). 브리지가 받는 메시지 형식은
  `runtime/wam/src/wam.c` 와 `bridge.c` 의 `zyl_bridge_register_handler` 호출이 전부입니다.
  `service.request` 밖의 것은 WAM 이 직접 처리하는 화면 동작이고, 누가 쓸 수 있는지는
  셸 정책(4장)이 정합니다. 없앤 지름길(`wifi.scan`·`battery.getLevel`)이 알 수 없는 메시지
  형식으로 끝나는지는 `tests/test_bridge_dispatch.c` 가 확인합니다.
- 라우트가 없는 서비스는 알 수 없는 서비스로, 라우트는 있는데 권한 항목이 없는 서비스는
  거부로 끝납니다. `verify-all` 이 라우트마다 권한 항목이 있는지 확인합니다.
- 앱은 버스에 직접 닿지 않습니다. 시스템 버스 정책(`system/dbus-1/zyl-os.conf`)은
  `org.zylos.*` 이름의 소유·호출을 기본 거부하고 서비스 데몬(root)과 WAM(`zyl-wam`)만
  허용합니다. 검증: `tests/test_dbus_policy.sh`.

## 3. 권한

권한은 앱이 `app.json` 의 `permissions` 로 선언하고, OS 가 요청마다 판정합니다. 단일
출처는 `apps/system/permissions.js` 이고, 기기의 `bridge.c` 는 그 사본입니다. 두 쪽의
아래 표가 어긋나면 `tests/verify-all.sh` 가 실패합니다.

| 표 | 뜻 |
|----|----|
| `SERVICE_PERMISSIONS` | 서비스마다 필요한 권한 — 하나라도 가지면 부를 수 있고, 빈 목록은 권한 없이 부릅니다. 표에 없는 서비스는 닫혀 있습니다 |
| `METHOD_PERMISSIONS` | 자기 것 너머에 닿는 메서드(다른 앱의 알림, 기기 전체의 방해 금지 등)에 더 필요한 권한 |
| `OWNED_EVENTS` | 한 앱의 것에 관한 이벤트는 그 주인에게만 갑니다 — 주인은 시그널의 두 번째 인자이고, 주인을 밝히지 않는 이벤트는 아무에게도 가지 않습니다. 모두 읽을 권한(`readAll`)을 가진 앱은 예외 |
| `RESERVED_PERMISSIONS` | OS 만 갖는 권한. 제3자 앱이 선언해도 주어지지 않습니다 |

- 시스템 앱은 모든 권한을 가지며, 그 권한은 끌 수 없습니다.
- 서비스 이벤트는 그 서비스를 부를 수 있는 앱에만 갑니다 (`ZylPermissions.mayReceive`,
  `bridge.c` 의 `zyl_bridge_service_event_script`).
- **사용자가 끈 권한**은 두 게이트 모두에서 거부합니다. 설정 앱이 settings 서비스의
  `app_permissions` 범주(앱 id 마다 끈 권한의 쉼표 목록)에 쓰면, 에뮬레이터는
  `ZylPermissions.setAppOverride` 로, 기기는 WAM 이 `CategoryChanged` 시그널을 따라 다시 읽어
  다음 호출부터 거부합니다.
- 첫 사용 때 묻는 동의 창은 없습니다. 넣을 자리는 [CLAUDE.md](../CLAUDE.md) 규칙 8.

## 4. 셸 정책 — 무엇이 앞에 오고, 누가 잠금을 푸는가

같은 규칙을 기기 `runtime/wam/src/shell/shell.c`(테스트 `tests/test_shell.c`)와 에뮬레이터
`apps/system/shell.js` 가 구현합니다.

- 첫 설정이 끝나기 전에는 첫 설정 앱(OOBE)만, 잠긴 동안에는 잠금 화면만 앞에 옵니다. 그
  위에 뜰 수 있는 몇몇 앱(통화·긴급 경보·잠금 화면의 카메라 바로가기)은 셸 정책이 정하며,
  잠긴 동안 다른 앱을 열 수 없습니다.
- **잠금 해제(`unlock`)는 잠금 화면만, 설정 완료(`setup.done`)는 첫 설정 앱만** 보낼 수
  있습니다. 두 앱의 id 는 `runtime/wam/src/shell/shell.h` 에 있고, 보낸 앱은 OS 가
  정합니다(1장).
- 화면이 꺼지면 잠깁니다. 기기는 설정을 읽지 못하면 열린 채가 아니라 잠긴 채로 시작합니다.
- 시스템 UI(상태바·알림판·최근 앱)의 표면 크기는 시스템 UI 만 요청할 수 있고, 전체 화면은
  사용자가 앱 사이를 오갈 수 있을 때만, 새 알림 배너는 잠금·첫 설정 중이 아닐 때만 줍니다.

### 화면 잠금 PIN

- PIN 은 `lock` 서비스(기기 `runtime/services/lock`, 에뮬레이터 `services/lock.js`)만 다룹니다.
  잠금 화면·설정·첫 설정은 검증과 변경을 요청할 뿐 PIN 을 보관하지 않습니다.
- 기기는 PIN 을 솔트를 넣은 해시로만 `/data/lock` 에 둡니다(root 전용). 비교는 상수 시간,
  변경·제거는 현재 PIN 확인 뒤입니다.
- 실패가 쌓이면 대기가 늘어납니다. 대기는 재부팅으로 초기화되지 않고, 대기 중에는 맞는 PIN
  도 판정하지 않습니다.
- 해시 방식·저장 형식·대기 수치는 `runtime/services/lock/lock.c` 의 머리와 상수가 정합니다.
- settings 서비스는 `security.pin` 쓰기를 거부합니다.
- 한계: 하드웨어 키 결합이 없으므로 짧은 PIN 은 해시를 손에 넣으면 오프라인 대입을 막지
  못합니다. 방어선은 대기 정책과 `/data/lock` 접근 제한입니다.

## 5. 앱 컨테이너 — 지우면 아무것도 남지 않는다

- 앱이 만드는 모든 것은 컨테이너 `/data/appdata/<앱 id>/` 에만 있습니다: 비공개 데이터
  (`appdata` — 키는 `caller` 로만 정해져 다른 앱의 것은 이름조차 댈 수 없음), 파일(`fs` —
  제3자 앱은 자기 컨테이너의 `files/` 가 루트), 웹 저장소(WAM 이 앱마다 출처
  `zyl-app://<id>/` 와 `webkit/` 세션을 따로 둠). 에뮬레이터의 `fs` 는 가상 디스크의 보호 경로
  (`settings.json`·`.credentials/`·`.system/`)를 거부합니다 (`apps/system/security.js`).
- 앱별 Linux UID 는 쓰지 않습니다. WAM 한 프로세스가 모든 웹뷰를 띄우므로 UID 로는 앱이
  갈라지지 않고, 위의 출처·세션·컨테이너·신원 인자가 그 역할을 나눠 맡습니다.
- 삭제는 앱을 닫고 패키지와 컨테이너를 지운 뒤 `AppRemoved` 를 보냅니다. 앱별 상태를 가진
  서비스는 그 시그널(기기) 또는 `_forgetApp`(에뮬레이터)으로 그 앱의 것을 지웁니다.
  삭제 뒤 흔적이 없는지는 `tests/store_e2e.sh`(appstore·appdata·fs·notification 데몬)와 각
  서비스의 테스트(`tests/test_settings.c` 등)가 확인합니다. 규칙은 [CLAUDE.md](../CLAUDE.md) 규칙 12.

## 6. 앱 봉인과 스토어 서명

- 사용자 앱은 봉인(`.zyl-seal/`)이 맞을 때만 설치·등록·실행됩니다. 공식 앱은 스토어 키,
  개발 빌드는 스토어가 서명한 개발자 인증서와 프로비저닝 프로필이 허락한 기기·앱에서만.
- 판정은 기기 `runtime/lib/app_seal.c`, 에뮬레이터 `apps/system/services/app-seal.js` 두
  곳뿐이고 규칙이 같습니다 (`tests/test_app_seal.c`, `tests/test_services.js`).
- 기기의 WAM 은 사용자 앱의 파일을 내줄 때마다 해시를 봉인과 대조하고, 검사한 바이트를
  그대로 메모리에서 내줍니다 — 검사한 것과 실행하는 것이 같습니다. 시스템 앱과 OS 코드
  (`/shared/`·`/keyboard/`)는 dm-verity 아래에 있습니다.
- WAM 은 앱을 등록할 때마다(시작할 때, 설치 직후) 봉인을 지금 시각과 스토어의 폐기 목록으로
  다시 확인합니다.
- 봉인을 건너뛰는 경로(개발자 모드 등)는 없습니다.
- 카탈로그·패키지·봉인·인증서·프로필의 형식과 거부 사유: [APPSTORE_API.md](APPSTORE_API.md).

## 7. 기기의 프로세스 격리

**앱 — WebKit 샌드박스.** 앱 코드는 WebKitGTK 웹 프로세스에서 돌고, 그 격리는 WebKit 이
직접 합니다 (bubblewrap 네임스페이스 + seccomp + xdg-dbus-proxy). `webkitgtk-6.0` 은 늘
켜져 있고, `webkit2gtk-4.x` 로 빌드하면 WAM 이 켭니다 (`runtime/wam/src/wam.c`).
`bubblewrap`·`xdg-dbus-proxy` 가 이미지에 없으면 격리가 조용히 꺼지므로 둘은
`system/packages.list` 에 있습니다. OS 가 자체 seccomp·네임스페이스 코드로 앱을 가두지는
않습니다 — WAM 이 앱 프로세스를 직접 띄우지 않고, 같은 일을 WebKit 이 합니다.

**데몬 — systemd 와 AppArmor.**

- 각 유닛(`system/*.service`)이 systemd 샌드박스 지시어로 자기를 좁힙니다. 기본은
  `ProtectSystem=strict` 에 쓰기 경로를 `ReadWritePaths=` 로만 여는 것이고, 유닛마다의
  지시어와 예외의 이유는 그 유닛 파일에 적혀 있습니다. 서비스 데몬은 root, WAM 은 전용
  사용자 `zyl-wam` 으로 돕니다.
- AppArmor 프로필은 `system/apparmor/zyl-*` 에 있고, `zyl-apparmor.service` 가 부팅 때 모두
  로드합니다. 프로필 하나라도 로드에 실패하면 그 유닛이 실패합니다.
- `tests/verify-all.sh` 가 유닛의 쓰기 경로를 두 가지로 대조합니다: 모두 `tmpfiles.d` 로
  만들어지는지, 그 서비스의 AppArmor 프로필이 그 경로에 쓰기를 허용하는지.

## 8. 키체인

- `credential` 서비스(기기 `runtime/services/credential/credential.c`)가 비밀을 암호화해
  저장합니다. `credential` 은 예약 권한이라 시스템 앱만 부릅니다.
- 항목마다 파일 하나로 인증 암호화하고, 항목 키는 마스터 키와 항목의 무작위 솔트에서
  파생합니다. 마스터 키는 기기 결합 키에서 파생합니다. 기기 결합 키는 첫 시작 때 만든
  무작위 바이트로 저장소 디렉터리(root 전용)에 있고, 저장소는 시작하면 사용자 입력 없이
  열립니다. 있는데 읽을 수 없는 키 파일은 새로 만들지 않습니다 — 저장된 비밀을 모두 잃게
  되기 때문입니다.
- 암호 방식·파일 형식·키 파생은 `credential.c` 의 머리가 정합니다.
- 한계: 키가 저장소와 같은 곳에 있으므로 지키는 것은 그 디렉터리의 접근 제한입니다.
- 에뮬레이터는 같은 방식을 컨테이너(`emulator-app/src/commands/emulated_services.rs`)가
  구현합니다.

## 9. 검증 부팅 (Verified Boot)

각 단계는 다음 단계의 서명을 검증한 뒤에만 넘깁니다. 키는 `tools/gen-boot-keys.sh` 가 만듭니다.

| 단계 | 누가 검증 | 키 | 만드는 곳 |
|---|---|---|---|
| FSBL (SPL) | Boot ROM — eFuse 에 루트 키 해시가 있을 때만 | `zylos-rotpk`(헤더), `zylos-spl-key`(SPL) | `tools/build-bootloader.sh` |
| OpenSBI · U-Boot FIT | SPL (공개키가 SPL DTB 에 `required`) | `zylos-fw-key` | `tools/build-bootloader.sh` |
| 커널 · DTB · initramfs FIT | U-Boot (공개키가 U-Boot DTB 에 `required`) | `zylos-boot-key` | `tools/sign-image.sh` |
| rootfs | 커널 dm-verity (루트 해시는 서명된 커널 인자) | — | `tools/sign-image.sh` |
| 사용자 앱 | appstore · WAM 봉인 (6장) | 스토어 키 / 허락된 개발자 키 | `tools/build-store.py` |

**불변식**: 부트 정책(`board/bpi-f3/u-boot-env.txt`, 검증 `tests/test_boot_env.sh`)은
부트로더에 내장되어 바깥에서 바꿀 수 없습니다. OS 이미지의 검증을 끄는 길은 소유자가 기기에서
확인하는 잠금 해제(GPLv3 §6) 하나이고, 잠금 상태가 바뀔 때마다 `/data` 가 지워집니다.
부트로더가 무엇을 막는지, U-Boot 명령 줄이 언제 남는지, 잠금 해제 절차, eFuse 굽기는
[INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) 가 정합니다.

에뮬레이터는 기기가 아니므로 검증 부팅이 없습니다.

## 10. OTA

- 패키지(`tools/build-ota.sh`)는 패키지 SHA-256 에 대한 RSA-SHA256 서명을 가집니다. 서명
  키는 부트 서명 키(`zylos-boot-key`)입니다 — 부팅 이미지를 서명할 수 있는 키가 곧 업데이트를
  낼 수 있는 키입니다.
- **받을 조건**: U-Boot 환경을 읽을 수 있고(`fw_printenv`), 공개키
  `/etc/zyl-os/ota-signing-key.pem`(빌드 옵션 `-Dota_public_key`)이 이미지에 있을 것.
  하나라도 없으면 업데이트를 받지 않습니다.
- **검증**: 서명이 없거나 맞지 않거나 해시가 다른 패키지는 설치하지 않습니다
  (`runtime/lib/ota_verify.c`). 서버 주소는 https 만 받습니다. 전체 이미지 패키지만
  설치합니다 — 델타·앱만·커널만 패키지는 dm-verity rootfs 와 맞출 수 없어 거부합니다.
- **설치**: 비활성 슬롯에만 rootfs → verity 트리 → FIT 순서로 씁니다(FIT 는 임시 이름으로
  쓰고 교체). 지금 부팅이 확정 전 시험 부팅이면 설치하지 않습니다 — 돌아갈 슬롯을 덮지
  않기 위해서입니다 (`runtime/services/updater`).
- **전환**: 모든 기록이 끝난 뒤에만 `zyl_next_slot` 을 지정합니다. 새 슬롯의 부팅은 U-Boot
  의 FIT 서명과 커널의 dm-verity 가 다시 확인합니다.
- **확정과 롤백**: 부팅 뒤 업데이터는 컴포지터와 WAM 이 떠 있으면 슬롯을 확정하고, 아니면
  재시작합니다. 부트 정책이 정한 횟수(`u-boot-env.txt` 의 `zyl_boot_limit`) 안에 확정되지
  않으면 U-Boot 가 이전 슬롯으로 돌아갑니다.
- 패키지를 만들고 이미지를 준비하는 절차는 [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#6-ota).

## 보안 취약점 신고

보안 취약점을 발견하시면 security@zyl-os.com 으로 보고해 주세요.
