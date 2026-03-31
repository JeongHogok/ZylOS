<!--
  Zyl OS: App Development Guide
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 앱 개발 가이드

Zyl OS용 앱을 개발하기 위한 공식 가이드입니다.

---

## 목차

1. [시작하기](#1-시작하기)
2. [앱 구조](#2-앱-구조)
3. [매니페스트 (app.json)](#3-매니페스트-appjson)
4. [JS-Native Bridge API](#4-js-native-bridge-api)
5. [Intent 시스템](#5-intent-시스템)
6. [ContentProvider](#6-contentprovider)
7. [런타임 권한](#7-런타임-권한)
8. [클립보드 API](#8-클립보드-api)
9. [UI 디자인 가이드라인](#9-ui-디자인-가이드라인)
10. [다국어 지원 (i18n)](#10-다국어-지원-i18n)
11. [권한 시스템](#11-권한-시스템)
12. [패키징 및 서명](#12-패키징-및-서명)
13. [zyl SDK CLI](#13-zyl-sdk-cli)
14. [앱스토어 배포](#14-앱스토어-배포)
15. [디버깅](#15-디버깅)
16. [API 레퍼런스](#16-api-레퍼런스)

---

## 1. 시작하기

### 요구사항

- 텍스트 에디터 (VS Code 권장)
- 웹 브라우저 (Chrome/Firefox — 개발 중 테스트용)
- Zyl OS SDK CLI (`tools/zyl`) — 패키징/서명에 필요

### 첫 번째 앱 만들기

```bash
# zyl SDK CLI로 프로젝트 생성 (권장)
./tools/zyl init my-app
cd my-app

# 또는 수동 생성
mkdir -p myapp/{css,js,assets}
touch myapp/app.json myapp/index.html myapp/css/style.css myapp/js/app.js
```

**myapp/app.json:**
```json
{
  "id": "com.yourname.myapp",
  "name": "My App",
  "version": "1.0.0",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "iconSvg": "<svg viewBox='0 0 24 24'>...</svg>",
  "permissions": [],
  "min_os_version": "0.1.0"
}
```

**myapp/index.html:**
```html
<!DOCTYPE html>
<html lang="ko">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
  <title>My App</title>
  <link rel="stylesheet" href="css/style.css">
</head>
<body>
  <header class="app-header">
    <h1>My App</h1>
  </header>
  <main id="content">
    <p>Hello, Zyl OS!</p>
  </main>
  <script src="js/app.js"></script>
</body>
</html>
```

---

## 2. 앱 구조

```
myapp/
├── app.json          # 앱 매니페스트 (필수)
├── index.html        # 진입점 (필수)
├── css/
│   └── style.css     # 스타일시트
├── js/
│   ├── app.js        # 애플리케이션 로직
│   └── i18n.js       # 앱별 번역 데이터 (필수, 5개 언어)
└── assets/
    └── icon.png      # 앱 아이콘 (192x192px)
```

### 파일 크기 제한

| 항목 | 제한 |
|------|------|
| 전체 패키지 | 50 MB |
| 단일 파일 | 10 MB |
| 아이콘 | 1 MB |
| 총 파일 수 | 500개 |

---

## 3. 매니페스트 (app.json)

```json
{
  "id": "com.yourname.myapp",
  "name": "My Awesome App",
  "name_i18n": {
    "ko": "멋진 앱",
    "en": "My Awesome App",
    "ja": "素晴らしいアプリ"
  },
  "version": "1.2.3",
  "description": "A sample Zyl OS application",
  "author": "Your Name",
  "email": "you@example.com",
  "website": "https://example.com",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "iconSvg": "<svg viewBox='0 0 24 24'>...</svg>",
  "permissions": [
    "notification.create",
    "storage.local"
  ],
  "min_os_version": "0.1.0",
  "category": "utility"
}
```

### 필수 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `id` | string | 역도메인 형식 앱 ID |
| `name` | string | 앱 이름 (기본 언어) |
| `version` | string | 시맨틱 버전 (major.minor.patch) |
| `entry` | string | HTML 진입점 파일 경로 |
| `icon` | string | 앱 아이콘 경로 (192x192 PNG 권장) |
| `iconSvg` | string | 인라인 SVG 아이콘 (ZylAppRegistry가 홈/런처에서 사용) |

---

## 4. JS-Native Bridge API

Zyl OS는 `ZylBridge.requestService()`를 통해 서비스를 호출합니다.
`requestService()`는 **Promise를 반환**합니다. requestId 기반으로 응답이 매칭됩니다.

```javascript
// requestService(service, method, params) → Promise
ZylBridge.requestService('power', 'getState', {})
  .then(function(state) {
    console.log('Battery:', state.battery_level);
  })
  .catch(function(err) {
    console.error('Service error:', err);
  });
```

### 4.1 앱 관리

```javascript
// 다른 앱 실행
navigator.system.launch('com.zylos.settings');

// 앱 종료
navigator.system.app.close();

// 앱 최소화 (백그라운드)
navigator.system.app.minimize();
```

### 4.2 알림

```javascript
navigator.system.notification.create('제목', '내용', {
  icon: 'assets/notif-icon.png',
  actions: [
    { id: 'reply', label: '답장' },
    { id: 'dismiss', label: '닫기' }
  ]
});
```

### 4.3 배터리

```javascript
navigator.system.battery.getLevel().then(function(level) {
  console.log('Battery:', level + '%');
});
```

### 4.4 WiFi

```javascript
navigator.system.wifi.scan().then(function(networks) {
  networks.forEach(function(net) {
    console.log(net.ssid, net.signal, net.secured);
  });
});
```

### 4.5 로컬 스토리지

```javascript
navigator.system.storage.set('key', 'value');
navigator.system.storage.get('key').then(function(val) {
  console.log(val);
});
```

### 4.6 Bridge 미지원 환경 (브라우저 테스트)

```javascript
// ZylBridge가 없는 환경에서는 콘솔 폴백 자동 제공
ZylBridge.launch('com.example.app');
// → 브라우저: console.log('[ZylBridge] launch: com.example.app')
```

---

## 5. Intent 시스템

Android 스타일 인텐트로 앱 간 기능을 연결합니다.

### 5.1 인텐트 필터 등록 (앱 초기화 시)

```javascript
// 내 앱이 처리할 수 있는 인텐트 등록
ZylIntent.registerFilter('com.yourname.myapp', ZylIntent.ACTION.VIEW, 'image/*');
ZylIntent.registerFilter('com.yourname.myapp', ZylIntent.ACTION.SEND, 'text/plain');
```

### 5.2 명시적 인텐트 (특정 앱 지정)

```javascript
// 연락처 앱을 명시적으로 기동
ZylIntent.startActivity({
  targetApp: 'com.zylos.contacts',
  action: ZylIntent.ACTION.PICK,
  extras: { returnResult: true }
});
```

### 5.3 암시적 인텐트 (액션 매칭)

```javascript
// 이미지를 처리할 수 있는 앱 검색 후 기동
ZylIntent.startActivity({
  action: ZylIntent.ACTION.VIEW,
  mimeType: 'image/jpeg',
  data: 'content://com.zylos.gallery/image/42'
});
// → 매칭 앱 1개: 즉시 기동
// → 매칭 앱 복수: 앱 선택 다이얼로그 표시
```

### 5.4 표준 액션

| 액션 | 값 | 용도 |
|------|-----|------|
| VIEW | `zyl.intent.action.VIEW` | 데이터 보기 |
| SEND | `zyl.intent.action.SEND` | 데이터 전송/공유 |
| PICK | `zyl.intent.action.PICK` | 데이터 선택 |
| EDIT | `zyl.intent.action.EDIT` | 데이터 편집 |
| DIAL | `zyl.intent.action.DIAL` | 전화 걸기 |
| CAPTURE | `zyl.intent.action.CAPTURE` | 카메라/오디오 캡처 |
| SEARCH | `zyl.intent.action.SEARCH` | 검색 |

---

## 6. ContentProvider

URI 기반 앱 간 데이터 공유 게이트웨이.

### 6.1 프로바이더 등록 (데이터 제공 앱)

```javascript
ZylContentProvider.registerProvider('com.yourname.myapp.data', {
  query: function(path, projection) {
    // path: 'items', 'items/42' 등
    if (path === 'items') {
      return Promise.resolve([
        { id: 1, title: 'Item 1' },
        { id: 2, title: 'Item 2' }
      ]);
    }
    return Promise.resolve([]);
  },
  insert: function(path, values) {
    // 새 항목 삽입
    return Promise.resolve({ id: 3 });
  },
  update: function(path, values) {
    // 기존 항목 수정
    return Promise.resolve({ affected: 1 });
  },
  'delete': function(path) {
    // 항목 삭제
    return Promise.resolve({ affected: 1 });
  }
});
```

### 6.2 데이터 쿼리 (데이터 소비 앱)

```javascript
// content://authority/path 형식
ZylContentProvider.query(
  'com.yourname.viewer',                          // 내 앱 ID (권한 체크)
  'content://com.yourname.myapp.data/items'       // URI
).then(function(rows) {
  rows.forEach(function(row) {
    console.log(row.id, row.title);
  });
});

// 특정 항목
ZylContentProvider.query(
  'com.yourname.viewer',
  'content://com.yourname.myapp.data/items/42'
).then(function(rows) {
  console.log(rows[0]);
});
```

### 6.3 시스템 ContentProvider URI 예시

| URI | 설명 |
|-----|------|
| `content://com.zylos.contacts/all` | 전체 연락처 |
| `content://com.zylos.contacts/id/42` | 특정 연락처 |
| `content://com.zylos.gallery/images` | 갤러리 이미지 목록 |

---

## 7. 런타임 권한

위험 권한은 실제 사용 시점에 사용자 승인을 요청합니다.

### 7.1 권한 요청 패턴

```javascript
// 카메라 권한 요청
ZylPermissionDialog.requestPermission('com.yourname.myapp', 'camera')
  .then(function(granted) {
    if (granted) {
      // 카메라 기능 사용
      startCamera();
    } else {
      // 거부 처리
      showPermissionRationale();
    }
  });
```

### 7.2 권한 확인 후 요청

```javascript
// 이미 승인된 권한인지 먼저 확인
if (ZylPermissions.hasPermission('com.yourname.myapp', 'location')) {
  getLocation();
} else {
  ZylPermissionDialog.requestPermission('com.yourname.myapp', 'location')
    .then(function(granted) {
      if (granted) getLocation();
    });
}
```

### 7.3 위험 권한 목록

`camera`, `location`, `contacts`, `messaging`, `telephony`, `storage`, `microphone`, `bluetooth`

이 권한들은 반드시 런타임 다이얼로그를 통해 사용자 승인이 필요합니다.
`app.json`에 선언 + 런타임 승인 둘 다 필요합니다.

---

## 8. 클립보드 API

앱 간 텍스트 데이터를 클립보드로 공유합니다.

### 8.1 클립보드에 복사

```javascript
ZylBridge.requestService('clipboard', 'copy', {
  text: '복사할 텍스트',
  mimeType: 'text/plain',      // 선택사항, 기본값: 'text/plain'
  sourceApp: 'com.yourname.myapp'  // 선택사항
}).then(function(result) {
  console.log('복사됨:', result);
});
```

### 8.2 클립보드에서 붙여넣기

```javascript
ZylBridge.requestService('clipboard', 'paste', {})
  .then(function(data) {
    console.log('클립보드 텍스트:', data.text);
    console.log('MIME 타입:', data.mimeType);
    console.log('출처 앱:', data.sourceApp);
    console.log('복사 시각:', new Date(data.timestamp));
  });
```

### 8.3 클립보드 초기화

```javascript
ZylBridge.requestService('clipboard', 'clear', {})
  .then(function() {
    console.log('클립보드 지워짐');
  });
```

---

## 9. UI 디자인 가이드라인

### 9.1 색상 팔레트

| 용도 | 값 | 변수명 |
|------|-----|--------|
| 배경 | `#0a0a1a` | `--bg` |
| 표면 | `rgba(255,255,255,0.06)` | `--surface` |
| 텍스트 | `#ffffff` | `--text` |
| 보조 텍스트 | `rgba(255,255,255,0.5)` | `--text-secondary` |
| 액센트 | `#4a9eff` | `--accent` |
| 위험 | `#ef4444` | `--danger` |
| 성공 | `#22c55e` | `--success` |

### 9.2 필수 CSS 변수

```css
main {
  padding-top: var(--safe-top);     /* 36px — 상태바 높이 */
  padding-bottom: var(--safe-bottom); /* 24px — 하단 제스처 영역 */
}
```

### 9.3 컴포넌트 스타일

```css
.card {
  background: rgba(255, 255, 255, 0.06);
  border-radius: 16px;
  padding: 16px;
}

.btn-primary {
  background: var(--accent);
  color: white;
  border: none;
  border-radius: 12px;
  padding: 12px 24px;
}

/* backdrop-filter는 반드시 불투명 폴백 제공 (opacity ≥ 0.85) */
.glass {
  background: rgba(10, 10, 26, 0.90); /* 폴백 */
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
}
```

### 9.4 터치 인터랙션

- 터치 타겟 최소 44×44px
- `:active` 상태에 `transform: scale(0.95)` 적용
- 스와이프 임계값: 50px
- 긴 누르기: 500ms

---

## 10. 다국어 지원 (i18n)

### 10.1 앱별 i18n.js 작성 (필수)

**모든 앱은 `js/i18n.js` 파일을 포함해야 합니다.** 5개 언어(ko/en/ja/zh/es) 번역 필수.

```javascript
// myapp/js/i18n.js
(function () {
  'use strict';
  if (!window.zylI18n || !window.zylI18n.addTranslations) return;

  zylI18n.addTranslations('ko', { 'myapp.title': '내 앱' });
  zylI18n.addTranslations('en', { 'myapp.title': 'My App' });
  zylI18n.addTranslations('ja', { 'myapp.title': 'マイアプリ' });
  zylI18n.addTranslations('zh', { 'myapp.title': '我的应用' });
  zylI18n.addTranslations('es', { 'myapp.title': 'Mi App' });
})();
```

### 10.2 HTML/JS에서 사용

```html
<span data-i18n="myapp.title">내 앱</span>
```

```javascript
var text = zylI18n.t('myapp.title');
var msg  = zylI18n.t('myapp.welcome', { name: '사용자' });
```

---

## 11. 권한 시스템

> **중요 (v0.1.0)**: 권한은 **실행 시점에 강제 적용**됩니다.
> `app.json`에 선언되지 않은 권한으로 서비스를 호출하면 요청이 **차단**됩니다.

### 11.1 권한 목록

| 권한 | 설명 | 위험도 |
|------|------|--------|
| `notification.create` | 알림 생성 | 보통 |
| `storage.local` | 앱 전용 저장소 | 낮음 |
| `storage.shared` | 공유 저장소 | 높음 |
| `camera` | 카메라 접근 | 높음 |
| `microphone` | 마이크 접근 | 높음 |
| `location` | 위치 정보 | 높음 |
| `wifi` | WiFi 스캔/연결 | 보통 |
| `bluetooth` | BT 스캔/연결 | 보통 |
| `contacts` | 연락처 접근 | 높음 |
| `messaging` | 메시지 접근 | 높음 |
| `telephony` | 전화 기능 | 높음 |
| `battery` | 배터리 정보 | 낮음 |
| `system.info` | 시스템 정보 | 낮음 |

### 11.2 권한 차단 예시

```javascript
// app.json에 "notification.create" 없으면 차단
navigator.system.notification.create('제목', '내용');
// → Error: Permission denied: notification.create not declared in app.json
```

---

## 12. 패키징 및 서명

### 12.1 zyl SDK로 패키징 (권장)

```bash
# 앱 디렉토리에서
./tools/zyl package       # .ospkg 생성 (서명 없음)
./tools/zyl sign developer.key developer.crt   # RSA-2048 서명
```

### 12.2 수동 패키징

```bash
cd myapp/
zip -r ../myapp.ospkg . -x ".*"
```

### 12.3 개발자 인증서 생성

```bash
openssl genrsa -out developer.key 2048
openssl req -new -x509 -key developer.key -out developer.crt \
  -days 365 -subj "/CN=Your Name/O=Your Org/C=KR"
```

### 12.4 수동 서명

```bash
# app.json SHA-256 해시 생성
sha256sum myapp/app.json | awk '{print $1}' > manifest.hash

# RSA 서명
openssl dgst -sha256 -sign developer.key -out SIGNATURE manifest.hash

# 인증서 지문 생성
openssl rsa -in developer.key -pubout -outform DER | sha256sum | awk '{print $1}' > CERT

# 서명 파일을 패키지에 추가
cp SIGNATURE CERT myapp/
zip -r myapp-signed.ospkg myapp/ -x ".*"
```

### 12.5 개발자 모드 (서명 우회)

```
설정 → 보안 → 개발자 모드 → 활성화
```

---

## 13. zyl SDK CLI

`tools/zyl`은 개발자 워크플로우 전체를 지원하는 CLI입니다.

```bash
# 새 앱 프로젝트 생성 (보일러플레이트 자동 생성)
./tools/zyl init <app-name>

# 에뮬레이터에서 개발 모드 실행
./tools/zyl dev

# app.json + 코드 구조 검증
./tools/zyl validate

# .ospkg 패키지 생성 (서명 없음)
./tools/zyl package

# RSA-2048 서명
./tools/zyl sign <key-file> <cert-file>

# 개발자 키 쌍 생성
./tools/zyl keygen <developer-name>
```

### zyl keygen 출력
```
<name>.key   — RSA-2048 비밀키 (절대 배포 금지!)
<name>.crt   — 자체 서명 인증서
<name>.pub   — PEM 공개키
```

### zyl validate 검사 항목
- `app.json` 필수 필드 존재 여부
- `iconSvg` 필드 유효성
- `permissions` 배열 유효성
- `entry` 파일 존재 여부
- ES5 호환성 기본 검사

---

## 14. 앱스토어 배포

### 등록 절차

1. **개발자 등록**: [developer.zyl-os.com](https://developer.zyl-os.com) 에서 계정 생성
2. **인증서 발급**: 개발자 포털에서 공식 인증서 발급 요청
3. **앱 제출**: 서명된 `.ospkg` 파일 업로드
4. **심사**: 보안 + 품질 검토 (3-5 영업일)
5. **배포**: 승인 후 앱스토어에 공개

### 심사 기준

- **보안**: XSS, 인젝션, 데이터 유출 취약점 없어야 함
- **성능**: 앱 시작 3초 이내, 메모리 100MB 이하
- **UX**: 터치 타겟 44px 이상, 접근성 기본 준수
- **권한**: 기능에 필요한 최소 권한만 요청

---

## 15. 디버깅

### 브라우저에서 테스트

```bash
python3 -m http.server 8080 --directory myapp/
# 브라우저에서 http://localhost:8080 접속
```

Bridge API가 없는 환경에서는 `ZylBridge`가 자동으로 콘솔 폴백을 제공합니다.

### Zyl OS에서 디버깅

```bash
# 앱 로그 확인
journalctl -u zyl-wam -f

# WebKit Inspector 활성화 (개발자 모드)
export WEBKIT_INSPECTOR_SERVER=0.0.0.0:8090
# 호스트 PC Chrome DevTools: chrome://inspect → Configure → [device-ip]:8090
```

### 공통 문제 해결

| 문제 | 원인 | 해결 |
|------|------|------|
| 앱이 설치되지 않음 | 서명 없음 | `zyl sign` 또는 개발자 모드 활성화 |
| Bridge API 작동 안함 | 일반 브라우저 | ZylBridge 폴백 확인 |
| 권한 차단 에러 | app.json 미선언 | `permissions` 배열에 권한 추가 |
| CSS 깨짐 | safe area 미적용 | `--safe-top`, `--safe-bottom` 사용 |
| requestService 응답 없음 | timeout (기본 10초) | 서비스 이름/메서드명 확인 |

---

## 16. API 레퍼런스

### ZylBridge

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `requestService(service, method, params)` | Promise | 서비스 호출 (requestId 매칭) |
| `requestServiceFire(service, method, params)` | void | fire-and-forget 서비스 호출 |
| `sendToSystem(message)` | void | OS에 메시지 전송 |

### ZylIntent

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `registerFilter(appId, action, mimeType)` | void | 인텐트 필터 등록 |
| `startActivity(intent)` | void | 인텐트 기동 |
| `resolve(intent)` | string[] | 처리 가능한 앱 목록 반환 |
| `ACTION.VIEW` | string | 표준 액션 상수 |

### ZylContentProvider

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `registerProvider(authority, impl)` | void | 프로바이더 등록 |
| `query(callerAppId, uri, projection)` | Promise | 데이터 쿼리 |
| `insert(callerAppId, uri, values)` | Promise | 데이터 삽입 |
| `update(callerAppId, uri, values)` | Promise | 데이터 수정 |
| `delete(callerAppId, uri)` | Promise | 데이터 삭제 |

### ZylPermissionDialog

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `requestPermission(appId, permission)` | Promise\<boolean\> | 런타임 권한 요청 |

### JS 서비스 (29개 — apps/system/services/)

앱에서 `ZylBridge.requestService(service, method, params)`로 접근:

| # | 서비스 | 주요 메서드 |
|---|--------|-----------|
| 1 | fs | getDirectory, getFileContent, writeFile, mkdir, remove |
| 2 | device | getInfo |
| 3 | storage | getUsage |
| 4 | apps | getInstalled, install, uninstall, getAppInfo |
| 5 | settings | get, set, getAll |
| 6 | terminal | exec |
| 7 | wifi | scan, connect, getConnected |
| 8 | bluetooth | getDevices, getConnected |
| 9 | browser | navigate, getHistory |
| 10 | notification | create, list, dismiss, clearAll |
| 11 | power | getState, setBrightness, getBattery |
| 12 | display | getInfo, setRotation, getDPI |
| 13 | input | getKeyboardState, setIME |
| 14 | sensors | getAccelerometer, getGyroscope, getProximity, getLight |
| 15 | location | getPosition |
| 16 | telephony | getSignal, getSIMInfo, call, sendSMS |
| 17 | usb | getState, setMode |
| 18 | user | getCurrentUser, listUsers, switchUser |
| 19 | credential | store, lookup, delete |
| 20 | appstore | search, install, uninstall, listInstalled |
| 21 | updater | checkUpdate, getVersion |
| 22 | sandbox | getPolicy |
| 23 | logger | getLogs, clearLogs |
| 24 | accessibility | getSettings, setFontScale, setHighContrast |
| 25 | audio | getVolume, setVolume, playNotificationSound, vibrate |
| 26 | contacts | getAll, getById, create, update, delete, search |
| 27 | messaging | getConversations, getMessages, sendMessage, deleteMessage |
| 28 | clipboard | copy, paste, clear |
| 29 | network | fetch |

### zylI18n (공유 모듈)

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `t(key, params)` | string | 번역 |
| `formatDate(date)` | string | 날짜 포맷 |
| `setLocale(locale)` | void | 로케일 변경 |
| `getLocale()` | string | 현재 로케일 |
| `addTranslations(locale, keys)` | void | 앱별 번역 키 등록 |
| `applyTranslations()` | void | DOM data-i18n 재적용 |

---

## 라이선스

Zyl OS SDK와 이 가이드는 MIT 라이선스 하에 배포됩니다.
