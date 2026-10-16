# Zyl OS 앱 개발 가이드

Zyl OS 앱을 만드는 사람(제3자 앱·이 저장소의 시스템 앱)을 위한 안내다.
규칙과 계약은 코드에 한 번씩만 적혀 있고, 이 문서는 그 위치와 읽는 법을 알려 준다.

| 알고 싶은 것 | 단일 출처 |
|---|---|
| 앱이 쓰는 API (`ZylBridge`, `ZylIntent`) | `apps/shared/bridge.js` |
| 서비스마다의 메서드·인자·결과 | `apps/system/services/<서비스>.js` (기기의 C 서비스가 같은 계약에 답한다) |
| 서비스 이벤트 이름·인자 | 기기 `runtime/services/<서비스>/` 의 D-Bus 시그널 선언 |
| 권한 | `apps/system/permissions.js` (표를 읽는 법: [SECURITY.md §3](SECURITY.md#3-권한)) |
| 폼팩터 ID와 성질 | `apps/shared/form-factor.js` 의 `PROFILES` |
| `app.json` 이 갖춰야 할 것 | 3장의 표. 도구가 검사하는 것은 [3장 끝](#3-appjson) |
| 패키지·봉인·개발 빌드 | [APPSTORE_API.md](APPSTORE_API.md) |

전체 구조는 [ARCHITECTURE.md](ARCHITECTURE.md), 권한·봉인의 보안 모델은 [SECURITY.md](SECURITY.md) 를 본다.

---

## 1. 앱이란

- **웹 앱 한 개**다: 자기 디렉터리에 `app.json`, 시작 HTML, CSS, ES5 JavaScript.
- **기기에서는 자기 출처에서 돈다**: `zyl-app://<앱 id>/`, WAM 이 앱마다 WebKit 뷰를 하나씩
  띄운다. 에뮬레이터에서는 스토어로 설치한 앱만 그 출처를 갖고, 이미지에 든 앱은 컨테이너
  페이지의 출처를 함께 쓴다(`apps/system/app-registry.js` 의 `getOrigin`). 어느 쪽이든 앱이
  보는 API 는 `ZylBridge` 하나다 — 앱은 자기가 기기에 있는지 에뮬레이터에 있는지 알 필요가 없다.
- **앱 자기 파일과 OS 코드만 읽는다**: OS 코드는 `/shared/`(앱에서는 `../shared/…`)와
  `bridge.js` 가 불러오는 화면 키보드 `/keyboard/` 이고, 이미지의 것이 나간다. 코드는 이
  파일에서만 돈다(CSP): 바깥 스크립트·`eval` 은 없고, 인라인 스크립트(`<script>` 본문,
  `onclick="…"`)도 안 된다 — `<script src>` 와 `addEventListener` 를 쓴다.
- **`network` 권한이 있으면 바깥에 직접 나간다**: `fetch`·`XMLHttpRequest`·`WebSocket` 으로
  https/wss 만(평문 http 는 막힘), 이미지·미디어·글꼴도 https 에서. 권한이 없으면 자기 출처
  밖으로는 아무것도 못 나간다. 자세한 규칙은 [SECURITY.md §3](SECURITY.md#3-권한).
- **앱 파일은 읽기 전용이다**: 봉인(14장)과 다른 파일은 앱에 전달되지 않는다. 데이터는
  9장의 저장소에 둔다.
- **흔적은 컨테이너에만 남는다**: 앱이 만드는 모든 것은 `/data/appdata/<앱 id>/` 에 있고,
  앱을 지우면 함께 지워진다 ([SECURITY.md §5](SECURITY.md#5-앱-컨테이너--지우면-아무것도-남지-않는다)).
- **시스템 앱은 OS 이미지에 실려 오는 앱이다.** 앱이 스스로 시스템 앱이라고 선언할 방법은
  없다. 제3자 앱과의 차이는 예약 권한(5장), 파일 루트(9장), 남을 대신해 말할 수 있는지(5장)다.

---

## 2. 디렉터리와 시작 페이지

```
myapp/
├── app.json        # 매니페스트 (3장)
├── index.html      # app.json 의 entry
├── i18n.js         # 번역 (12장)
├── css/myapp.css
└── js/myapp.js
```

`tools/zyl init <이름> [id]` 가 이 뼈대를 만든다. `id` 는 자기 도메인을 뒤집은 이름이어야
한다(개발 빌드 프로필의 앱 id 패턴과 맞아야 한다). 생략하면 자리표시 `com.example.<이름>` 이 들어가니
바꿀 것. `com.zylos.*` 는 OS 이미지의 앱이다.

시작 페이지가 읽는 OS 코드와 그 순서는 `apps/notes/index.html` 이 본보기다. `tools/zyl init`
이 쓰는 `index.html` 도 같은 것을 같은 순서로 넣는다. `bridge.js` 는
`<script src="…/shared/bridge.js">` 로 읽어야 한다 — 화면 키보드(11장)를 그 위치를 기준으로
찾는다.

---

## 3. app.json

```json
{
  "id": "com.example.myapp",
  "name": "My App",
  "version": "1.0.0",
  "entry": "index.html",
  "permissions": ["notification"],
  "formFactors": ["*"],
  "intents": [{ "action": "zyl.intent.action.VIEW", "mimeType": "text/plain" }],
  "iconSvg": "M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2z",
  "color": "icon-blue",
  "description": "…"
}
```

| 필드 | 뜻 |
|---|---|
| `id` | 필수. 역도메인 이름. 앱의 출처(`zyl-app://<id>/`)와 컨테이너(`/data/appdata/<id>/`)의 이름이 된다. 시스템 앱의 id 는 쓸 수 없다 |
| `name` | 필수. 표시 이름 |
| `version` | 필수. 숫자와 점(`1.2.3`). 기기는 낮은 버전으로 되돌리는 설치를 받지 않는다 |
| `entry` | 필수. 시작 HTML, 앱 디렉터리 기준 경로 |
| `permissions` | 앱이 부를 서비스에 필요한 권한 이름 (5장). 없으면 권한 없음 |
| `formFactors` | 이 앱이 맞춰 만든 폼팩터 ID 목록, `["*"]` 은 전부. 유효한 ID는 `PROFILES`. 목록 밖의 기기에서는 홈 화면에 나오지 않는다. 늘 적는다 — 없으면 OS 는 전부로 보고, 저장소 앱은 `tests/test_manifests.sh` 가 요구한다 |
| `intents` | 이 앱이 열어 주는 것 `[{ action, mimeType }]` (7장). `action` 은 `ZylIntent.ACTION` 의 값, `mimeType` 은 `type/subtype`(subtype 은 `*` 가능) |
| `launcher` | `false` 면 런처·최근 앱에 나오지 않는다 (시스템 UI용 — 시스템 앱만 따른다. 제3자 앱이 숨으면 사용자가 지우거나 권한을 끌 곳이 없어진다). 기본 `true` |
| `iconSvg` | 아이콘. 24×24 `viewBox` 안의 SVG **path 데이터**(`d` 값) — `<svg>` 요소가 아니다 |
| `color` | 아이콘 바탕. `apps/home/css/home.css` 의 `.icon-*` 클래스 이름 |
| `description`·`developer`·`category` | 스토어 카탈로그에 실린다 (`tools/build-store.py`) |

검사:

- 저장소 앱 — `tests/test_manifests.sh`.
- `tools/zyl validate`(앱 디렉터리에서) — `id`·`name`·`version`·`entry` 가 있는지, `version` 이
  숫자와 점인지, `entry` 파일이 있는지, 모든 JS 파일에 `let`·`const`·`=>` 가 없는지, `i18n.js`
  에 앱 번역 언어(ko·en·ja·zh·es)가 다 있는지(`i18n.js` 가 없으면 경고만), 시작 페이지 첫 줄이 DOCTYPE 인지.
- `tools/build-store.py package`(`tools/zyl package` 가 부른다) — `id` 가 역도메인인지,
  `version` 이 숫자와 점인지, `entry` 페이지가 있는지.

제3자 앱의 `formFactors`·`intents`·`iconSvg`·`color` 는 어느 도구도 검사하지 않는다.

---

## 4. 서비스 부르기

```javascript
ZylBridge.requestService(service, method, params)   // → Promise
```

- 요청마다 `requestId` 가 붙고, 같은 `requestId` 의 응답이 그 Promise 를 끝낸다.
  앱이 응답 메시지를 직접 듣거나 짝지을 필요는 없다.
- 결과는 `resolve`, 거부·실패는 `reject` 다. reject 값은 `error` 를 가진 객체다
  (권한 거부, 인자 오류, 서비스 오류, 시간 초과 모두).
- 응답이 필요 없으면 `ZylBridge.requestServiceFire(service, method, params)`.

### 계약을 찾는 법

서비스 `X` 의 계약은 `apps/system/services/X.js` 가 돌려주는 객체다. 키가 메서드 이름,
첫 인자 `p` 의 멤버가 `params`, 반환값이 결과다. `_` 로 시작하는 것은 OS 내부용이라
앱이 부를 수 없다. 기기에서는 같은 요청이 D-Bus 의 `org.zylos.*` 서비스로 가서
메서드 이름의 첫 글자를 대문자로 바꾼 메서드(`getItem` → `GetItem`)를 부르고,
`params` 의 멤버를 같은 이름의 인자에 넣는다(대소문자·`_` 무시, 빠진 인자는 오류).

### 예 1 — 앱 전용 키-값 저장 (`appdata`, 권한 불필요)

```javascript
ZylBridge.requestService('appdata', 'setItem', { key: 'lastTab', value: 'inbox' })
  .then(function (stored) {
    /* true: 저장됨. false: 앱의 할당량을 넘었거나 key·value 가 맞지 않음 */
  });

ZylBridge.requestService('appdata', 'getItem', { key: 'lastTab' })
  .then(function (value) {
    /* 없는 키는 '' */
  });
```

어느 앱의 데이터인지는 요청에 없다. OS 가 부른 앱을 `caller` 로 넣으므로 앱은 다른 앱의
데이터를 가리킬 수조차 없다 ([앱이 누구인지는 OS 만 말한다](#앱이-누구인지는-os-만-말한다)).

### 예 2 — 알림 올리기 (`notification` 권한)

```javascript
ZylBridge.notify(zylI18n.t('myapp.done_title'), zylI18n.t('myapp.done_body'), { priority: 2 })
  .catch(function () { /* 권한이 없거나 사용자가 껐다 */ });
```

`notify` 는 `notification` 서비스 요청이다. 올린 앱은 OS 가 정한다 — 제3자 앱이 `appId`
를 무엇으로 주든 자기 이름으로 올라간다.

한 앱이 띄워 둘 수 있는 알림은 **50개**까지다(51번째는 거부 — 지운 만큼 다시 올릴 수
있다). 제목은 128자, 본문은 1024자, 액션은 3개까지만 남고 넘는 것은 잘린다. 채널
(`channel`)은 그 앱의 것이라 다른 앱의 같은 이름 채널과 섞이지 않는다.

**액션 버튼** — `actions: [{ label, actionId }]`(라벨은 앱이 번역해서). 사용자가 누르면 OS 가
그 앱을 앞으로 띄우고(떠 있지 않았으면 실행해서, 페이지가 뜬 뒤) 알려 주며, 알림은 끝난다.

```javascript
ZylBridge.notify(title, body, { actions: [{ label: zylI18n.t('myapp.reply'), actionId: 'reply' }] });
ZylBridge.onNotificationAction(function (a) {
  /* a = { id, actionId } — 이 앱이 올린 알림의 것만 온다 */
});
```

---

## 5. 권한

앱은 `app.json` 의 `permissions` 로 선언하고, OS 가 **요청마다** 판정한다. 앱이 따로
권한을 요청하는 API 는 없다.

어느 서비스·메서드에 어떤 권한이 필요한지는 `apps/system/permissions.js` 의 표가 정한다 —
읽는 법은 [SECURITY.md §3](SECURITY.md#3-권한). 예약 권한은 제3자 앱이 선언해도 주어지지
않고, 사용자가 설정에서 끈 권한은 그 뒤 호출부터 거부된다.

거부는 조용히 사라지지 않고 reject 로 오므로, 앱은 실패 경로에서 무엇이 왜 안 되는지
사용자에게 알린다. 필요한 권한만 선언한다.

### 앱이 누구인지는 OS 만 말한다

신원을 뜻하는 서비스 인자(`caller`·`container`·`app_id`, 에뮬레이터에서는 `appId`)는 요청
값을 무시하고 OS 가 채운다. 제3자 앱은 다른 앱을 대신해 말할 수 없다 — 무엇이 어떻게
채워지는지는 [SECURITY.md §1](SECURITY.md#1-앱이-누구인지는-os-가-말한다).

---

## 6. 서비스 이벤트 받기

```javascript
ZylBridge.onServiceEvent('notification', 'NotificationPosted', function (d) {
  /* d = 시그널 인자 배열, 선언된 순서대로: [id, app_id, title, body, priority] */
});
```

- 이벤트 이름과 인자는 그 서비스가 내는 D-Bus 시그널 그대로다
  (`runtime/services/<서비스>/` 의 `<signal>` 선언). 인자가 하나면 배열이 아니라 그 값이다.
- 이벤트는 그 서비스를 **부를 수 있는 앱에게만** 가고, 한 앱의 것에 관한 이벤트는 그 주인에게만
  간다 (`OWNED_EVENTS`, [SECURITY.md §3](SECURITY.md#3-권한)).

### 화면에 없을 때 — 푸시와 알람

앱이 화면에 없으면 실행이 이어진다고 기대하지 않는다 — 기기는 메모리가 모자라면 뒤의 앱부터
닫고, 화면이 꺼지고 얼마 뒤 절전에 들어가며, 에뮬레이터는 앞의 앱 하나만 페이지가 살아 있다.
앱마다 서버 연결을 붙들지 않는다. 화면에 없는 앱에 닿는 길은 둘이다.

- **푸시** (`push` 권한) — 앱 서버가 보내는 것. OS 가 서버 연결 하나로 모든 앱의 것을 받는다
  ([PUSH_SERVER.md](PUSH_SERVER.md)).

  ```javascript
  ZylBridge.registerPush(zylI18n.t('myapp.push_title'), zylI18n.t('myapp.push_body'))
    .then(function (endpoint) { /* 내 서버에 끝점을 준다 — 서버는 거기에 POST/PUT */ });
  ZylBridge.onPush(function (m) { /* 떠 있을 때: m.message — 보통 "가져갈 것이 있다"는 신호 */ });
  ```

  앱이 떠 있지 않으면 등록 때 준 제목·본문의 알림이 뜨고, 누르면 앱이 열린다. 끝점은 앱마다
  하나이고 다시 등록해도 같다. 내용은 서버를 지나가므로 민감하면 암호화하거나 신호만 보낸다.
- **알람** (`notification` 권한) — 정한 시각. 기기가 절전 중이어도 깨워서 울린다(벽시계 타이머).
  반복 간격은 1분~366일, 한 앱에 128개까지(같은 `tag` 는 덮어쓰므로 셈에 더해지지 않는다) —
  그 밖은 `set` 이 `false` 다(한도는 `runtime/services/alarm/alarm.h`).

### 웹 플랫폼이 묻는 것 — 카메라·마이크·위치·파일

- `getUserMedia` 의 카메라·마이크는 앱의 `camera`·`microphone` 권한으로 OS 가 정한다(묻는 창
  없음, 사용자가 끄면 거부). 화면 공유·웹 알림(`Notification`)·포인터 잠금은 거부된다 —
  알림은 `ZylBridge.notify`.
- `navigator.geolocation` 은 쓰지 않는다 — 위치는 `location` 서비스(`location` 권한).
- 카메라·마이크·위치·센서는 **앞에 있는 동안만** 쓴다. 홈으로 가거나 화면이 잠기면 OS 가 캡처를
  멈추고(앞으로 돌아오면 이어짐), `location`·`sensors`·`camera` 요청은 `Not in front` 로 거부된다.
  지오펜스 진입·이탈은 뒤에 있어도 받는다.
- `<input type=file>` 은 열리지 않는다(기기에서 데스크톱 대화창이 뜨는 대신). 파일은
  `ZylIntent` 의 PICK 으로 받는다 (7장).

---

## 7. 인텐트 — 다른 앱과 이어지기

앱끼리는 함수를 부르거나 DOM 을 나누지 않는다. 인텐트를 시작하면 OS 가 받을 앱을 정한다
(에뮬레이터 `apps/system/intent.js`, 기기 `runtime/wam/src/intent` — 같은 규칙).

**시작하기**

```javascript
/* 암시적: 이것을 받을 수 있는 앱 (예: 글 공유) */
ZylIntent.startActivity({
  action: ZylIntent.ACTION.SEND,
  mimeType: 'text/plain',
  data: shareText
});

/* 명시적: 특정 앱 열기 */
ZylIntent.startActivity({ targetApp: 'com.example.viewer' });
```

- 받을 앱이 하나면 바로 열린다. 여럿이면 고르는 시트를, 없으면 "열 수 있는 앱 없음" 시트를
  OS 공용 코드(`bridge.js`)가 시작한 앱 화면에 그린다. 앱이 이 UI 를 만들지 않는다.
- 시작한 앱 자신은 후보에 오르지 않는다. 사용자는 제시된 앱 중에서만 고를 수 있다.
- 넘기는 값은 `data`(텍스트·URL 문자열), `extras`(JSON 으로 옮겨지는 객체), `files`(파일)뿐이다.

**파일 건네기** — 파일은 `files` 에 자기 파일 기준 경로로 적는다(10개까지, 하나에 64 MiB
까지). 받는 앱은 경로가 아니라 **자기 사본**을 받는다: OS 가 받는 앱의 `Inbox/`(시스템
앱이면 사용자의 `Downloads/`)에 복사하고, `files` 를 그 앱 기준 경로로 바꿔 건넨다.

```javascript
ZylIntent.startActivity({ action: ZylIntent.ACTION.SEND, mimeType: 'image/*', files: ['photos/cat.jpg'] });
```

**파일 고르기(PICK)** — 사용자의 파일을 받고 싶으면 OS 화면에서 고르게 한다. 고르는 앱은
시스템 앱뿐이고(파일 앱), 답은 `onResult` 로 온다. 고르는 동안 앱이 다시 열릴 수 있으므로
(에뮬레이터는 앱을 바꾸면 문서가 내려간다) 답은 이벤트로 받고 `requestCode` 로 구분한다.

```javascript
ZylIntent.onResult(function (r) {
  /* r = { requestCode, files } — files 는 이 앱의 Inbox/ 에 들어온 사본. 비면 취소 */
  if (r.requestCode === 'attach' && r.files.length) attach(r.files[0]);
});
ZylIntent.startActivity({ action: ZylIntent.ACTION.PICK, mimeType: 'image/*', requestCode: 'attach' });
```

**받기**

1. `app.json` 의 `intents` 에 처리하는 `{ action, mimeType }` 을 선언한다 — 실행 중이 아닌
   앱도 고를 수 있어야 하므로 실행 중에 등록하는 API 는 없다. 처리하지 않는 action 은
   선언하지 않는다.
2. 페이지에서 받는다. 등록 전에 도착한 인텐트는 등록할 때 전달된다.

```javascript
ZylIntent.onIntent(function (it) {
  /* it = { action, data, mimeType, extras, files, sourceApp } — sourceApp 은 OS 가 채우고,
     files 는 이 앱 기준 경로(사본) */
});
```

PICK 을 처리하는 쪽(시스템 앱)은 받은 인텐트로 답한다:
`ZylIntent.setResult(it, { files: [...] })` — 대기 중인 그 하나, 그 처리 앱만 답할 수 있다.

---

## 8. 뒤로 가기·닫기·화면 상태

**뒤로 가기** — 사용자가 뒤로 가면 OS 는 먼저 앞에 있는 앱에 `navigation.back` 을 보낸다.
앱은 곧바로 둘 중 하나로 답한다. 답이 없으면 OS 는 나간 것으로 보고 홈으로 간다
(멈춘 앱이 사용자를 가두지 못하게).

```javascript
window.addEventListener('message', function (e) {
  if (e.source !== window.parent) return;
  var msg;
  try { msg = typeof e.data === 'string' ? JSON.parse(e.data) : e.data; } catch (err) { return; }
  if (!msg || msg.type !== 'navigation.back') return;
  if (detailOpen) {
    closeDetail();
    ZylBridge.sendToSystem({ type: 'navigation.handled' });   /* 앱 안에서 처리함 */
  } else {
    ZylBridge.sendToSystem({ type: 'navigation.exit' });      /* 앱을 떠난다 */
  }
});
```

**닫기** — `ZylBridge.closeApp()` 은 자기 앱을 닫는다. 닫아도 되는지는 OS 가 정한다.

**화면 상태** — OS 가 앱을 떠나기 전에 화면 상태를 묻고 다음 실행 때 돌려줄 수 있다.

```javascript
ZylBridge.onSaveState(function () {
  return { view: currentView, draft: input.value };   /* JSON 으로 옮겨지는 작은 객체, 없으면 null */
});
ZylBridge.onRestoreState(function (st) {
  if (st) showView(st.view, st.draft);
});
```

여기에는 열려 있던 화면·쓰다 만 글 같은 UI 상태만 둔다(크기 제한이 있어 넘치면 버려진다).
OS 는 묻지 않고 앱을 끝낼 수도 있다(메모리가 모자랄 때). 잃으면 안 되는 데이터는 바뀔 때
곧바로 9장의 저장소에 쓴다.

---

## 9. 저장

| 무엇 | 어떻게 | 어디에 |
|---|---|---|
| 작은 비공개 값 | `appdata` 서비스 (`getItem`·`setItem`·`removeItem`·`keys`·`clear`) | 앱 컨테이너 |
| 파일 | `fs` 서비스 (`storage` 또는 `files.*` 권한) | 제3자 앱: 자기 컨테이너가 루트. 시스템 앱: 사용자 공용 파일 |
| 웹 저장소 | `localStorage`·IndexedDB 등 | 앱 출처마다 따로, 컨테이너 안 |

- 제3자 앱은 사용자 공용 폴더를 보지 못하고, 다른 앱의 컨테이너도 보지 못한다.
- 앱을 지우면 컨테이너와 OS 가 그 앱을 위해 갖고 있던 것이 함께 사라진다
  ([SECURITY.md §5](SECURITY.md#5-앱-컨테이너--지우면-아무것도-남지-않는다)). 컨테이너 밖에
  흔적을 남기는 방법은 없다.

---

## 10. 화면 — 폼팩터와 CSS

폼팩터는 하드웨어다. 기기는 이미지의 빌드 프로파일을 선언하고, 에뮬레이터는 하드웨어만 알리며,
`form-factor.js` 가 판정해 페이지에 적용한다. 앱은 판정하지 않고 결과만 읽는다.

- `<html>` 에 `ff-<id>` 클래스와 `data-form-factor`·`data-nav-model` 속성이 붙는다.
  폼팩터별 스타일은 `html.ff-<id>` 선택자로만 쓴다.
- JS 에서 기기 성질이 필요하면 `ZylFormFactor.getProfile()` 을 읽는다
  (`id`, `navModel`, `inputModel`, `fontScale`, `touchTarget`, `density`, `statusBar`).
  화면 크기로 직접 분기하지 않는다.
- OS 가 폼팩터 배율로 루트를 확대한다. 그래서:
  - `vh`/`vw` 대신 `var(--ff-vh)`/`var(--ff-vw)` 를 쓴다 (확대는 뷰포트 단위에 걸리지 않는다).
  - `zoom: var(...)` 을 쓰지 않는다 (`zoom` 은 `var()` 를 무시한다).
  - 누르는 대상의 최소 크기는 `var(--ff-touch-target)`.
- 포인터가 없는 기기(리모컨·다이얼·크라운)에서는 `focus-nav.js` 가 방향키로 포커스를 옮긴다.
  조작할 요소는 `button`·`a[href]`·입력 요소·`[tabindex]`·`[data-focusable]` 로 만든다.
- GPU 없이도 그려져야 한다. `backdrop-filter` 에는 불투명에 가까운 배경색 폴백을 함께 주고,
  무거운 필터·3D 변환은 없어도 동작하는 장식으로만 쓴다.

---

## 11. 화면 키보드

텍스트 입력에 포커스가 가면 OS 키보드가 앱 페이지 안에 뜬다(마우스·키보드가 입력 장치인
폼팩터에서는 뜨지 않는다). 키보드가 떠 있는 동안 `body` 에 `keyboard-visible` 클래스와
`--keyboard-height` 변수가 걸리고 `runtime.css` 가 아래 여백을 준다. 앱은 따로 할 일이 없다.
화면 아래에 고정한 요소가 있으면 `--keyboard-height` 만큼 올린다. 앱이 자기 키보드를 만들지 않는다.

---

## 12. 다국어

사용자에게 보이는 글은 모두 `zylI18n` 을 거친다. 앱 안에 영어·한국어 문장을 박지 않는다.

```javascript
/* i18n.js — 앱 디렉터리 루트. 번역 언어 모두를 같은 키로 */
(function () {
  'use strict';
  if (typeof zylI18n === 'undefined') return;
  zylI18n.addTranslations('ko', { 'myapp.title': '내 앱', 'myapp.greeting': '{name}님 안녕하세요' });
  zylI18n.addTranslations('en', { 'myapp.title': 'My App', 'myapp.greeting': 'Hello, {name}' });
  /* en·ja·zh·es 도 같은 키로 … */
})();
```

- 앱이 갖출 번역 언어는 ko·en·ja·zh·es 다(규칙: [CLAUDE.md](../CLAUDE.md) 4). 그 밖의
  언어에서는 OS 가 영어로 대신한다.
- 키 이름은 `<앱 이름>.<키>`.
- HTML: `data-i18n`(글), `data-i18n-placeholder`, `data-i18n-aria-label` 은 자동으로 번역된다.
- JS: `zylI18n.t(key, { name: … })`, 복수형은 `zylI18n.tp(key, count)` (`key.one`·`key.other`),
  날짜·시간·숫자·용량은 `formatDate`·`formatTime`·`formatRelative`·`formatNumber`·`formatBytes`.
- 언어는 OS 가 정한다. JS 로 만든 글은 `zylI18n.onLocaleChange(fn)` 에서 다시 그린다.
- OS 가 앱 대신 띄우는 글(알림 제목, 알람 `title`)도 앱이 번역해서 넘긴다 — OS 는 앱의
  번역을 갖고 있지 않다.

---

## 13. JavaScript 는 ES5

기기의 WebKitGTK 에서 그대로 돌아야 하므로 앱의 모든 JS 는 ES5 문법만 쓴다.

- `var` 만 (`let`/`const` 없음), `function` 만 (화살표 함수 없음)
- 문자열 연결 (템플릿 리터럴 없음), 프로토타입 (`class` 없음)
- `Promise` 의 `.then()`/`.catch()` (`async`/`await` 없음)
- 구조 분해·전개·기본 매개변수 없음

`tools/zyl validate` 가 기본 위반을 찾는다.

---

## 14. 패키징과 개발 빌드

기기는 **봉인된 앱만** 설치·실행하고, 봉인을 건너뛰는 개발자 모드는 없다 — 언제 다시
확인하고 무엇을 내주는지는 [SECURITY.md §6](SECURITY.md#6-앱-봉인과-스토어-서명), 봉인·서명의
형식과 거부 사유는 [APPSTORE_API.md](APPSTORE_API.md).

- **스토어 배포**: 스토어 운영자가 자기 키로 봉인해 카탈로그에 싣는다.
- **개발 빌드**: 개발자 키로 봉인한다. 스토어가 서명한 개발자 인증서와 프로비저닝 프로필이
  허락한 기기·앱 id·기한 안에서만 설치·실행된다.

```bash
tools/zyl keygen acme       # ~/.zyl/keys/acme.key(비밀), acme.pub — 앱 폴더 밖에 둔다
# acme.pub 와 시험할 기기의 ID(설정 > 이 기기 정보 > 기기 ID)를 스토어 운영자에게 보내
# 개발자 인증서(acme.cert.json)와 프로필(myapp.profile.json)을 .sig 와 함께 받는다
cd myapp
../tools/zyl validate
../tools/zyl package --key ~/.zyl/keys/acme.key \
    --cert ../acme.cert.json --profile ../myapp.profile.json   # → ../<id>-<version>.zpkg
```

`.zpkg` 를 기기의 사용자 파일로 옮겨 파일 앱에서 열면 앱스토어가 확인하고 설치한다.
에뮬레이터의 가상 기기도 자기 기기 ID 를 가지며 같은 규칙으로 검사한다.
패키지 크기·파일 수의 상한은 기기 스토어 서비스가 정한다
(`runtime/services/appstore/store_package.h`, `store_catalog.h`).

---

## 15. 시스템 앱 (이 저장소의 `apps/`)

- 앱 디렉터리 `apps/<이름>/`, 이미지에 싣는 `meson.build` 의 `install_subdir`,
  OS 화이트리스트 `ZylPermissions.SYSTEM_APPS` 에 함께 넣는다.
- 저장소 규칙(파일 헤더, mock 데이터 금지, 에뮬레이터 독립 등)은 [CLAUDE.md](../CLAUDE.md) 를
  따르고 `bash tests/verify-all.sh` 로 확인한다. 앱마다의 성능 예산은
  [CONTRIBUTING.md](../CONTRIBUTING.md#성능-예산).

---

## 16. 문제 찾기

기기에는 셸이 없다. WAM 의 판정은 직렬 콘솔에 남는다(`system/zyl-wam.service` 의
`StandardOutput=journal+console`). 보드에서 콘솔을 보는 법은 `board/<id>/README.md`.

| 증상 | 로그 / 원인 |
|---|---|
| 서비스 호출이 reject | `denied <서비스>.<메서드> (missing permission)` — `permissions` 에 없거나 사용자가 껐거나 예약 권한 |
| 앱이 목록에 없거나 열리지 않음 | `<id> refused: …` — 봉인·인증서·프로필 문제 (사유는 APPSTORE_API.md) |
| 일부 파일만 안 읽힘 | `<id>/<경로> is not as sealed — not served` — 앱이 등록된 뒤 바뀐 파일 |
| 응답이 늦게 reject | 시간 초과 — 서비스 이름·메서드 이름 확인 |
| 홈 화면에 안 보임 | `formFactors` 에 이 기기의 폼팩터가 없음 |
