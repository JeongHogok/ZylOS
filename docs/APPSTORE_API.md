# Zyl OS 앱스토어 — 스토어 운영자를 위한 프로토콜

스토어는 **아무 HTTPS 호스트에 올린 정적 파일 묶음**입니다. 서버 프로그램은 없습니다.
`tools/build-store.py` 가 파일을 만들고, 기기와 에뮬레이터가 검증합니다. 신뢰는 호스트가
아니라 키에서 옵니다 — CDN·미러는 파일을 내줄 수는 있어도 바꿀 수는 없습니다.

키는 2단입니다.

| 키 | 어디에 | 무엇을 서명 |
|---|---|---|
| **루트 키** | 기기 이미지의 `system/store.json` `rootKey`(공개키). 비밀키는 오프라인에만 | 서명 키 인증서(`signing.json`)만 |
| **서명 키** | 스토어 운영(온라인) | 카탈로그, 패키지, 공식 앱 봉인, 개발자 인증서·프로필 |

서명 키가 새면 루트 키로 새 서명 키 인증서를 내고, 거기에 옛 일련번호를 폐기한다고
적습니다(아래 "폐기"). 기기 이미지(루트 키)를 바꾸지 않아도 됩니다.

검증하는 쪽은 둘이고 규칙이 같습니다.

| | 기기 | 에뮬레이터 |
|---|---|---|
| 카탈로그·패키지·설치 | `runtime/services/appstore/` (`org.zylos.AppStore`) | `apps/system/services/appstore.js` |
| 봉인 판정 | `runtime/lib/app_seal.c` | `apps/system/services/app-seal.js` |

종단 동작은 `tests/store_e2e.sh` 가 실제 데몬과 로컬 HTTPS 서버로 확인합니다.

## 서명

모든 서명은 OTA 와 같은 한 방식입니다 (`tools/build-ota.sh`, `runtime/lib/ota_verify.c`).

- 서명하는 메시지: 대상 파일 바이트의 **SHA-256 32바이트**
- 알고리즘: RSASSA-PKCS1-v1_5 + SHA-256 (`openssl dgst -sha256 -sign <키>` 에 그 32바이트를 넣은 결과)
- 표기: base64 텍스트. `.sig` 파일은 앞뒤 공백·줄바꿈을 무시합니다
- 키: RSA. 공개키는 PEM (`-----BEGIN PUBLIC KEY-----`)

## 스토어의 파일

```
<store>/signing.json          서명 키 인증서 (아래)
<store>/signing.json.sig      그 서명 (루트 키)
<store>/catalog.json          제공하는 앱 목록
<store>/catalog.json.sig      catalog.json 의 서명 (서명 키)
<store>/<id>-<version>.zpkg   앱 버전마다 zip 하나
```

`signing.json`·`catalog.json` 은 같은 디렉터리에 둡니다. 기기는 `catalog.json` 주소를 기준으로
`signing.json` 을 찾습니다.

### signing.json

```json
{"version": 1, "serial": 2, "publicKey": "<서명 키 공개키 PEM>", "revokedSerials": [1]}
```

- `serial`: 1 이상의 정수. 새 서명 키마다 앞선 것보다 큰 수를 씁니다
- `revokedSerials`: 더는 믿지 않는 앞선 일련번호. 기기는 본 것을 **계속 쌓아** 둡니다 —
  한 번 폐기된 서명 키는 돌아오지 않습니다
- 루트 키가 서명하지 않았거나 일련번호가 폐기됐으면, 그 서명 키로 서명한 모든 것은 무효입니다

### catalog.json

```json
{
  "version": 1,
  "issued": 1800000000,
  "apps": [
    {
      "id": "com.example.hello",
      "name": "Hello",
      "version": "1.2.0",
      "description": "…",
      "developer": "Example Inc.",
      "category": "productivity",
      "permissions": ["network"],
      "package": "com.example.hello-1.2.0.zpkg",
      "size": 10240,
      "sha256": "<64자리 hex — 패키지 파일의 SHA-256>",
      "signature": "<base64 — 패키지 파일의 서명 (서명 키)>"
    }
  ],
  "revoked": ["<폐기한 개발자 인증서 cert.json 의 SHA-256>"],
  "revokedApps": ["<회수한 앱 id>"]
}
```

