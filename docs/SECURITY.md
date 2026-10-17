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
| `METHOD_PERMISSIONS` | 자기 것 너머에 닿는 메서드(다른 앱의 알림, 알림 액션 누르기, 기기 전체의 방해 금지, 전원 — 화면·절전·종료·재부팅·웨이크락, 기기 설정 바꾸기 — USB 모드·화면·접근성·입력기, 로그, 셸 — 홈·뒤로·패널·최근 앱·앱 전환, 앱 목록 `app.list`·다른 앱 띄우기 `app.launch`)에 더 필요한 권한. 메서드 이름은 첫 글자의 대소문자와 관계없이 같은 규칙에 걸립니다(D-Bus 이름이 같으므로) |
| `OWNED_EVENTS` | 한 앱의 것에 관한 이벤트는 그 주인에게만 갑니다 — 주인은 시그널의 두 번째 인자이고, 주인을 밝히지 않는 이벤트는 아무에게도 가지 않습니다. 모두 읽을 권한(`readAll`)을 가진 앱은 예외 |
| `RESERVED_PERMISSIONS` | OS 만 갖는 권한. 제3자 앱이 선언해도 주어지지 않습니다 |

- 시스템 앱은 모든 권한을 가지며, 그 권한은 끌 수 없습니다.
- **클립보드는 앞에 있는 앱의 것**입니다 — 기기는 여러 앱이 살아 있어, 뒤에 있는 앱은 읽지도
  바꾸지도 못합니다(`bridge.c` 의 `FRONT_ONLY_SERVICES`, WAM 의 셸이 앞 앱을 답함). 에뮬레이터는
  한 번에 한 앱만 살아 있어 그 앱이 늘 앞입니다.
- **캡처도 앞에 있는 앱의 것**입니다 — 카메라·위치·센서 서비스와 그 흐름(위치 갱신·센서 값)은
  앞 앱만 받습니다(제3자 앱의 지오펜스처럼 그 앱 자신의 이벤트는 예외). 화면에서 밀려나거나
  잠금 화면 뒤로 간 앱의 WebKit 카메라·마이크는 WAM 이 멈추고(MUTED) 돌아오면 되돌리며,
  뒤에 있는 앱의 새 `getUserMedia` 는 거부합니다. 무엇이든 카메라·마이크를 쓰고 있으면 상태바가
  표시등을 켭니다(`capture.state`).
- 서비스 이벤트는 그 서비스를 부를 수 있는 앱에만 갑니다 (`ZylPermissions.mayReceive`,
  `bridge.c` 의 `zyl_bridge_service_event_script`).
- **사용자가 끈 권한**은 두 게이트 모두에서 거부합니다. 설정 앱이 settings 서비스의
  `app_permissions` 범주(앱 id 마다 끈 권한의 쉼표 목록)에 쓰면, 에뮬레이터는
  `ZylPermissions.setAppOverride` 로, 기기는 WAM 이 `CategoryChanged` 시그널을 따라 다시 읽어
  다음 호출부터 거부합니다.
- **위험 권한은 처음 쓸 때 묻습니다** — 카메라·마이크·위치·연락처·메시지·전화(`calls`)·
  문자(`sms`) (`DANGEROUS_PERMISSIONS`). 요청은 답을 기다리고, 허용이면 진행, 거부면
  `Permission denied` 입니다. 묻는 창은 시스템 UI(`apps/statusbar`)가 그리고 그 답만 받습니다
  (`permission.answer`, 질문마다의 토큰) — 앱 자기 화면에 그리면 브리지가 앱과 같은 JS 세계에
  있어 앱이 스스로 허용을 보낼 수 있습니다. 앞에 있지 않은 앱의 질문은 띄우지 않고, 답이 없으면
  2분 뒤 이번만 거부합니다. 답은 settings 에 남습니다(허용 `app_grants`, 거부 `app_permissions`) —
  이벤트(위치 갱신·`SmsReceived` 등)도 허용된 뒤에만 갑니다. 기기는 WAM(`wam.c` 의 consent,
  `getUserMedia` 포함), 에뮬레이터는 `apps/system/consent.js`.
