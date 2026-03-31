# Zyl OS API Reference

**Version**: 0.1.0
**대상**: 서드파티 앱 개발자
**런타임**: WebKitGTK (ES5 JavaScript)

---

## 1. ZylBridge — IPC 추상화

앱과 OS 서비스 간 통신 인터페이스.

### ZylBridge.requestService(service, method, params)
OS 서비스 호출. 응답은 `message` 이벤트로 수신.

```javascript
// 파일 목록 조회
ZylBridge.requestService('fs', 'getDirectory', { path: '/Documents' });

// 응답 수신
window.addEventListener('message', function (e) {
  var msg = JSON.parse(e.data);
  if (msg.type === 'service.response' && msg.service === 'fs') {
    // msg.data = [{ name: 'file.txt', is_dir: false, size: 1024 }, ...]
  }
});
```

### ZylBridge.launch(appId)
다른 앱 실행.
```javascript
ZylBridge.launch('com.zylos.browser');
```

### ZylBridge.notify(title, body, options)
알림 생성.
```javascript
ZylBridge.notify('알림 제목', '알림 내용', {
  channel: 'default',
  priority: 2,
  appId: 'com.zylos.myapp',
  actions: [{ label: '열기', actionId: 'open' }]
});
```

### ZylBridge.getIpcMode()
현재 IPC 모드 반환: `'native'` | `'webkit'` | `'iframe'` | `'standalone'`.

---

## 2. 서비스 API

### fs — 파일시스템
| 메서드 | 파라미터 | 반환 | 설명 |
|--------|----------|------|------|
| getDirectory | `{ path }` | `[FileEntry]` | 디렉토리 내용 |
| getFileContent | `{ path }` | `string` | 텍스트 파일 읽기 |
| writeFile | `{ path, content }` | `boolean` | 파일 쓰기 |
| mkdir | `{ path }` | `void` | 디렉토리 생성 |
| remove | `{ path }` | `void` | 파일/디렉토리 삭제 |
| rename | `{ oldPath, newPath }` | `void` | 이름 변경 |

### contacts — 연락처
| 메서드 | 파라미터 | 반환 | 권한 |
|--------|----------|------|------|
| getAll | — | `[Contact]` | `contacts` |
| getById | `{ id }` | `Contact` | `contacts` |
| create | `{ name, phone, email }` | `Contact` | `contacts` |
| update | `{ id, name, phone, email }` | `Contact` | `contacts` |
| delete | `{ id }` | `void` | `contacts` |
| search | `{ query }` | `[Contact]` | `contacts` |

### messaging — 메시지
| 메서드 | 파라미터 | 반환 | 권한 |
|--------|----------|------|------|
| getThreads | — | `[Thread]` | `messaging` |
| getMessages | `{ threadId }` | `[Message]` | `messaging` |
| send | `{ number, text, name }` | `Message` | `messaging` |

### notification — 알림
| 메서드 | 파라미터 | 권한 |
|--------|----------|------|
| post | `{ appId, title, body, icon, priority }` | `notification` |
| cancel | `{ id }` | `notification` |
| getActive | — | `notification` |
| clearAll | — | `notification` |
| setDndMode | `{ enabled }` | `notification` |

### audio — 오디오
| 메서드 | 파라미터 | 설명 |
|--------|----------|------|
| getVolume | `{ stream }` | 볼륨 조회 (0-100) |
| setVolume | `{ stream, value }` | 볼륨 설정 |
| getState | — | 전체 오디오 상태 |
| playNotificationSound | — | 알림음 재생 |
| vibrate | `{ pattern }` | 진동 패턴 |

Stream 종류: `media`, `notif`, `alarm`, `ringtone`, `system`

### settings — 설정
| 메서드 | 파라미터 | 권한 |
|--------|----------|------|
| get | `{ category }` | `system` |
| update | `{ category, key, value }` | `system` |

### location — 위치
| 메서드 | 파라미터 | 권한 |
|--------|----------|------|
| getLastKnown | — | `location` |
| requestUpdates | — | `location` |
| stopUpdates | — | `location` |

### power — 전원
| 메서드 | 설명 |
|--------|------|
| getState | 배터리/밝기/전원 상태 |
| setBrightness | `{ percent }` — 밝기 설정 |

### network — 네트워크
| 메서드 | 파라미터 | 권한 |
|--------|----------|------|
| fetch | `{ url }` | `network` |

**도메인 제한**: `ZylSandbox.ALLOWED_DOMAINS`에 등록된 도메인만 접근 가능.

---

## 3. 권한 (app.json permissions)

```json
{
  "permissions": ["storage", "contacts", "notification", "network"]
}
```

| 권한 | 접근 가능 서비스 |
|------|-----------------|
| `storage` | fs |
| `contacts` | contacts |
| `messaging` | messaging |
| `notification` | notification |
| `camera` | camera |
| `location` | location |
| `network` | network |
| `telephony` | telephony |
| `bluetooth` | bluetooth |
| `audio` | audio |
| `shell` | terminal |
| `credential` | credential |
| `system` | settings |

---

## 4. i18n — 다국어

```javascript
// 번역 키 등록 (앱 i18n.js에서)
zylI18n.addTranslations('ko', { 'myapp.hello': '안녕하세요' });
zylI18n.addTranslations('en', { 'myapp.hello': 'Hello' });

// 사용
var text = zylI18n.t('myapp.hello');

// 복수형
var msg = zylI18n.tp('myapp.items', count); // .one / .other 자동 선택

// 숫자 포맷
var num = zylI18n.formatNumber(1234.5); // "1,234.5"

// 날짜 포맷
var date = zylI18n.formatDate(new Date());
```

지원 로케일: `ko`, `en`, `ja`, `zh`, `es`

---

## 5. app.json 스키마

```json
{
  "id": "com.developer.appname",      // 필수. 역도메인 형식
  "name": "App Name",                  // 필수. 표시 이름
  "version": "1.0.0",                  // 필수. semver
  "entry": "index.html",               // 필수. 진입점
  "description": "App description",    // 선택
  "permissions": ["storage"],           // 선택. 빈 배열 = 권한 없음
  "iconSvg": "<svg>...</svg>",         // 선택. 인라인 SVG 아이콘
  "color": "icon-blue"                 // 선택. 아이콘 배경색
}
```