기기는 `issued`·`apps`·`revoked`·`revokedApps` 를 읽습니다.

- `issued`: 발행 시각(epoch 초, 1 이상)이며 필수입니다. 기기는 이미 본 카탈로그보다
  오래된 카탈로그를 받지 않습니다(`untrusted`). 옛 카탈로그를 되돌려 보내 회수를 풀 수 없습니다.

항목 규칙 (`store_catalog.c`):

- `id`: 영문자·숫자·`.`·`_`·`-`, `.` 를 하나 이상 포함(역도메인), `..` 없음. 앱의
  디렉터리와 컨테이너 이름이 됩니다. 길이 상한은 `runtime/lib/container.c`
- `name`: 비어 있지 않은 문자열. `version`: 숫자와 점 (`1.2.0`)
- `package`: `catalog.json` 기준 상대 경로이거나 절대 URL. 풀린 결과가 `https://` 여야 합니다
- `size`: 패키지 바이트 수 (양의 정수, 상한은 `store_catalog.h` 의 `ZYL_STORE_MAX_PACKAGE`).
  기기는 이 크기를 넘게 받지 않습니다
- `sha256`: 64자리 hex. `signature`: base64
- `description`·`developer`·`category`: 선택 문자열
- `permissions`: 스토어 화면에 보여 주는 값일 뿐입니다. 설치된 앱의 권한은 패키지 안
  `app.json` 의 선언으로 정해지고, 예약 권한은 선언해도 주어지지 않습니다
  ([SECURITY.md](SECURITY.md) 의 권한)

규칙을 어긴 항목은 건너뜁니다. 같은 `id` 가 여럿이면 첫 항목만 씁니다. 시스템 앱(이미지에
든 앱)의 id 는 목록에 보이지 않고 설치되지도 않습니다.

`build-store.py catalog` 는 `.zpkg` 안의 `app.json` 에서 이 값들을 채웁니다.

### 패키지 (`.zpkg`)

앱 디렉터리를 zip 으로 묶은 것으로, 루트에 `app.json` 이 있습니다. 기기가 푸는 규칙
(`store_package.c`):