- 전화 권한은 셋입니다 — `telephony`(망 상태), `calls`(걸기·받기·통화 기록), `sms`(보내기·받기).
  재난문자·모바일 데이터 설정·IMEI(`telephony.getImei`)는 시스템 전용입니다.
- **사용자의 스위치는 그 일을 하는 서비스가 지킵니다.** 빠른 설정은 settings 에 쓰기만 하고,
  서비스가 그 범주를 따라갑니다(`runtime/lib/settings_follow.c`). 위치(`location.enabled`)를
  끄면 location 서비스는 공급자(GPS·Wi-Fi 조회·GeoIP)에 묻지 않고, 마지막 위치를 잊고, 앱에는
  provider `off` 만 줍니다(에뮬레이터 `services/location.js` 도 같게). 비행기 모드
  (`connectivity.airplaneMode`)면 telephony 가 모뎀을 끕니다 — 사용자가 거는 통화 동안만
  켭니다(긴급전화는 늘 나가야 합니다).
- 칩 ID(`deviceId`)는 공장 초기화로도 바뀌지 않아 `device.getInfo` 에 싣지 않고, 시스템 앱만
  부르는 `device.getDeviceId` 로만 줍니다.

### 웹 플랫폼의 권한 요청

페이지가 WebKit 에 묻는 것도 앱 권한으로 정합니다 — 묻는 창은 없습니다. 기기는 WAM 의
`on_permission_request`(`wam.c`), 에뮬레이터는 앱 iframe 의 Permissions Policy
(`sandbox.js` 의 `getPolicy`)로, 같은 규칙입니다.

| 요청 | 판정 |
|---|---|
| `getUserMedia` 카메라 / 마이크 | `camera` / `microphone` 권한 (둘 다 요청하면 둘 다) |
| 장치 목록의 이름 | `camera` 나 `microphone` 중 하나 |
| 화면 공유, 위치(`navigator.geolocation`), 웹 알림, 포인터 잠금 등 | 거부 — OS 서비스(`location`, `notification`)를 쓴다 |
| `<input type=file>` | 열지 않음 — 데스크톱 대화창이 컨테이너 밖 경로를 건넬 수 있다. 파일은 PICK (§5) |

### 푸시 — 앱 서버가 닿는 길

- 앱마다 토픽 `up` + 영숫자 12자(약 71비트)를 OS 가 만들고, 끝점이 곧 비밀이다 — 아는 쪽만
  그 앱에 보낼 수 있다. 등록·해제는 `caller`(브리지가 채우는 호출 앱)로만, 받은 메시지
  (`PushReceived`)는 그 앱에만 갑니다(`OWNED_EVENTS`).
- 서버(`system/push.json`, https 만)는 본문을 그대로 전달하므로 내용을 볼 수 있습니다 —
  민감한 것은 앱이 암호화하거나 신호만 보냅니다. 운영은 [PUSH_SERVER.md](PUSH_SERVER.md).
- 지운 앱의 등록은 `AppRemoved` 로, 공장 초기화는 `/data/push` 를 지웁니다 — 이전 주인 앱의
  메시지가 계속 오지 않게.

### 앱이 닿는 곳 — CSP

앱 파일은 OS 가 권한으로 만든 Content-Security-Policy 와 함께 나갑니다. 기기는 WAM 이
`zyl-app://` 의 모든 응답 헤더로(`app_scheme.c` 의 `zyl_app_scheme_csp`), 에뮬레이터는 스토어
앱 응답에 OS 가 준 것을(`sandbox.js` 의 `getContentPolicy` → `store.rs`) 붙입니다. 헤더라서
문서의 첫 바이트와 워커 스크립트부터 묶입니다.

| | 기본 | `network` |
|---|---|---|
| 스크립트 | 자기 파일만. eval·인라인 없음 | 같음 |
| `fetch`·XHR·WebSocket | 자기 출처만 | + https·wss (평문 http 는 막힘) |
| 이미지·미디어·글꼴 | 자기 출처, `data:`·`blob:` | + https |
| iframe | 자기 출처 | 자기 출처 |

