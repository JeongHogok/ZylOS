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
5. [UI 디자인 가이드라인](#5-ui-디자인-가이드라인)
6. [다국어 지원 (i18n)](#6-다국어-지원-i18n)
7. [권한 시스템](#7-권한-시스템)
8. [패키징 및 서명](#8-패키징-및-서명)
9. [앱스토어 배포](#9-앱스토어-배포)
10. [디버깅](#10-디버깅)
11. [API 레퍼런스](#11-api-레퍼런스)

---

## 1. 시작하기

### 요구사항

- 텍스트 에디터 (VS Code 권장)
- 웹 브라우저 (Chrome/Firefox - 개발 중 테스트용)
- Zyl OS SDK CLI (선택사항)

### 첫 번째 앱 만들기

```bash
# 앱 디렉토리 생성
mkdir -p myapp/{css,js,assets}

# 기본 파일 생성
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
├── icon.png          # 앱 아이콘 192x192px (필수)
├── css/
│   └── style.css     # 스타일시트
├── js/
│   └── app.js        # 애플리케이션 로직
├── assets/
│   ├── icon.png      # 앱 아이콘
│   └── ...           # 이미지, 폰트, 기타 에셋
└── js/
    ├── app.js        # 애플리케이션 로직
    └── i18n.js       # 앱별 번역 데이터 (필수, 5개 언어)
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

앱의 메타데이터와 권한을 정의하는 JSON 파일입니다.

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
  "description_i18n": {
    "ko": "Zyl OS 샘플 애플리케이션",
    "en": "A sample Zyl OS application"
  },
  "author": "Your Name",
  "email": "you@example.com",
  "website": "https://example.com",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "permissions": [
    "notification.create",
    "storage.local"
  ],
  "min_os_version": "0.1.0",
  "category": "utility",
  "tags": ["productivity", "tools"],
  "screenshots": [
    "assets/screenshot1.png",
    "assets/screenshot2.png"
  ]
}
```

### 필수 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `id` | string | 역도메인 형식 앱 ID. 글로벌 고유값 |
| `name` | string | 앱 이름 (기본 언어) |
| `version` | string | 시맨틱 버전 (major.minor.patch) |
| `entry` | string | HTML 진입점 파일 경로 |
| `icon` | string | 앱 아이콘 경로 (192x192 PNG 권장) |

### 선택 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `name_i18n` | object | 다국어 앱 이름 |
| `description` | string | 앱 설명 |
| `permissions` | string[] | 요청 권한 목록 |
| `min_os_version` | string | 최소 OS 버전 |
| `category` | string | 카테고리 (아래 참조) |

### 카테고리

`game`, `utility`, `productivity`, `entertainment`, `social`,
`education`, `health`, `finance`, `news`, `developer`

---

## 4. JS-Native Bridge API

Zyl OS는 `navigator.system` 객체를 통해 네이티브 기능을 제공합니다.

### 4.1 앱 관리

```javascript
// 다른 앱 실행
navigator.system.launch('com.zylos.settings');

// 현재 앱 정보
console.log(navigator.system.app.id);      // "com.yourname.myapp"
console.log(navigator.system.app.name);    // "My App"
console.log(navigator.system.app.version); // "1.0.0"

// 앱 종료
navigator.system.app.close();

// 앱 최소화 (백그라운드)
navigator.system.app.minimize();
```

### 4.2 알림

```javascript
// 알림 생성
navigator.system.notification.create('제목', '내용', {
  icon: 'assets/notif-icon.png',
  actions: [
    { id: 'reply', label: '답장' },
    { id: 'dismiss', label: '닫기' }
  ]
});

// 알림 액션 리스너
navigator.system.notification.onAction(function(actionId) {
  if (actionId === 'reply') {
    // 답장 처리
  }
});
```

### 4.3 배터리

```javascript
// 배터리 레벨 조회 (Promise)
navigator.system.battery.getLevel().then(function(level) {
  console.log('Battery:', level + '%');
});

// 충전 상태
navigator.system.battery.isCharging().then(function(charging) {
  console.log('Charging:', charging);
});
```

### 4.4 네트워크

```javascript
// WiFi 스캔
navigator.system.wifi.scan().then(function(networks) {
  networks.forEach(function(net) {
    console.log(net.ssid, net.signal, net.secured);
  });
});

// 연결 상태
navigator.system.network.getStatus().then(function(status) {
  console.log(status.type);      // "wifi" | "cellular" | "none"
  console.log(status.connected); // true | false
});
```

### 4.5 로컬 스토리지

```javascript
// 앱 전용 키-값 저장소 (앱 삭제 시 함께 삭제)
navigator.system.storage.set('key', 'value');
navigator.system.storage.get('key').then(function(val) {
  console.log(val); // "value"
});
navigator.system.storage.remove('key');
```

### 4.6 시스템 정보

```javascript
navigator.system.info.getDeviceName();     // "BPI-F3"
navigator.system.info.getOsVersion();      // "0.1.0"
navigator.system.info.getLocale();         // "ko"
navigator.system.info.getTimezone();       // "Asia/Seoul"
```

### 4.7 Bridge 미지원 환경 (브라우저 테스트)

Bridge가 없는 환경(일반 브라우저)에서도 앱이 동작하도록 폴백을 제공합니다:

```javascript
// shared/bridge.js가 자동으로 폴백을 제공
// navigator.system이 없으면 콘솔 로그만 출력
ZylBridge.launch('com.example.app');
// → Bridge 없으면: console.log('[ZylBridge] launch: com.example.app')
```

---

## 5. UI 디자인 가이드라인

### 5.1 색상 팔레트

| 용도 | 값 | 변수명 |
|------|-----|--------|
| 배경 | `#0a0a1a` | `--bg` |
| 표면 | `rgba(255,255,255,0.06)` | `--surface` |
| 텍스트 | `#ffffff` | `--text` |
| 보조 텍스트 | `rgba(255,255,255,0.5)` | `--text-secondary` |
| 액센트 | `#4a9eff` | `--accent` |
| 위험 | `#ef4444` | `--danger` |
| 성공 | `#22c55e` | `--success` |

### 5.2 필수 CSS 변수

```css
:root {
  --safe-top: 36px;     /* 상태바 높이 */
  --safe-bottom: 24px;  /* 하단 제스처 영역 */
}

/* 메인 콘텐츠는 safe area 내에 배치 */
main {
  padding-top: var(--safe-top);
  padding-bottom: var(--safe-bottom);
}
```

### 5.3 컴포넌트 스타일

```css
/* 카드 */
.card {
  background: rgba(255, 255, 255, 0.06);
  border-radius: 16px;
  padding: 16px;
  border: 1px solid rgba(255, 255, 255, 0.04);
}

/* 버튼 */
.btn-primary {
  background: var(--accent);
  color: white;
  border: none;
  border-radius: 12px;
  padding: 12px 24px;
  font-size: 15px;
  cursor: pointer;
}

/* 글래스모피즘 효과 */
.glass {
  background: rgba(255, 255, 255, 0.08);
  backdrop-filter: blur(20px);
  -webkit-backdrop-filter: blur(20px);
  border: 1px solid rgba(255, 255, 255, 0.06);
  border-radius: 16px;
}
```

### 5.4 터치 인터랙션

- 터치 타겟 최소 44x44px
- `:active` 상태에 `transform: scale(0.95)` 적용
- 스와이프 임계값: 50px
- 긴 누르기: 500ms

### 5.5 반응형 레이아웃

```css
/* 기본: 모바일 세로 */
.container { padding: 0 16px; }

/* 태블릿/가로 모드 */
@media (min-width: 768px) {
  .container { padding: 0 32px; max-width: 720px; margin: 0 auto; }
}
```

---

## 6. 다국어 지원 (i18n)

Zyl OS는 **공유 엔진 + 앱별 번역 데이터** 아키텍처를 사용합니다.
각 앱은 반드시 자체 `i18n.js` 파일에서 번역 데이터를 등록해야 합니다.

### 6.1 아키텍처 개요

```
shared/i18n.js          ← 번역 엔진 (t(), formatDate(), DOM 자동 번역)
  ↑
앱별 js/i18n.js          ← 앱이 addTranslations()로 자체 키 등록
  ↑
앱 HTML (data-i18n)      ← DOM 자동 번역
```

### 6.2 앱별 i18n.js 작성 (필수)

**모든 앱은 `js/i18n.js` 파일을 포함해야 합니다.** 5개 언어(ko/en/ja/zh/es) 번역 필수.

```javascript
// myapp/js/i18n.js
(function () {
  'use strict';
  if (!window.zylI18n || !window.zylI18n.addTranslations) return;

  zylI18n.addTranslations('ko', {
    'myapp.title': '내 앱',
    'myapp.welcome': '{name}님 환영합니다'
  });
  zylI18n.addTranslations('en', {
    'myapp.title': 'My App',
    'myapp.welcome': 'Welcome, {name}'
  });
  zylI18n.addTranslations('ja', {
    'myapp.title': 'マイアプリ',
    'myapp.welcome': '{name}さん、ようこそ'
  });
  zylI18n.addTranslations('zh', {
    'myapp.title': '我的应用',
    'myapp.welcome': '欢迎，{name}'
  });
  zylI18n.addTranslations('es', {
    'myapp.title': 'Mi App',
    'myapp.welcome': 'Bienvenido, {name}'
  });
})();
```

### 6.3 HTML에서 사용

```html
<!-- 공유 엔진 먼저 로드, 앱 i18n.js 이후 로드 -->
<script src="../../shared/i18n.js"></script>
<script src="js/i18n.js"></script>

<!-- 텍스트 번역 -->
<span data-i18n="myapp.title">내 앱</span>

<!-- placeholder 번역 -->
<input data-i18n-placeholder="search.hint" placeholder="검색...">
```

### 6.4 JavaScript에서 사용

```javascript
// 번역
var text = zylI18n.t('myapp.title');

// 파라미터 치환
var msg = zylI18n.t('myapp.welcome', { name: '사용자' });

// 날짜 포맷
var dateStr = zylI18n.formatDate(new Date());

// 로케일 변경 감지
zylI18n.onLocaleChange(function(newLocale) {
  // UI 업데이트
});
```

### 6.5 지원 언어

시스템 필수 5개 언어: 한국어(ko), English(en), 日本語(ja), 中文(zh), Español(es)

앱에서 추가 언어를 지원할 수 있으며, 시스템에 없는 언어도 앱 단위로 추가 가능합니다.

> **규칙**: 모든 앱은 반드시 5개 언어 번역을 포함한 `js/i18n.js`를 가져야 합니다.

---

## 7. 권한 시스템

앱이 민감한 시스템 기능에 접근하려면 `app.json`에 권한을 선언해야 합니다.

### 7.1 권한 목록

| 권한 | 설명 | 위험도 |
|------|------|--------|
| `notification.create` | 알림 생성 | 보통 |
| `notification.read` | 알림 읽기 | 보통 |
| `storage.local` | 앱 전용 저장소 | 낮음 |
| `storage.shared` | 공유 저장소 접근 | 높음 |
| `camera` | 카메라 접근 | 높음 |
| `microphone` | 마이크 접근 | 높음 |
| `location` | 위치 정보 | 높음 |
| `wifi` | WiFi 스캔/연결 | 보통 |
| `bluetooth` | BT 스캔/연결 | 보통 |
| `contacts` | 연락처 접근 | 높음 |
| `phone` | 전화 기능 | 높음 |
| `battery` | 배터리 정보 | 낮음 |
| `system.info` | 시스템 정보 | 낮음 |

### 7.2 권한 요청 흐름

1. `app.json`에 권한 선언
2. 설치 시 사용자에게 권한 목록 표시
3. **높음** 위험도 권한은 설치 시 명시적 승인 필요
4. **낮음/보통** 위험도 권한은 자동 승인
5. 사용자는 설정에서 언제든 권한 취소 가능

---

## 8. 패키징 및 서명

### 8.1 .ospkg 패키지 생성

```bash
# 앱 디렉토리에서
cd myapp/

# 패키지 생성 (ZIP 기반)
zip -r ../myapp.ospkg . -x ".*"
```

### 8.2 개발자 인증서 생성

```bash
# RSA-2048 키 쌍 생성
openssl genrsa -out developer.key 2048
openssl rsa -in developer.key -pubout -out developer.pub

# 인증서 서명 요청 (CSR) 생성
openssl req -new -key developer.key -out developer.csr \
  -subj "/CN=Your Name/O=Your Org/C=KR"
```

### 8.3 패키지 서명

```bash
# app.json의 SHA-256 해시 생성
sha256sum myapp/app.json | awk '{print $1}' > manifest.hash

# RSA 서명
openssl dgst -sha256 -sign developer.key -out SIGNATURE manifest.hash

# 인증서 지문 생성
openssl rsa -in developer.pub -pubin -outform DER | sha256sum | awk '{print $1}' > CERT

# 서명 파일을 패키지에 추가
cd myapp/
cp ../SIGNATURE ../CERT .
zip -r ../myapp-signed.ospkg . -x ".*"
```

### 8.4 개발자 모드 (서명 우회)

개발 중에는 서명 없이도 앱을 설치할 수 있습니다:

```
설정 → 보안 → 개발자 모드 → 활성화
```

**주의:** 개발자 모드는 보안 위험이 있으므로 개발 환경에서만 사용하세요.

---

## 9. 앱스토어 배포

### 9.1 등록 절차

1. **개발자 등록**: [developer.zyl-os.com](https://developer.zyl-os.com) 에서 계정 생성
2. **인증서 발급**: 개발자 포털에서 공식 인증서 발급 요청
3. **앱 제출**: 서명된 `.ospkg` 파일 업로드
4. **심사**: 보안 + 품질 검토 (3-5 영업일)
5. **배포**: 승인 후 앱스토어에 공개

### 9.2 심사 기준

- **보안**: XSS, 인젝션, 데이터 유출 취약점 없어야 함
- **성능**: 앱 시작 3초 이내, 메모리 100MB 이하
- **UX**: 터치 타겟 44px 이상, 접근성 기본 준수
- **콘텐츠**: 불법/유해 콘텐츠 금지
- **권한**: 기능에 필요한 최소 권한만 요청

### 9.3 앱 업데이트

```json
{
  "version": "1.1.0"
}
```

버전을 올린 새 `.ospkg`를 제출하면 기존 사용자에게 자동으로 업데이트가 제공됩니다.

---

## 10. 디버깅

### 10.1 브라우저에서 테스트

```bash
# 로컬 서버로 앱 실행
python3 -m http.server 8080 --directory myapp/
# 브라우저에서 http://localhost:8080 접속
```

Bridge API가 없는 환경에서는 `ZylBridge`가 자동으로 콘솔 로그 폴백을 제공합니다.

### 10.2 Zyl OS에서 디버깅

```bash
# 앱 로그 확인
journalctl -u zyl-wam -f

# WebKit Inspector 활성화 (개발자 모드)
export WEBKIT_INSPECTOR_SERVER=0.0.0.0:8090
# 호스트 PC에서 Chrome DevTools로 접속:
# chrome://inspect → Configure → [device-ip]:8090
```

### 10.3 공통 문제 해결

| 문제 | 원인 | 해결 |
|------|------|------|
| 앱이 설치되지 않음 | 서명 없음 | 개발자 모드 활성화 또는 패키지 서명 |
| Bridge API 작동 안함 | 일반 브라우저 | ZylBridge 폴백 확인 |
| CSS 깨짐 | safe area 미적용 | `--safe-top`, `--safe-bottom` 사용 |
| 터치 응답 없음 | 터치 타겟 너무 작음 | 최소 44x44px |

---

## 11. API 레퍼런스

### navigator.system

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `app.id` | string | 현재 앱 ID |
| `app.name` | string | 현재 앱 이름 |
| `app.version` | string | 현재 앱 버전 |
| `app.close()` | void | 앱 종료 |
| `app.minimize()` | void | 앱 최소화 |
| `launch(appId)` | void | 앱 실행 |
| `notification.create(title, body, opts)` | void | 알림 생성 |
| `battery.getLevel()` | Promise\<number\> | 배터리 % |
| `battery.isCharging()` | Promise\<boolean\> | 충전 여부 |
| `wifi.scan()` | Promise\<Array\> | WiFi 목록 |
| `network.getStatus()` | Promise\<Object\> | 네트워크 상태 |
| `storage.set(key, value)` | void | 값 저장 |
| `storage.get(key)` | Promise\<string\> | 값 조회 |
| `storage.remove(key)` | void | 값 삭제 |
| `info.getDeviceName()` | string | 기기 이름 |
| `info.getOsVersion()` | string | OS 버전 |
| `info.getLocale()` | string | 현재 로케일 |

### zylI18n (공유 모듈)

| 메서드 | 반환 | 설명 |
|--------|------|------|
| `t(key, params)` | string | 번역 |
| `formatDate(date)` | string | 날짜 포맷 |
| `setLocale(locale)` | void | 로케일 변경 |
| `getLocale()` | string | 현재 로케일 |
| `getSupportedLocales()` | string[] | 지원 로케일 |
| `onLocaleChange(callback)` | void | 변경 리스너 |
| `addTranslations(locale, keys)` | void | 앱별 번역 키 등록 |
| `applyTranslations()` | void | DOM data-i18n 재적용 |

### 시스템 서비스 (24개)

앱에서 `postMessage` IPC로 접근 가능한 서비스 목록:

| # | 서비스 | 주요 메서드 |
|---|--------|-----------|
| 1 | fs | getDirectory, getFileContent, writeFile, mkdir, remove |
| 2 | device | getInfo |
| 3 | storage | getUsage |
| 4 | apps | getInstalled, install, uninstall, getAppInfo |
| 5 | settings | get, set, getAll |
| 6 | terminal | exec |
| 7 | wifi | scan, getConnected |
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

---

## 라이선스

Zyl OS SDK와 이 가이드는 MIT 라이선스 하에 배포됩니다.