- 일반 파일과 폴더만. 절대 경로, `.`·`..` 경로 조각, `\`, 링크·장치 파일이 하나라도 있으면
  패키지 전체가 무효입니다
- 파일 수와 풀린 전체 크기에 상한이 있습니다 (`store_package.h` 의 `ZYL_STORE_MAX_FILES`,
  `ZYL_STORE_MAX_UNPACKED`)
- `app.json` 의 `id`·`version` 이 카탈로그 항목과 같아야 하고, `entry`(기본 `index.html`)
  페이지가 있어야 합니다

`build-store.py package` 는 숨김 파일(`.` 으로 시작)과 링크를 넣지 않고, 같은 앱에서 같은
바이트가 나오도록 묶습니다.

### 봉인 (`.zyl-seal/`)

모든 패키지는 봉인을 가집니다. 봉인이 맞지 않는 사용자 앱은 설치·등록·실행되지 않습니다.

```
.zyl-seal/seal.json      {"version":1,"id":…,"appVersion":…,"files":{"<경로>":"<sha256 hex>",…}}
.zyl-seal/seal.sig       seal.json 의 서명
.zyl-seal/signing.json   봉인(또는 개발자 인증서·프로필)을 서명한 서명 키의 인증서 — signing.sig 와 함께
.zyl-seal/cert.json      개발 빌드만 (아래) — cert.sig 와 함께
.zyl-seal/profile.json   개발 빌드만 — profile.sig 와 함께
```

- `id`·`appVersion` 은 설치하는 앱의 id·버전과 같아야 합니다
- `files` 는 `.zyl-seal/` 밖의 **모든** 일반 파일을 앱 디렉터리 기준 상대 경로로 담습니다.
  빠진 파일, 더 있는 파일, 내용이 다른 파일, 링크가 하나라도 있으면 봉인이 깨집니다
- `seal.sig` 의 키: 공식 앱은 서명 키, 개발 빌드는 `cert.json` 의 개발자 키
- `signing.json` 은 루트 키가 서명하고 폐기되지 않은 것이어야 합니다. 봉인이 서명 키
  인증서를 담고 있으므로, 기기는 스토어에 닿지 않고도 등록 때 봉인을 판정합니다

설치 뒤에도 기기가 봉인을 언제 다시 확인하는지는 [SECURITY.md §6](SECURITY.md#6-앱-봉인과-스토어-서명).

## 개발 빌드 — 인증서와 프로비저닝 프로필

개발자의 빌드는 스토어가 허락한 기기·앱에서만 돕니다. 둘 다 서명 키로 서명합니다.
스토어는 이 둘과 함께 `signing.json`·`.sig` 를 개발자에게 줍니다 — 개발자의 봉인에 들어갑니다.

| 파일 | 내용 |
|---|---|
| `cert.json` | `{"version":1, "developer", "name", "publicKey": "<개발자 공개키 PEM>", "notAfter": <epoch 초>}` — 스토어가 이 개발자 키를 보증 |
| `profile.json` | `{"version":1, "cert": "<cert.json 바이트의 SHA-256>", "apps": ["com.acme.app" 또는 "com.acme.*"], "devices": ["<기기 ID>", …], "notAfter": <epoch 초>}` |

판정 순서 (`app_seal.c`, `app-seal.js` — 같은 순서·같은 사유):

0. (모든 봉인) 앱 id 가 회수됨, 또는 `signing.json` 의 일련번호가 폐기됨 → `revoked`.
   `signing.json` 이 루트 키 서명이 아님 → `bad_package`
1. `cert.json`·`profile.json` 이 서명 키로 서명돼 있지 않음 → `bad_package`
2. 인증서의 SHA-256 이 기기가 가진 폐기 목록에 있음 → `revoked`
3. 인증서나 프로필의 `notAfter` 가 지남 → `expired`
4. 프로필의 `cert` 가 그 인증서가 아님, 앱 id 가 `apps` 에 없음, 이 기기의 ID 가
   `devices` 에 없음 → `not_provisioned`
5. `seal.sig` 가 인증서의 `publicKey` 로 서명돼 있지 않거나 파일이 봉인과 다름 → `bad_package`

- 앱 패턴: 정확한 id, 또는 `.*` 로 끝나는 접두어 (`com.acme.*` 는 `com.acme.` 아래의 id)
- 기기 ID: 보드가 주는 하드웨어 ID (`/proc/device-tree/serial-number`, BPI-F3 은 부트로더가
  칩 ID 로 정함). 설정 > 이 기기 정보 > 기기 ID 에 보입니다. ID 가 없는 기기는 개발 빌드를
  실행하지 않습니다
- 개발 빌드는 사용자 파일에서만(`InstallFile`) 설치됩니다. 카탈로그로는 서명 키 봉인만 받습니다
- 기한과 폐기는 설치 때만이 아니라 앱을 등록할 때마다 다시 확인합니다
  ([SECURITY.md §6](SECURITY.md#6-앱-봉인과-스토어-서명))

### 폐기와 회수

| 무엇을 | 어디에 적나 | 기기가 지키는 방식 |
|---|---|---|
| 개발자 인증서 | 카탈로그 `revoked` (`cert.json` 의 SHA-256) | 카탈로그마다 **통째로 바꿈** — 이후 카탈로그에 계속 실어야 한다 |
| 앱 | 카탈로그 `revokedApps` (앱 id) | 위와 같다 |
| 서명 키 | 루트 키가 서명한 새 `signing.json` 의 `revokedSerials` | **쌓기만** 한다 |

- 기기는 이를 `/data/apps/.revocations.json` 에 둡니다(`app_seal.c` 가 형식의 주인).
  바뀌면 `RevocationsChanged` 를 보냅니다. WAM 은 모든 사용자 앱의 봉인을 다시 판정하고,
  더는 맞지 않는 앱은 실행 중이어도 닫습니다. 그 데이터는 사용자가 지울 때까지 남습니다.
- 카탈로그를 바꿔 쓰는 것은 `issued` 가 본 것 이상일 때뿐입니다.
- 회수는 기기가 스토어에 닿을 때(카탈로그를 받을 때) 알게 됩니다.

## 기기 설정

이미지의 `system/store.json` 이 기기의 `/etc/zyl-os/store.json` 이 됩니다.

```json
{ "url": "https://store.example/v1/catalog.json", "rootKey": "-----BEGIN PUBLIC KEY-----\n…" }
```

- `url` 은 `catalog.json` 의 https 주소입니다. 서명은 같은 주소에 `.sig` 를 붙인 곳에서,
  `signing.json` 은 같은 디렉터리에서 받습니다
- `rootKey` 는 루트 키의 공개키입니다. 이미지(dm-verity)에 들어 있어 OTA 로만 바뀝니다
- `url` 이 https 가 아니거나 `rootKey` 가 비어 있으면 스토어는 `unconfigured` 입니다

## 스토어 만들기

```bash
openssl genrsa -out root.key 3072                          # 오프라인에만 둔다
openssl rsa -in root.key -pubout -out root.pub             # → rootKey
openssl genrsa -out signing.key 3072
openssl rsa -in signing.key -pubout -out signing.pub
python3 -I tools/build-store.py signing-cert --root-key root.key --pubkey signing.pub \
    --serial 1 --out signing.json                          # (+ signing.json.sig)