어느 앱도 웹을 프레이밍하지 않습니다 — 웹 콘텐츠는 OS 가 소유하는 별도 뷰입니다(§7의
`webcontent`). 그래서 `frame-src` 는 늘 `'self'` 입니다.

- **어느 앱에도 인라인 스크립트가 없는 이유:** 네트워크와 합치면 받아 온 문자열을 스크립트로
  넣어 실행할 수 있어, 봉인한 코드만 돈다는 보장(§6)이 깨집니다. 시스템 앱은 다른 앱·
  네트워크·주변 기기가 보낸 문자열(알림, 파일 이름, Wi-Fi·블루투스 이름, 앱 이름)을 그리므로,
  그 문자열이 마크업으로 들어가도 코드로 돌지 않게 시스템 앱도 같습니다(verify-all A3b 가
  앱 HTML 의 인라인 스크립트·핸들러를 막음). 브리지 스크립트가 넣는 앱 이름·버전은 JS 문자열
  리터럴로 들어갑니다(`bridge.c` 의 `js_literal`).
- **CORS:** 기기는 `network` 를 가진 앱의 https 요청에 CORS 를 걸지 않습니다 — 네이티브
  앱과 같습니다. 앱마다 네트워크 세션(쿠키 포함)이 따로라 남의 자격 증명이 실려 가지 않기
  때문입니다. 에뮬레이터(호스트 WebView)는 iframe 단위로 끌 수 없어, CORS 헤더를 주는
  서버만 응답이 읽힙니다. WebSocket 은 양쪽 모두 CORS 와 무관합니다.
- **OS 에 닿는 것은 앱 자기 문서뿐:** WebKit 은 메시지 핸들러를 웹뷰의 모든 프레임에
  줍니다. 그래서 WAM 은 웹뷰마다 토큰을 만들어 브리지 스크립트(`runtime/wam/src/bridge/bridge.js`)
  에만 넣고, 그 스크립트는 최상위 문서가 `zyl-app://<앱 id>/` 일 때만 토큰을 붙여 보냅니다
  (`window.__zylPost`). 토큰이 없는 메시지는 버립니다(`zyl_bridge_untoken`). 앱이 띄운
  프레임이나 앱이 옮겨 간 페이지는 앱이 아닙니다. 앱 웹뷰는 자기 출처 밖으로 가지 않습니다
  — 어떤 앱도 웹을 자기 문서에 프레이밍하지 않고(최상위 문서가 바뀌면 앱 첫 화면으로 되돌림,
  `zyl_app_scheme_may_load`·`zyl_app_scheme_is_own`), 웹 페이지는 OS 가 소유하는 별도 콘텐츠
  뷰(§7)가 띄웁니다. 에뮬레이터는 프레임 문서의 출처가 표시 중인 앱의 것일 때만 그 앱으로
  봅니다(`emulator.js` 의 `senderOf`).
- **권한을 바꾸면:** CSP 는 문서와 함께 오므로, 설정에서 권한이 바뀐 앱은 실행 중이면 WAM 이
  닫습니다(Android 가 권한을 거둔 앱을 멈추는 것과 같음). 다음 실행부터 새 규칙입니다.
  에뮬레이터는 앱을 바꿀 때마다 문서를 새로 읽으므로 따로 할 일이 없습니다.

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

- PIN 은 6자리 숫자입니다. 화면 키패드와 서비스가 같은 길이여야 하므로 `lock` 서비스
  (기기 `lock.c`, 에뮬레이터 `lock.js`)가 정확히 6자리만 받습니다.
- PIN 은 `lock` 서비스(기기 `runtime/services/lock`, 에뮬레이터 `services/lock.js`)만 다룹니다.
  잠금 화면·설정·첫 설정은 검증과 변경을 요청할 뿐 PIN 을 보관하지 않습니다.
- 잠금 화면은 알림이 **어느 앱에서 왔는지**와 알림이 있다는 사실만 보이고, 제목·본문은
  잠금을 푼 뒤에 보입니다(`apps/lockscreen` 의 `lock.notif_hidden`).
