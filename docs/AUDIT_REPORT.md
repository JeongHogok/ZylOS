# Zyl OS 프로덕션 준비 상태 감사 보고서

**감사일**: 2026-03-28
**코드베이스**: 8,894 LOC (C 3,988 + JS 4,906)
**전체 평가**: 3.7/10 — 아키텍처 우수, OS 인프라 미구현

---

## 현재 완성된 것 (15~20%)

| 영역 | 점수 | 상태 |
|------|------|------|
| 클린 아키텍처 | 9/10 | 레이어 분리, SOLID, 모듈화 우수 |
| Wayland 컴포지터 | 6/10 | 기본 렌더링/터치 동작, 제스처 액션 미구현 |
| 웹 앱 런타임 (WAM) | 6/10 | WebKitGTK 통합 동작, 라이프사이클 미완성 |
| 시스템 앱 UI | 7/10 | 홈/잠금/설정/브라우저/터미널/카메라/파일 |
| 에뮬레이터 | 8/10 | 3종 네비게이션, 알림, QS, HAL 연동 |
| 문서 | 5/10 | 앱 개발 가이드 있음, 배포 가이드 없음 |

---

## CRITICAL — 배포 불가 (13개, ~55-70일)

### ~~C1. 빌드 시스템 통합~~ ✅ 완료 (2026-03-28)
- ~~최상위 meson.build 없음~~ → 루트 meson.build 생성, compositor/WAM을 subdir로 통합
- Yocto/Buildroot 레시피 아직 없음 (Phase 2에서 구현 예정)
- zyl-compositor, zyl-wam으로 바이너리 이름 통일

### ~~C2. Init 시스템 / systemd 서비스~~ ✅ 완료 (2026-03-28)
- ~~systemd unit 파일 0개~~ → 4개 생성: zyl-compositor.service, zyl-wam.service, zyl-notification.service, zyl-os.target
- 서비스 의존관계: compositor → WAM → notification

### ~~C3. 부팅 스플래시~~ ✅ 완료 (2026-03-28)
- ~~검은 화면~~ → Plymouth 테마 구현 (system/plymouth/zyl-os/)
  - 다크 배경 + "Zyl OS" 텍스트 + 펄스 애니메이션 + 프로그레스 바
  - install.sh 스크립트 제공

### ~~C4. 컴포지터 제스처 액션~~ ✅ 완료 (2026-03-28)
- ~~로그만 출력~~ → 4개 액션 모두 구현:
  - GoHome: home_screen_visible 플래그 + D-Bus 시그널
  - NotificationPanel: wlr_scene_rect 오버레이 토글 + D-Bus 시그널
  - GoBack: 포커스된 앱에 D-Bus 시그널 전달
  - AppSwitcher: wl_list 순회로 view_focus() 호출

### ~~C5. 앱 라이프사이클~~ ✅ 완료 (2026-03-28)
- ~~suspend/resume no-op~~ → 실제 구현:
  - Suspend: `zyl:pause` JS 이벤트 디스패치 + GTK 윈도우 숨김
  - Resume: `zyl:resume` 이벤트 + 윈도우 표시
  - Close: `zyl:destroy` 이벤트 → 100ms 대기 → 정리
  - 메모리 경고: 5개 초과 시 로그
  - D-Bus Resume 메서드 + ListRunning에 인스턴스 카운트 추가

### ~~C6. AppStore 서비스~~ ✅ 완료 (2026-03-28)
- ~~TODO 주석만~~ → 실제 구현:
  - 9단계 패키지 검증 파이프라인 (unzip→CERT→만료→폐기→SHA256→서명→파싱)
  - 실제 설치 (unzip → 디렉토리 추출 → 매니페스트 검증)
  - rm_rf 기반 앱 제거 (시스템 앱 보호)
  - 디렉토리 스캔으로 설치된 앱 목록 조회

