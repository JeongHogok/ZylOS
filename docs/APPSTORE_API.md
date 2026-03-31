# Zyl OS App Store — Server API Specification

**Base URL**: `https://store.zylos.dev/v1`
**인증**: Bearer token (개발자 계정)
**포맷**: JSON

---

## Public Endpoints (인증 불필요)

### GET /apps
앱 목록 조회 (페이지네이션).

| 파라미터 | 타입 | 설명 |
|----------|------|------|
| page | int | 페이지 번호 (기본 1) |
| per_page | int | 페이지당 항목 (기본 20, 최대 100) |
| category | string | 카테고리 필터 (optional) |
| q | string | 검색어 (optional) |
| sort | string | `popular`, `recent`, `rating` (기본 popular) |

**응답**: `200 OK`
```json
{
  "apps": [
    {
      "id": "com.example.myapp",
      "name": "My App",
      "version": "1.2.3",
      "description": "App description",
      "author": "Developer Name",
      "icon_url": "https://store.zylos.dev/icons/com.example.myapp.svg",
      "category": "productivity",
      "rating": 4.5,
      "downloads": 1234,
      "size_bytes": 102400,
      "updated_at": "2026-03-30T12:00:00Z",
      "min_os_version": "0.1.0"
    }
  ],
  "total": 150,
  "page": 1,
  "per_page": 20
}
```

### GET /apps/{id}
앱 상세 정보.

### GET /apps/{id}/download
.ospkg 파일 다운로드.

**응답**: `200 OK` + `Content-Type: application/octet-stream`
또는 `302 Found` (CDN 리다이렉트)

### GET /apps/{id}/reviews
리뷰 목록.

---

## Developer Endpoints (Bearer 토큰 필요)

### POST /apps
새 앱 등록.

**요청**: `multipart/form-data`
- `package`: .ospkg 파일
- `category`: 카테고리

**서버 처리**:
1. .ospkg ZIP 검증
2. app.json 파싱 + 필수 필드 확인
3. SIGNATURE + CERT 검증 (RSA-2048+SHA-256)
4. 인증서 → 신뢰 저장소 대조
5. 중복 ID 확인
6. 스토리지 업로드
7. 카탈로그 등록

### PUT /apps/{id}
앱 업데이트 (새 버전 업로드).

### DELETE /apps/{id}
앱 삭제 (소유자만).

### POST /developers/register
개발자 등록 + 인증서 제출.

### POST /developers/crl
인증서 폐기 요청.

---

## 클라이언트 연동 (apps/store/js/store.js)

```javascript
// ES5 — 앱스토어 서버 API 호출
function fetchApps(page, query) {
  var url = 'https://store.zylos.dev/v1/apps?page=' + page;
  if (query) url += '&q=' + encodeURIComponent(query);
  ZylBridge.requestService('network', 'fetch', { url: url });
}
```

## 자동 업데이트 흐름

```
1. 부팅 시 + 24시간 주기
2. GET /v1/apps/updates?installed=com.a:1.0,com.b:2.0
3. 응답: 업데이트 가능한 앱 목록
4. 사용자 확인 → 다운로드 → 서명 검증 → 설치
```