- 기기는 PIN 을 솔트를 넣은 해시로만 `/data/lock` 에 둡니다(root 전용). 비교는 상수 시간,
  변경·제거는 현재 PIN 확인 뒤입니다.
- 실패가 쌓이면 대기가 늘어납니다. 대기는 재부팅으로 초기화되지 않고, 대기 중에는 맞는 PIN
  도 판정하지 않습니다. 시도는 PIN 을 보기 전에 실패로 디스크에 먼저 적고, 적지 못하면
  (저장소가 가득 참) 시도 자체를 하지 않습니다 — 메모리에만 센 시도는 재시작마다 사라졌습니다.
- 앱이 `fs` 로 쓰거나 건네받는 파일은 root 가 아닌 몫(`f_bavail`)까지만 씁니다. ext4 가 root
  에게 남겨 둔 블록은 OS 상태(설정·잠금 시도·메시지)의 몫입니다(`runtime/services/fs/fs.c`).
- 해시 방식·저장 형식·대기 수치는 `runtime/services/lock/lock.c` 의 머리와 상수가 정합니다.
- settings 서비스는 `security.pin` 쓰기를 거부합니다.
- 한계: 하드웨어 키 결합이 없으므로 짧은 PIN 은 해시를 손에 넣으면 오프라인 대입을 막지
  못합니다. 방어선은 대기 정책과 `/data/lock` 접근 제한입니다.

### 사용자 데이터 암호화

- 앱 컨테이너(`/data/appdata`)·사용자 파일(`/data/users`)·연락처·메시지·키체인은 파일
  단위로 암호화됩니다(ext4 fscrypt v2, 내용 AES-256-XTS·이름 AES-256-CTS). 목록은
  `lock.c` 의 `ZYL_CE_DIRS` 입니다. 설정·알람·통화 기록·망·푸시 상태는 잠기지 않아 부팅
  뒤 PIN 전에도 전화가 울리고 알람이 울리고 Wi-Fi 가 붙습니다.
- 키는 `lock` 서비스가 첫 부팅에 만듭니다. PIN 에서 파생한 키로 감싸(AES-256-GCM) PIN
  해시와 같은 파일에 한 번에 씁니다 — 둘이 어긋나 데이터를 잃는 순간이 없습니다. PIN 을
  바꾸거나 지우면 같은 키를 새 PIN 으로 다시 감쌉니다.
- `zyl-lock-prepare.service` 가 서비스들보다 먼저 디렉터리에 정책을 겁니다. PIN 이 없으면
  여기서 키를 올리고, 있으면 부팅 뒤 첫 맞는 PIN 이 올립니다(`StorageUnlocked`). 그 뒤로는
  화면이 잠겨도 열린 채입니다.
- 잠긴 동안 키체인·사용자 서비스는 열리기를 기다리고, 연락처·파일·앱 데이터는 실패로
  답하며, 받은 문자는 `/data/messages-pending` 에 두었다가 열리면 대화로 옮깁니다. 그때
  뜨는 앱의 웹 저장소는 휘발입니다.
- 이미지는 `/data` 를 비워 두고 첫 부팅이 `encrypt` 기능을 켠 ext4 로 만듭니다
  (`tools/build-image.sh` 가 이미지의 `mke2fs.conf` 에 넣음). 이 변경 전에 만들어져 파일이
  든 디렉터리는 그 자리에서 암호화할 수 없어 `zyl-lock-prepare` 가 실패로 알립니다 —
  공장 초기화가 필요합니다.
- 한계: 키를 감싸는 것이 PIN 뿐이라 `/data` 이미지를 떠 간 사람은 PIN 을 오프라인으로 대
  볼 수 있습니다. 하드웨어 키(TEE·eFuse)가 생기면 거기에 묶어야 합니다.

## 5. 앱 컨테이너 — 지우면 아무것도 남지 않는다