### C7. 알림 서비스 스텁
- C 서비스 헤더/구조만 존재, 실제 알림 전달 미구현
- **필요**: 알림 큐, D-Bus 시그널 전달, 액션 라우팅

### ~~C8. OTA 업데이터~~ ✅ 완료 (2026-03-28)
- ~~주석만 존재~~ → 실제 구현:
  - curl 기반 HTTP 업데이트 확인 + 다운로드
  - SHA-256 해시 검증 (sha256sum/shasum)
  - 4종 업데이트 적용: FULL(dd), DELTA(bspatch), APPS_ONLY(unzip), KERNEL(cp)
  - fw_setenv으로 U-Boot A/B 슬롯 전환 (파일 폴백)
  - 헬스체크 기반 mark_verified (systemctl is-active)
  - 롤백 지원

### ~~C9. 전력 관리~~ ✅ 완료 (2026-03-28)
- ~~화면 끄기/절전 미구현~~ → power.h/c 구현:
  - 6단계 전력 상태 (Active/Dim/ScreenOff/Doze/Suspend/Shutdown)
  - sysfs backlight 자동 감지 + 밝기 제어
  - cpufreq 거버너 설정 (schedutil 기본)
  - 타이머 기반 자동 절전 (dim → screen_off → suspend)
  - 웨이크락 시스템 (앱이 절전 방지 가능)
  - D-Bus 인터페이스 (StateChanged 시그널)
  - systemd 서비스 파일 (zyl-power.service)

### ~~C10. 셀룰러/전화 스택~~ ✅ 완료 (2026-03-28)
- ~~모뎀 미구현~~ → ModemManager D-Bus 통합:
  - SIM/신호/통신사 조회, 음성 통화, SMS 전송
  - ObjectManager 기반 모뎀 열거
  - 하드웨어 미감지 시 graceful fallback

### ~~C11. GPS/위치 서비스~~ ✅ 완료 (2026-03-28)
- ~~GNSS 미구현~~ → 듀얼 프로바이더 구현:
  - Primary: GPSD (libgps) 하드웨어 GPS
  - Fallback: HTTP GeoIP (ip-api.com via libcurl)
  - Fused provider: 두 소스 결합
  - 조건부 컴파일 (HAVE_GPSD, HAVE_CURL)

### ~~C12. 센서 통합~~ ✅ 완료 (2026-03-28)
- ~~IIO 미구현~~ → Linux IIO 서브시스템 통합:
  - /sys/bus/iio/devices/ 스캔으로 센서 자동 감지
  - 5종 센서: 가속도계, 자이로, 근접, 조도, 자기계
  - 폴링 스레드 기반 리스너 (설정 가능한 주파수)
  - raw → scale/offset 변환
  - D-Bus SensorEvent 시그널

### ~~C13. 앱 샌드박싱~~ ✅ 완료 (2026-03-28)
- ~~동일 권한 실행~~ → 5계층 보안 모델 구현:
  - L1: mount namespace + bind mount 파일 격리
  - L2: seccomp-bpf 시스콜 필터 (3단계 프로필)
  - L3: network namespace (네트워크 권한 없으면 차단)
  - L4: cgroup v2 리소스 제한 (메모리/CPU/PID)
  - L5: D-Bus 정책 XML 생성 (권한별 서비스 접근 제어)
  - 11개 권한 플래그 비트마스크
  - 매니페스트 기반 정책 자동 생성

---

## HIGH — 기능 부족 (20개, ~40-50일)

