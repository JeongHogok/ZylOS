# ZylOS v0.1.0 — 4차 감사: Android 아키텍처 기준 비교

**일자**: 2026-03-31
**기준**: Android 14 핵심 아키텍처 패턴 vs ZylOS 구현 갭

---

## Android가 안정적인 구조적 이유 → ZylOS 갭

### 1. Intent 시스템 부재 [HIGH]
- **Android**: Intent로 앱 간 통신 (암시적/명시적), ACTION_VIEW/SEND/PICK 등
- **ZylOS**: `launchApp(appId)` 직접 호출만. 데이터 전달 없음. "사진 공유" 같은 앱 간 워크플로우 불가
- **패치**: Intent-like 메시지 시스템 — `{ action, data, mimeType, targetApp }`

### 2. 런타임 퍼미션 다이얼로그 없음 [HIGH]
- **Android**: 앱이 위험 권한 요청 → 시스템 다이얼로그 → 사용자 승인/거부
- **ZylOS**: app.json에 선언 → 설치 시 전부 승인. 런타임 거부 불가 (설정에서 사후 해제만)
- **패치**: 런타임 권한 요청 프로토콜 + OS 수준 다이얼로그 UI

### 3. Content Provider 패턴 없음 [HIGH]
- **Android**: ContentProvider로 앱 간 구조화된 데이터 공유 (연락처, 미디어 등)
- **ZylOS**: 모든 앱이 직접 fs 서비스로 Documents/ 접근. 앱 간 데이터 격리 불완전
- **패치**: 미디어/연락처 등 공유 데이터에 대한 접근 게이트웨이

### 4. IPC 요청-응답 매칭 없음 [HIGH]
- **Android**: Binder — 동기/비동기 RPC, 타입 안전, 트랜잭션 ID
- **ZylOS**: postMessage fire-and-forget. requestId 없음 → 앱이 여러 서비스를 동시 호출하면 응답 구분 불가
- **패치**: bridge.js에 requestId 자동 생성 + Promise 기반 응답 매칭

### 5. 백그라운드 실행 제한 없음 [MEDIUM]
- **Android**: Background execution limit, foreground service 필수, doze/app standby
- **ZylOS**: iframe은 숨기면 JS 실행 계속. 제한 메커니즘 없음
- **패치**: 앱 suspend 시 iframe unload 또는 JS 실행 중지

### 6. 웨이크락 타임아웃 없음 [MEDIUM]
- **Android**: 웨이크락 최대 시간 제한 + 배터리 사용량 추적
- **ZylOS**: acquire/release만. 앱이 release 안 하면 영구 점유
- **패치**: 웨이크락 최대 10분 타임아웃 + 강제 해제

### 7. LMK(Low Memory Killer) 없음 [MEDIUM]
- **Android**: oom_adj 기반 앱 우선순위 → 메모리 부족 시 백그라운드 앱 종료
- **ZylOS**: `check_memory_pressure`가 경고만 출력. 실제 종료 안 함
- **패치**: 앱별 메모리 우선순위 + 임계값 초과 시 LRU 백그라운드 앱 종료

### 8. 클립보드 격리 없음 [MEDIUM]
- **Android**: 클립보드 접근 시 권한 확인 + 백그라운드 앱 접근 차단
- **ZylOS**: 클립보드 서비스 자체가 없음. 앱 간 복사/붙여넣기 불가
- **패치**: 클립보드 서비스 + 앱별 접근 제어

### 9. 앱 데이터 삭제 누락 [MEDIUM]
- **Android**: 앱 제거 시 /data/data/{pkg}/ 자동 삭제
- **ZylOS**: `appstore_uninstall`이 설치 디렉토리만 삭제. Documents/ 내 앱 데이터 잔존
- **패치**: uninstall 시 앱별 데이터 디렉토리 정리

### 10. 알람/스케줄러 서비스 없음 [MEDIUM]
- **Android**: AlarmManager + JobScheduler + WorkManager
- **ZylOS**: 시계 앱의 알람만. 시스템 수준 스케줄러 없음. 앱이 주기적 작업을 스케줄할 수 없음
- **패치**: 알람 서비스 (일회성/반복 타이머 + 앱 콜백)

---

## 총계: 10건

| 심각도 | 건수 |
|--------|------|
| HIGH | 4 (Intent, 런타임 권한, Content Provider, IPC 매칭) |
| MEDIUM | 6 |