- 앱이 만드는 모든 것은 컨테이너 `/data/appdata/<앱 id>/` 에만 있습니다: 비공개 데이터
  (`appdata` — 키는 `caller` 로만 정해져 다른 앱의 것은 이름조차 댈 수 없음), 파일(`fs` —
  제3자 앱은 자기 컨테이너의 `files/` 가 루트), 웹 저장소(WAM 이 앱마다 출처
  `zyl-app://<id>/` 와 `webkit/` 세션을 따로 둠). 에뮬레이터의 `fs` 는 가상 디스크의 보호 경로
  (`settings.json`·`.credentials/`·`.system/`)를 거부합니다 (`apps/system/security.js`).
- **앱끼리 파일은 사본으로만 건너갑니다.** 인텐트의 `files` 는 보낸 앱 루트의 경로이고,
  OS 가 받는 앱 루트(`files/Inbox/`, 시스템 앱이면 사용자의 `Downloads/`)에 복사한 뒤 그 앱
  기준 경로로 바꿔 건넵니다 — 받는 앱이 남의 경로를 받거나 원본을 고치는 일은 없습니다.
  복사는 파일 서비스의 OS 전용 인터페이스 `org.zylos.FileSystem.Handoff`(기기, 앱 브리지에는
  라우트가 없음)와 `fs._handoff`(에뮬레이터, 앱이 부를 수 없는 `_` 메서드)만 합니다.
  사용자의 파일을 앱에 주는 길은 사용자가 OS 화면에서 고르는 것(PICK — 시스템 앱만 처리)뿐입니다.
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

**웹 콘텐츠 — 앱 밖 별도 프로세스.** 앱은 웹을 자기 문서에 프레이밍하지 않습니다. 웹
페이지는 OS 가 소유하는 콘텐츠 뷰(`webcontent` 서비스)가 띄웁니다 — 자기 WebKitWebView,
자기 웹 프로세스, **브리지 없음**(토큰·`navigator.system`·`messageHandlers` 없음). 그래서
웹 콘텐츠의 렌더러 취약점에 OS 로 닿을 길도, 훔칠 토큰도 없습니다. 브라우저(크롬: 주소창·
탭)는 이 서비스로만 콘텐츠를 몰고(navigate/back/…) url·title·loading 을 이벤트로 받을 뿐,
콘텐츠의 DOM·JS 에는 닿지 않습니다. 임베드는 시스템 앱만(예약 권한 `webcontent`). 기기는
WAM 이 콘텐츠 뷰를 앱 창 안에 합성하고, 에뮬레이터는 별도 iframe 으로 근사합니다(교차
출처라 title·url·favicon·진짜 back/forward 는 못 봄 — 기기에서만 충실).
`bubblewrap`·`xdg-dbus-proxy` 가 이미지에 없으면 격리가 조용히 꺼지므로 둘은
`system/packages.list` 에 있습니다. OS 가 자체 seccomp·네임스페이스 코드로 앱을 가두지는
않습니다 — WAM 이 앱 프로세스를 직접 띄우지 않고, 같은 일을 WebKit 이 합니다.

**데몬 — systemd 와 AppArmor.**

- 각 유닛(`system/*.service`)이 systemd 샌드박스 지시어로 자기를 좁힙니다. 기본은
  `ProtectSystem=strict` 에 쓰기 경로를 `ReadWritePaths=` 로만 여는 것이고, 유닛마다의
  지시어와 예외의 이유는 그 유닛 파일에 적혀 있습니다. WAM 은 전용 사용자 `zyl-wam` 으로
  돕니다.
- **외부 입력을 해석하는 서비스는 root 가 아닙니다** — push(푸시 서버 스트림)·location(Wi-Fi
  조회·GeoIP 응답)·network(앱 대신 받는 응답)는 각자 전용 사용자(`system/sysusers.d/zyl-services.conf`)
  로, 커널 권한(capability) 없이, `SystemCallFilter=@system-service` 로 돕니다. 버스에서는 자기
  이름만 갖고 일에 필요한 서비스만 부릅니다(`system/dbus-1/zyl-os.conf` 의 사용자별 정책 —
  `tests/test_dbus_policy.sh` 가 zyl-push 로 확인). telephony(문자·재난문자)와 appstore(내려받은
  패키지)는 아직 root 입니다 — ModemManager 는 재난문자 채널 설정을 root 에게만 열고 나머지는
  이미지에 없는 polkit 에 묻고, NetworkManager(nmcli)도 다른 사용자를 거부하며, appstore 는 WAM
  소유의 앱·컨테이너 디렉터리를 씁니다. 대신 telephony 는 capability 가 하나도 없고 appstore 는
  `CAP_DAC_OVERRIDE`·`CAP_CHOWN` 만 남깁니다(다른 서비스 ptrace·다시 마운트·모듈 적재 불가).
  나머지 서비스 데몬은 root 입니다.
