<!--
  Zyl OS: Contributing Guide
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 기여 가이드

Zyl OS에 기여해 주셔서 감사합니다!

## 개발 환경 세팅

```bash
git clone https://github.com/zylos/zyl-os.git
cd zyl-os
./tools/setup-toolchain.sh
meson setup builddir
ninja -C builddir
```

## 코드 스타일

### C 코드
- C11 표준
- 들여쓰기: 4칸 스페이스
- 함수명: `zyl_모듈_동작()` (예: `zyl_power_set_brightness`)
- 구조체: `ZylPascalCase` (예: `ZylPowerState`)
- 상수: `ZYL_UPPER_CASE` (예: `ZYL_POWER_STATE_ACTIVE`)
- 모든 `malloc`/`calloc`/`strdup` → NULL 체크 필수
- `system()` 호출 금지 → D-Bus 또는 sysfs 사용
- `snprintf`에 `sizeof(buffer)` 사용 (매직 넘버 금지)

### HTML
- **`<!DOCTYPE html>`이 반드시 파일 첫 번째 줄이어야 합니다**
- 주석은 DOCTYPE 뒤에 배치: `<!DOCTYPE html>\n<!-- 주석 -->`
- Tauri WKWebView가 첫 바이트로 MIME 타입을 판단하므로, `<!--`로 시작하면 `application/octet-stream`으로 인식되어 렌더링 실패합니다
- CI에서 `tests/test_doctype.sh`로 자동 검증됩니다

### JavaScript
- ES5 호환 (WebKitGTK RISC-V 지원)
- `var` 사용 (`let`/`const` 사용 금지)
- 세미콜론 필수
- `===` 사용 (`==` 금지)
- `postMessage` 수신 시 `e.source` 검증 필수

### 파일 헤더
모든 소스 파일에 아래 형식의 헤더를 추가해야 합니다:

```
// ──────────────────────────────────────────────────────────
// [Clean Architecture] {Layer} - {Type}
//
// 역할: {이 파일의 역할}
// 수행범위: {구체적 수행 범위}
// 의존방향: {의존하는 모듈/파일들}
// SOLID: {적용된 SOLID 원칙} — {원칙 적용 설명}
// ──────────────────────────────────────────────────────────
```

## 브랜치 전략

- `main`: 안정 브랜치 (CI 통과 필수)
- `dev`: 개발 브랜치
- `feature/*`: 기능 브랜치
- `fix/*`: 버그 수정 브랜치

## 커밋 메시지 규칙

```
{영역}: {변경 내용 요약}

{상세 설명}

Co-Authored-By: {이름} <{이메일}>
```

영역 예시: `compositor`, `wam`, `apps/home`, `services/power`, `docs`

## PR 제출 전 체크리스트

- [ ] `ninja -C builddir` 빌드 성공
- [ ] `ninja -C builddir test` 테스트 통과
- [ ] 새 파일에 Clean Architecture 헤더 추가
- [ ] mock 데이터 사용하지 않음 (서비스 채널로 데이터 전달)
- [ ] `malloc`/`strdup` NULL 체크
- [ ] `system()` 미사용
- [ ] D-Bus 이름 `org.zylos.*` 규칙 준수
- [ ] 관련 문서 업데이트 (해당 시)

## 디렉토리 구조

```
compositor/        Wayland 컴포지터
runtime/wam/       Web Application Manager
runtime/hal/       Hardware Abstraction Layer
runtime/services/  시스템 서비스 (14개)
apps/              시스템 앱 (HTML/CSS/JS)
system/            systemd, plymouth, DTS, AppArmor
tools/             빌드/개발 도구
tests/             테스트
docs/              문서
```

## 라이선스

기여하신 코드는 MIT 라이선스 하에 배포됩니다.