python3 -I tools/build-store.py package --key signing.key --signing signing.json path/to/app out/
python3 -I tools/build-store.py catalog --key signing.key --signing signing.json \
    [--revoked revoked.txt] [--revoked-apps apps.txt] out/
# out/ 을 https 호스트에 올리고, 그 catalog.json 주소 → url
```

서명 키를 바꿀 때 — 새 키에 더 큰 일련번호로 `signing-cert` 를 내고, 옛 키가 샜다면
`--revoke <옛 일련번호>` 를 붙입니다. 폐기된 키로 봉인한 앱은 새 키로 다시 봉인해 새 버전으로
냅니다(서명자는 같은 "스토어" 라 업데이트로 받아들여집니다).

개발자를 받는 쪽 — 개발자에게서 공개키(`tools/zyl keygen`)와 시험할 기기 ID 를 받아:

```bash
python3 -I tools/build-store.py dev-cert --key signing.key --developer acme --name "ACME" \
    --pubkey acme.pub --days 365 --out acme.cert.json
python3 -I tools/build-store.py profile --key signing.key --cert acme.cert.json \
    --app 'com.acme.*' --device <기기 ID> --days 30 --out acme.profile.json
```

`.json` 과 `.sig` 를 `signing.json`·`.sig` 와 함께 돌려주면 개발자가 그것으로 봉인합니다
([APP_DEVELOPMENT_GUIDE.md](APP_DEVELOPMENT_GUIDE.md) §14). `revoked.txt` 는 한 줄에
SHA-256 하나, `apps.txt` 는 한 줄에 앱 id 하나입니다.

## 기기가 설치 전에 확인하는 것

통신은 https 로만 하고 리디렉션도 https 로만 따라갑니다. 무결성은 서명이 보장합니다.

**카탈로그** (`GetCatalog`, 그리고 `Install` 의 첫 단계)

1. 설정이 있는가 → 아니면 `unconfigured`
2. `signing.json`·`catalog.json` 과 서명들을 받는가 → 아니면 `unreachable`
3. `signing.json` 이 루트 키 서명이고 일련번호가 폐기되지 않았는가 → 아니면 `untrusted`
4. 카탈로그가 그 서명 키 서명인가 → 아니면 `untrusted`
5. `apps` 배열을 가진 JSON 객체인가 → 아니면 `invalid`
6. `issued` 가 있고 이미 본 것 이상인가 → 아니면 `untrusted`. 맞으면 회수 목록을 저장합니다

**카탈로그에서 설치** (`Install(app_id)`)

1. 카탈로그에 있고(`not_in_store`) 시스템 앱도 `com.zylos.*` 도 아니며(`system_app` —
   그 이름은 OS 이미지의 것) 설치된 버전보다 새로운가(`up_to_date`) — 같은 버전 재설치와
   다운그레이드는 없습니다
2. 패키지를 `size` 안에서 받는가 → `download_failed`
3. 패키지가 카탈로그의 `sha256`·`signature`(서명 키)와 맞는가 → `bad_package`
4. 안전하게 풀리고 `app.json` 이 그 앱·버전이며 시작 페이지가 있는가 → `invalid_package`
5. 서명 키 봉인이고 파일이 봉인과 정확히 같은가 → `bad_package` (개발 빌드도 여기서 거부).
   회수된 앱이거나 봉인의 서명 키가 폐기됐으면 → `revoked`
6. 설치된 앱이 있으면 그 서명자(스토어, 또는 개발자 인증서의 공개 키)와 같은가 →
   `signer_changed` — 다른 서명자의 빌드가 같은 id 로 그 앱의 컨테이너를 넘겨받지 않게.
   앱을 지운 뒤(컨테이너도 지워짐) 설치합니다
7. 설치된 것과 교체합니다. 업데이트는 앱 컨테이너를 그대로 둡니다

**사용자 파일에서 설치** (`InstallFile(path)`)

1. `path` 는 사용자 파일 영역 기준 상대 경로이고 실제 위치도 그 안이어야 합니다 → `invalid_package`
2. 카탈로그 설치의 4번과 같고, 앱 id 는 역도메인이어야 합니다 → `invalid_package`
3. 시스템 앱도 `com.zylos.*` 도 아니고(`system_app`) 다운그레이드가 아닌가(`up_to_date`) —
   같은 버전은 다시 설치할 수 있습니다 (개발자의 다음 빌드)
4. 봉인: 서명 키 봉인, 또는 위 판정을 통과한 개발 빌드 → `bad_package`·`revoked`·
   `expired`·`not_provisioned`
5. 카탈로그 설치의 6번과 같습니다 → `signer_changed`

설치가 끝나면 `AppInstalled` 를 보내고, WAM 이 봉인을 다시 확인한 뒤 앱을 등록합니다.

## 기기 인터페이스 (`org.zylos.AppStore`)

앱은 `ZylBridge.requestService('appstore', …)` 로 부릅니다 (메서드 이름은 소문자로
시작: `getCatalog`, `install`, `installFile`, `uninstall`). `appstore` 서비스는 예약 권한
`app.manage` 가 필요하므로 시스템 앱만 부를 수 있습니다 (`apps/system/permissions.js`).

| 메서드 | 결과 |
|---|---|
| `GetCatalog()` | `{ state, apps: [{ id, name, version, description, developer, category, size, permissions, installed, installedVersion, update }] }` — `state` 는 `ok`·`unconfigured`·`unreachable`·`untrusted`·`invalid` |
| `Install(app_id)` | `{ ok, reason }` — 설치 또는 업데이트 |
| `InstallFile(path)` | `{ ok, reason, appId }` — 사용자 파일의 `.zpkg`. 파일 앱이 `.zpkg` 를 열면 스토어 앱이 VIEW 인텐트로 받아 부릅니다 |
| `Uninstall(app_id)` | `{ ok, reason }` — 앱과 그 컨테이너 전체를 지움. `system_app`, `not_installed` |
| 시그널 `AppInstalled(app_id)`, `AppRemoved(app_id)` | WAM 이 앱을 등록·해제하고, 앱별 상태를 가진 서비스가 그 앱의 것을 지웁니다 |
| 시그널 `RevocationsChanged()` | 회수 목록이 바뀜 — WAM 이 사용자 앱을 다시 판정합니다 |

작업은 한 번에 하나입니다 (다른 작업 중이면 `busy`). 기기에서 파일을 쓰지 못하면 `io` 입니다.

삭제 뒤 무엇이 남지 않아야 하고 누가 그것을 확인하는지는
[SECURITY.md §5](SECURITY.md#5-앱-컨테이너--지우면-아무것도-남지-않는다).