- AppArmor 프로필은 `system/apparmor/zyl-*` 에 있고, `zyl-apparmor.service` 가 부팅 때 모두
  로드합니다. 프로필 하나라도 로드에 실패하면 그 유닛이 실패합니다.
- `tests/verify-all.sh` 가 유닛의 쓰기 경로를 두 가지로 대조합니다: 모두 `tmpfiles.d` 로
  만들어지는지, 그 서비스의 AppArmor 프로필이 그 경로에 쓰기를 허용하는지. 전용 사용자 유닛은
  그 사용자가 있고, 쓰는 `/data` 경로가 그 사용자 소유이며, 버스 이름을 가질 수 있는지도(D4).

## 8. 키체인

- `credential` 서비스(기기 `runtime/services/credential/credential.c`)가 비밀을 암호화해
  저장합니다. `credential` 은 예약 권한이라 시스템 앱만 부릅니다.
- 항목마다 파일 하나로 인증 암호화하고, 항목 키는 마스터 키와 항목의 무작위 솔트에서
  파생합니다. 마스터 키는 기기 결합 키에서 파생합니다. 기기 결합 키는 첫 시작 때 만든
  무작위 바이트로 저장소 디렉터리(root 전용)에 있고, 저장소는 시작하면 사용자 입력 없이
  열립니다. 있는데 읽을 수 없는 키 파일은 새로 만들지 않습니다 — 저장된 비밀을 모두 잃게
  되기 때문입니다. 저장소 디렉터리는 사용자 데이터라 부팅 뒤 첫 잠금 해제 때 열립니다.
- 암호 방식·파일 형식·키 파생은 `credential.c` 의 머리가 정합니다.
- 한계: 키가 저장소와 같은 곳에 있으므로 켜진 기기에서 지키는 것은 그 디렉터리의 접근
  제한이고, 꺼진 기기에서는 그 디렉터리를 잠그는 PIN 입니다.
- 에뮬레이터는 같은 방식을 컨테이너(`emulator-app/src/commands/emulated_services.rs`)가
  구현합니다.

## 9. 검증 부팅 (Verified Boot)

각 단계는 다음 단계의 서명을 검증한 뒤에만 넘깁니다. 키는 `tools/gen-boot-keys.sh` 가 만듭니다.

| 단계 | 누가 검증 | 키 | 만드는 곳 |
|---|---|---|---|
| FSBL (SPL) | Boot ROM — eFuse 에 루트 키 해시가 있을 때만 | `zylos-rotpk`(헤더), `zylos-spl-key`(SPL) | `tools/build-bootloader.sh` |
| OpenSBI · U-Boot FIT | SPL (공개키가 SPL DTB 에 `required`) | `zylos-fw-key` | `tools/build-bootloader.sh` |
| 커널 · DTB · initramfs FIT | U-Boot (공개키가 U-Boot DTB 에 `required`) | `zylos-boot-key` | `tools/sign-image.sh` |
| rootfs | initramfs 가 여는 dm-verity (`system/initramfs`) — 값은 전부 서명된 커널 인자, 디스크의 verity 수퍼블록은 읽지 않음 | — | `tools/sign-image.sh` |
| 사용자 앱 | appstore · WAM 봉인 (6장) | 스토어 키 / 허락된 개발자 키 | `tools/build-store.py` |