| # | 항목 | 설명 |
|---|------|------|
| ~~H1~~ | ~~CI/CD 파이프라인~~ | ✅ GitHub Actions: build-native + lint-js |
| H2 | 디스플레이 관리 | 해상도/밝기/회전/절전 미구현 |
| H3 | 입력 처리 | IME, 멀티터치, 하드웨어 키 매핑 없음 |
| H4 | D-Bus 견고성 | 비동기 처리, 타임아웃, 에러 핸들링 부족 |
| H5 | Bridge 디스패치 | 3개 메시지만 처리, 응답 라우팅 없음 |
| H6 | USB/MTP | PC-디바이스 파일 전송 불가 |
| H7 | 화면 회전 | 가속도계 연동 없음 |
| H8 | 멀티 유저 | 단일 사용자만 지원 |
| H9 | 브라우저 | 실제 URL 로딩 불가 (시뮬레이션만) |
| H10 | WebKit 보안 | file:// 접근 허용, CSP 없음 |
| H11 | TLS/인증서 | 인증서 검증/핀닝 없음 |
| H12 | 인증 정보 관리 | libsecret 미통합, 암호화 저장소 없음 |
| H13 | 에러 핸들링 | Bridge JSON 검증 없음 |
| H14 | 패키지 관리 | .ospkg 포맷 실제 검증 없음 |
| H15 | 디바이스 트리 | BPI-F3용 .dts/.dtb 없음 |
| H16 | 배포 가이드 | 플래싱/운영 문서 없음 |
| H17 | 서비스 요청 ID | postMessage 상관 ID 없음 (경합 가능) |
| H18 | 비동기 스토리지 | getUsage()가 동기인데 HAL은 비동기 |
| H19 | 파일 시스템 접근 | 가상 FS만 존재, 실제 FS 연동 없음 |
| H20 | GTK 윈도우 관리 | 풀스크린 강제/크기 제한 미흡 |

---

## MEDIUM — 완성도 부족 (11개, ~20-25일)

| # | 항목 |
|---|------|
| M1 | 커서/포인터 관리 (테마, DPI) |
| M2 | 렌더링 최적화 (프레임 페이싱, 데미지 트래킹) |
| M3 | 접근성 (스크린 리더, 고대비) |
| M4 | SELinux/AppArmor 정책 |
| M5 | 크래시 리포팅 + 구조화 로깅 |
| M6 | 복구 모드 (팩토리 리셋) |
| M7 | 단위 테스트 (0% → 50%+) |
| M8 | 통합 테스트 |
| M9 | 성능 프로파일링 |
| M10 | 아키텍처 문서 |
| M11 | 기여 가이드 (CONTRIBUTING.md) |

---

## LOW (3개, ~2-3일)

- 코드 포맷 통일 (.clang-format, .prettierrc)
- 첫 실행 설정 마법사 (OOBE)
- D-Bus 호출 레이트 리밋

---

## 권장 로드맵

### Phase 1: 코어 안정화 (2주)
- 최상위 빌드 시스템 + CI/CD
- systemd 서비스 파일
- WAM 라이프사이클 완성
- 컴포지터 제스처 액션 구현
- Bridge 메시지 ID 상관

### Phase 2: 시스템 통합 (3-4주)
- Linux HAL 구현 (WiFi/BT/센서/배터리)
- AppStore 서비스 (서명 검증 + 설치)
- 알림 데몬
- 전력 관리
- Yocto 이미지 빌드

### Phase 3: 기능 완성 (2-3주)
- OTA 업데이터 (A/B 파티션)
- USB/MTP
- 화면 회전 + 센서
- 앱 샌드박싱
- 크래시 리포팅

### Phase 4: 품질 강화 (2-3주)
- 테스트 (50%+ 커버리지)
- 성능 프로파일링
- 보안 감사
- 접근성
- 배포/운영 문서
- 첫 실행 마법사

---

## 예상 일정

| 인원 | Phase 1-4 전체 | 최소 배포 가능 (Phase 1+2) |
|------|--------------|------------------------|
| 1명 | 5-7개월 | 5-6주 |
| 2-3명 | 2-3개월 | 3-4주 |
| 5명+ | 6-8주 | 2-3주 |

---

*이 보고서는 코드베이스의 실제 파일을 읽고 분석한 결과입니다.*