**불변식**: 부트 정책(모든 보드의 `board/boot-policy.txt` + 보드의 `board/<id>/u-boot-env.txt`)은
부트로더의 기본 환경으로 내장되어 바깥에서 바꿀 수 없습니다. 저장소의 환경에서는 정책의
"Runtime state" 변수만 받으므로(쓰기 허용 목록) 환경을 쓸 수 있어도 `bootcmd` 와 검증은 그대로이고,
커널 인자(루트·verity 값·슬롯)는 환경이 아니라 서명된 FIT 안에 있습니다. 검증되지 않는 루트는
셸 대신 재시작으로 끝나고(`panic=`, `--restart-on-corruption`), 시험 중인 슬롯이면 U-Boot 가 그
시도를 셉니다. 정책은 `tests/test_boot_env.sh` 가 보드마다 실행하고, `tests/verified_boot.sh` 가
QEMU 에서 실제 U-Boot 로 확인합니다 — 저장된 환경에 써 넣은 `bootcmd` 가 무시되는지, 변조된
FIT 가 거부되고 롤백되는지. OS 이미지의 검증을 끄는 길은 소유자가 기기에서
확인하는 잠금 해제(GPLv3 §6) 하나이고, 잠금 상태가 바뀔 때마다 `/data` 가 지워집니다.
부트로더가 무엇을 막는지, U-Boot 명령 줄이 언제 남는지, 잠금 해제 절차, eFuse 굽기는
[INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) 가 정합니다.

에뮬레이터는 기기가 아니므로 검증 부팅이 없습니다.

## 10. OTA

- 패키지(`tools/build-ota.sh`)는 패키지 SHA-256 에 대한 RSA-SHA256 서명을 가집니다. 서명
  키는 부트 서명 키(`zylos-boot-key`)입니다 — 부팅 이미지를 서명할 수 있는 키가 곧 업데이트를
  낼 수 있는 키입니다.
- **받을 조건**: U-Boot 환경을 읽을 수 있고(`fw_printenv`, 자리는 이미지 빌드가 보드의
  `partitions.conf` 에서 만든 `/etc/fw_env.config`), 공개키
  `/etc/zyl-os/ota-signing-key.pem`(빌드 옵션 `-Dota_public_key`)이 이미지에 있을 것.
  하나라도 없으면 업데이트를 받지 않습니다.
- **검증**: 서명이 없거나 맞지 않거나 해시가 다른 패키지는 설치하지 않습니다
  (`runtime/lib/ota_verify.c`). 서버 주소는 https 만 받고, 리디렉트도 https 만 따라갑니다.
  다운로드는 매니페스트의 크기(없으면 4GiB)까지만, `/data` 에 그만큼과 256MiB 여유가 있을
  때만 시작하고, 실패하면 받던 조각을 지웁니다. 전체 이미지 패키지만
  설치합니다 — 델타·앱만·커널만 패키지는 dm-verity rootfs 와 맞출 수 없어 거부합니다.
- **설치**: 비활성 슬롯에만 rootfs → verity 트리 → FIT 순서로 씁니다(FIT 는 임시 이름으로
  쓰고 교체). 지금 부팅이 확정 전 시험 부팅이면 설치하지 않습니다 — 돌아갈 슬롯을 덮지
  않기 위해서입니다 (`runtime/services/updater`).
- **전환**: 모든 기록이 끝난 뒤에만 `zyl_next_slot` 을 지정합니다. 새 슬롯의 부팅은 U-Boot
  의 FIT 서명과 커널의 dm-verity 가 다시 확인합니다.
- **확정과 롤백**: 부팅 1분 뒤부터 업데이터는 핵심 유닛(컴포지터·WAM·잠금·설정)을 30초
  간격으로 보고, 두 표본에서 모두 active 이고 같은 실행(재시작 없음)이면 슬롯을 확정합니다.
  몇 번 봐도(약 3.5분) 안 되면 재시작합니다 — 몇 초마다 죽는 WAM 도 한 번 본 순간에는 떠 있을
  수 있어서입니다. 부트 정책이 정한 횟수(`board/boot-policy.txt` 의 `zyl_boot_limit`) 안에 확정되지
  않으면 U-Boot 가 이전 슬롯으로 돌아갑니다.
- 패키지를 만들고 이미지를 준비하는 절차는 [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md#6-ota).

## 보안 취약점 신고

보안 취약점을 발견하시면 security@zyl-os.com 으로 보고해 주세요.
