# ZylOS 상용 OS 로드맵

**목표**: BPI-F3 (SpacemiT K1 RISC-V) 상에서 일상 사용 가능한 상용 모바일 OS
**기준**: Android/iOS/Tizen 대비 최소 기능 동등성 (Minimum Viable Commercial OS)
**현재**: v0.1.0 Developer Preview — 3차 감사 완료, 기술부채 제로

---

## Phase 구조

| Phase | 목표 | 의존 | 예상 규모 |
|-------|------|------|-----------|
| **A** | GPU 파이프라인 + 프레임워크 | 없음 (최우선) | ~5,000 LoC |
| **B** | 보안 부팅 체인 | A (GPU 안정화 후) | ~3,000 LoC |
| **C** | 미디어 파이프라인 | A (GPU 가속 필요) | ~4,000 LoC |
| **D** | 전력 관리 | A (거버너 연동) | ~2,500 LoC |
| **E** | 프로세스 격리 강화 | B (보안 기반) | ~2,000 LoC |
| **F** | 앱 에코시스템 | E (격리 완성 후) | ~6,000 LoC |
| **G** | 네트워킹 스택 | 없음 | ~3,000 LoC |
| **H** | 입력/접근성/생체 | A (UI 프레임워크) | ~3,000 LoC |
| **I** | 계정/동기화/백업 | F (스토어), G (네트워크) | ~4,000 LoC |
| **J** | 알림/멀티태스킹 | A (컴포지터) | ~2,500 LoC |
| **K** | 위치/NFC/기타 HAL | 없음 | ~2,000 LoC |
| **L** | 개발자 도구/SDK | F (앱 포맷 확정) | ~5,000 LoC |
| **M** | 테스트/인증 | 전체 | ~3,000 LoC |
| **N** | 국제화/OTA 고도화 | 없음 | ~1,500 LoC |

**총 예상**: ~46,500 LoC 신규 → 코드베이스 ~93,000 LoC로 성장

---

## Phase A: GPU 파이프라인 + 그래픽 프레임워크 [CRITICAL]

### A1. IMG BXE GPU 드라이버 통합

**현재**: CPU 소프트웨어 렌더링. wlroots가 llvmpipe 폴백.
**목표**: IMG BXE-2-32 (PowerVR) GPU 가속 → DRM/KMS → wlroots GPU 렌더.

```
┌─────────────────────────────────────────┐
│ WebKitGTK (GL 가속 렌더링)               │
├─────────────────────────────────────────┤
│ wlroots 0.18 (DRM/KMS 백엔드)           │
├─────────────────────────────────────────┤
│ Mesa (pvr Gallium3D 드라이버)            │
├─────────────────────────────────────────┤
│ Linux DRM/KMS (spacemit-drm)            │
├─────────────────────────────────────────┤
│ IMG BXE firmware (blob)                  │
└─────────────────────────────────────────┘
```

**작업**:
1. SpacemiT BSP에서 DRM/KMS 드라이버 활성화 (`CONFIG_DRM_SPACEMIT`, `CONFIG_DRM_PVR`)
2. Mesa pvr Gallium3D 드라이버 빌드 (또는 IMG 독점 드라이버 바이너리 통합)
3. wlroots 빌드 시 DRM 백엔드 활성화 (`-Dbackends=drm,libinput`)
4. 컴포지터 main.c에서 GPU 출력 우선 선택
5. DTS(`bpi-f3-zyl.dts`)에 GPU 노드 활성화

**파일**:
- `system/dts/bpi-f3-zyl.dts` — GPU 노드 추가
- `compositor/meson.build` — DRM 백엔드 의존성
- `tools/setup-toolchain.sh` — Mesa 빌드 옵션
- `docs/GPU_INTEGRATION.md` — 드라이버 설정 가이드

**검증**: `wlr_renderer_autocreate` → GLES2/Vulkan 백엔드 선택 확인, `glxinfo` 또는 `eglinfo` GPU 이름 확인.

### A2. WebKitGTK GL 가속

**현재**: `WEBKIT_HARDWARE_ACCELERATION_POLICY_ALWAYS` 설정했으나 GPU 없이 효과 없음.
**목표**: GPU 사용 가능 시 WebKitGTK가 GL 컴포지팅 + 텍스처 업로드.

**작업**:
1. A1 완료 후 WebKitGTK가 자동으로 GPU 감지
2. `webkit_settings_set_hardware_acceleration_policy(ALWAYS)` 유지
3. RISC-V용 WebKitGTK JIT 비활성화 확인 (`WEBKIT_DISABLE_JIT=1` 환경변수)
4. CSS `will-change`, `transform: translateZ(0)` 최적화 가이드

**파일**:
- `runtime/wam/src/wam.c` — WebKit 설정 검증
- `system/zyl-wam.service` — 환경변수 `WEBKIT_DISABLE_JIT=1`

### A3. 프레임 성능 모니터링

**현재**: FPS 측정 수단 없음.
**목표**: 컴포지터에 프레임 타이밍 추적 + 60fps 타겟.

**작업**:
1. `compositor/src/output/output.c`의 frame 콜백에 타이밍 측정 추가
2. 16.67ms 초과 시 로그 경고
3. D-Bus `org.zylos.Compositor.GetFrameStats` 메서드 추가

**파일**:
- `compositor/src/output/output.c` — 프레임 카운터
- `compositor/include/zyl_compositor.h` — 통계 구조체

---

## Phase B: 보안 부팅 체인 [CRITICAL]

### B1. U-Boot Verified Boot

**현재**: U-Boot → 커널 서명 검증 없음.
**목표**: U-Boot FIT 이미지 + RSA-2048 서명 검증.

```
U-Boot SPL (ROM 검증)
  → U-Boot (SPL이 서명 검증)
    → Linux Kernel + DTB (U-Boot이 FIT 서명 검증)
      → initramfs → rootfs (dm-verity)
```

**작업**:
1. U-Boot `.its` (Image Tree Source) 작성 — kernel + dtb + ramdisk 포함
2. RSA-2048 키 쌍 생성 (`mkimage -G key.pem`)
3. U-Boot defconfig에 `CONFIG_FIT_SIGNATURE=y`, `CONFIG_RSA=y` 활성화
4. 서명된 FIT 이미지 생성 스크립트

**파일**:
- `board/bpi-f3/fit-image.its` — FIT 이미지 소스
- `board/bpi-f3/keys/` — 서명 키 (공개키만 repo에 포함)
- `tools/sign-image.sh` — FIT 서명 스크립트
- `board/bpi-f3/u-boot-env.txt` — 부트 명령

### B2. dm-verity rootfs 무결성

**현재**: rootfs 무결성 검증 없음.
**목표**: dm-verity로 읽기 전용 시스템 파티션 무결성 보장.

**작업**:
1. 빌드 시 `veritysetup format /dev/rootfs`로 해시 트리 생성
2. 커널 커맨드라인에 `root=/dev/dm-0 dm-mod.create="..." ` verity 설정
3. 해시 트리를 별도 파티션에 저장
4. 변조 감지 시 recovery 모드로 부팅

**파일**:
- `tools/build-verity.sh` — verity 이미지 생성
- `board/bpi-f3/cmdline.txt` — 커널 커맨드라인

### B3. TEE (Trusted Execution Environment) — 향후

**현재**: 없음.
**RISC-V 제약**: SpacemiT K1에 TrustZone 동등 기능이 있는지 확인 필요.
PMP(Physical Memory Protection)로 최소 격리 가능.

**작업**:
1. SpacemiT K1 PMP 레지스터 설정 연구
2. OpenSBI에서 보안 영역 설정
3. 크리덴셜 암호화 키를 보안 영역에 저장

---

## Phase C: 미디어 파이프라인 [HIGH]

### C1. 카메라 ISP + JPEG 인코딩

**현재**: V4L2 raw YUYV 캡처만. 앱이 직접 변환해야 함.
**목표**: 카메라 서비스가 JPEG 출력 + 포커스/노출 제어.

**작업**:
1. libjpeg-turbo 통합 — YUYV → JPEG 변환
2. V4L2 컨트롤: `V4L2_CID_FOCUS_AUTO`, `V4L2_CID_EXPOSURE_AUTO`
3. 카메라 서비스에 `captureJpeg()`, `setFocus()`, `setExposure()` 추가
4. 썸네일 생성 (프리뷰용 320×240)

**파일**:
- `runtime/services/camera/camera.c` — JPEG 인코딩 + 컨트롤
- `runtime/services/camera/meson.build` — libjpeg-turbo 의존성

### C2. 동영상 녹화

**현재**: 없음.
**목표**: V4L2 → H.264 인코딩 → MP4 컨테이너.

**작업**:
1. GStreamer 또는 FFmpeg CLI 통합 (HW 인코더가 없으면 소프트웨어)
2. 카메라 서비스에 `startRecording()`, `stopRecording()` 추가
3. D-Bus 시그널로 녹화 상태 전파

### C3. 오디오 녹음 (마이크)

**현재**: PipeWire 재생만. 녹음 없음.
**목표**: PipeWire 캡처 + WAV/Opus 저장.

**작업**:
1. `pw-record` 또는 PipeWire API로 캡처 스트림 오픈
2. 오디오 서비스에 `startRecording()`, `stopRecording()` 추가
3. 통화 앱과 연동 (마이크 입력 → telephony 서비스)

### C4. 미디어 재생 (오디오/비디오)

**현재**: Web Audio API 비프음만.
**목표**: GStreamer/PipeWire 백엔드로 MP3/FLAC/MP4 재생.

**작업**:
1. 음악 앱에서 `audio.play(path)` → PipeWire 재생
2. 갤러리 앱에서 동영상 재생 (GStreamer → Wayland subsurface)
3. 미디어 세션 관리 (play/pause/next 하드키)

---

## Phase D: 전력 관리 [HIGH]

### D1. CPU 거버너 + DVFS

**현재**: 기본 커널 거버너(performance?). 제어 없음.
**목표**: 사용 패턴에 따른 동적 주파수/전압 조절.

**작업**:
1. `cpufreq` sysfs 인터페이스 래퍼 — HAL에 추가
2. 기본 거버너: `schedutil` (커널 스케줄러 연동)
3. 파워 프로필: 성능/균형/절전 — 설정 앱에서 선택
4. thermal throttling 감지 + 알림

**파일**:
- `runtime/hal/hal_cpu.c` — cpufreq 제어
- `runtime/services/power/power.c` — 프로필 관리 확장
- `apps/settings/js/settings.js` — 전력 모드 UI

### D2. 앱별 배터리 제한

**현재**: 없음.
**목표**: 백그라운드 앱 CPU/네트워크 제한.

**작업**:
1. 앱 suspend 시 cgroup freeze (`cgroup.freeze = 1`)
2. 배터리 사용량 추적 — cgroup `cpu.stat` 누적
3. 설정 → 배터리 → 앱별 사용량 표시

### D3. Doze/Standby 모드

**현재**: 서스펜드(suspend-to-RAM)만.
**목표**: 화면 꺼짐 후 단계적 전력 절감.

```
Screen Off → Idle (5분) → Doze (30분) → Deep Sleep
  ↑ 웨이크락 보유 앱은 Idle 유지
```

**작업**:
1. 파워 서비스에 상태 머신 추가 (ACTIVE → IDLE → DOZE → DEEP_SLEEP)
2. DOZE: 네트워크 차단, 알람만 허용
3. DEEP_SLEEP: suspend-to-RAM + 주기적 웨이크업 (알람 체크)

---

## Phase E: 프로세스 격리 강화 [HIGH]

### E1. 앱별 UID

**현재**: 모든 앱이 `zyl-app` 단일 UID.
**목표**: 앱 설치 시 고유 UID 할당.

**작업**:
1. 앱 설치 시 `/etc/passwd`에 `zyl-app-{id}` 사용자 생성 (UID 10000+)
2. 앱 데이터 디렉토리 소유권 = 해당 UID
3. WAM launch 시 `setuid` 전환
4. 앱 간 파일 접근 불가 (DAC 강제)

### E2. SELinux / Smack 통합

**현재**: AppArmor만.
**목표**: MAC(Mandatory Access Control) 강화.

**작업**:
1. RISC-V 커널에서 Smack 활성화 (SELinux보다 경량)
2. 앱별 Smack 라벨 할당
3. 서비스 간 통신 규칙 정의

### E3. Zygote 패턴

**현재**: 앱마다 새 WebKitGTK 프로세스.
**목표**: 사전 fork된 프로세스 풀에서 즉시 launch.

**작업**:
1. WAM에 zygote 프로세스 추가 — WebKitGTK 초기화 후 대기
2. 앱 launch 요청 → zygote fork → `setuid` + sandbox → 앱 로드
3. 콜드 스타트: ~2초 → ~200ms 목표

---

## Phase F: 앱 에코시스템 [HIGH]

### F1. 앱스토어 서버

**현재**: 로컬 설치/제거만.
**목표**: HTTPS 앱 카탈로그 서버 + 검색 + 카테고리.

**작업**:
1. 서버 API 설계 (REST)
   - `GET /v1/apps` — 앱 목록 (페이지네이션, 카테고리 필터)
   - `GET /v1/apps/{id}` — 앱 상세
   - `GET /v1/apps/{id}/download` — .ospkg 다운로드
   - `POST /v1/apps/{id}/review` — 리뷰 등록
2. 클라이언트: `apps/store/js/store.js`에 서버 연동
3. 자동 업데이트 체크 (`updater` 서비스 확장)

### F2. 앱 서명 인프라

**현재**: RSA-2048 검증 구현됨. 서명 도구 없음.
**목표**: 개발자가 앱에 서명할 수 있는 도구 체인.

**작업**:
1. `tools/sign-ospkg.sh` — .ospkg 서명 스크립트
2. 개발자 인증서 발급 CLI
3. 인증서 폐기 목록 (CRL) 서버

### F3. 앱 결제

**현재**: 없음.
**목표**: 유료 앱 구매 + 인앱 결제.

**작업**: 외부 결제 게이트웨이 (Stripe/PayPal) 통합. OS 수준에서는 라이선스 토큰 검증.

### F4. 개발자 SDK

**현재**: APP_DEVELOPMENT_GUIDE.md만.
**목표**: 앱 템플릿 + API 레퍼런스 + 에뮬레이터 통합.

**작업**:
1. `zyl create-app <name>` CLI 도구
2. app.json 스키마 JSON Schema 정의
3. API 레퍼런스 자동 생성 (JSDoc → HTML)
4. VS Code 확장 (구문 강조, 앱 실행)

---

## Phase G: 네트워킹 스택 [MEDIUM]

### G1. VPN 클라이언트

**작업**: WireGuard 커널 모듈 + NetworkManager VPN 플러그인. 설정 앱 UI.

### G2. WiFi 핫스팟

**작업**: `hostapd` + `dnsmasq` 통합. AP 모드 전환.

### G3. 캡티브 포털 감지

**작업**: 연결 후 `http://connectivitycheck.zylos.dev/generate_204` 프로브. 302 → 브라우저 열기.

### G4. IPv6

**작업**: 커널 `CONFIG_IPV6=y` + NetworkManager IPv6 자동 설정.

### G5. mDNS

**작업**: Avahi 데몬 통합. `.local` 도메인 해석.

---

## Phase H: 입력/접근성/생체 [MEDIUM]

### H1. 예측 변환 + 자동 완성

**작업**: hunspell 사전 + n-gram 기반 예측. 키보드 앱에 후보 표시 바.

### H2. 음성 입력

**작업**: Whisper.cpp (로컬 STT) 또는 서버 사이드. 마이크 HAL 연동.

### H3. 스크린리더

**작업**: AT-SPI 완성 + Orca 경량 포크 또는 자체 TTS 엔진.

### H4. 생체 인증

**작업**: 지문 센서 HAL (libfprint) + PAM 모듈. PIN 대체/보조.

---

## Phase I: 계정/동기화/백업 [MEDIUM]

### I1. 계정 서비스

**작업**: OAuth 2.0 클라이언트 + 계정 관리 D-Bus 서비스. 다중 계정 지원.

### I2. 클라우드 백업

**작업**: 설정/연락처/메시지 → 암호화 → 클라우드 스토리지 (S3 호환).

### I3. 크로스 디바이스 동기화

**작업**: 연락처/캘린더 → CalDAV/CardDAV 서버 동기화.

---

## Phase J: 알림/멀티태스킹 [MEDIUM]

### J1. 알림 액션 + 인라인 리플라이

**작업**: 알림에 버튼 액션 (수락/거부/리플라이) + 앱 콜백.

### J2. 방해 금지(DND) 모드

**작업**: 시간/연락처/앱 기반 필터. 우선순위 알림만 통과.

### J3. 분할 화면

**작업**: 컴포지터에 split view 모드. 두 앱 동시 표시.

### J4. PiP (Picture in Picture)

**작업**: 컴포지터에 floating window 레이어. 동영상/통화 앱 지원.

---

## Phase K: 위치/NFC/기타 HAL [LOW~MEDIUM]

### K1. WiFi/Cell 삼각측량

**작업**: wpa_supplicant BSS 목록 + 셀 타워 → 위치 추정 DB.

### K2. 지오펜싱

**작업**: 위치 서비스에 등록/해제 + 진입/이탈 이벤트.

### K3. NFC

**작업**: libnfc HAL + D-Bus 서비스 (하드웨어 의존).

---

## Phase L: 개발자 도구/SDK [MEDIUM]

### L1. 원격 디버그

**작업**: WebKitGTK 원격 인스펙터 활성화 + ADB 유사 도구.

### L2. 프로파일러

**작업**: 앱별 CPU/메모리/네트워크 사용량 실시간 모니터링 UI.

### L3. 레이아웃 인스펙터

**작업**: 앱 DOM 트리 원격 탐색 (WebKitGTK inspector).

### L4. 앱 템플릿 생성기

**작업**: `zyl init` → 보일러플레이트 app.json + index.html + i18n 생성.

---

## Phase M: 테스트/인증 [HIGH]

### M1. CTS 급 적합성 테스트

**작업**: 각 서비스 D-Bus API 자동 테스트 (500+ 케이스).

### M2. 성능 벤치마크

**작업**: 앱 launch 시간, 프레임 레이트, 메모리 사용량 측정 + 회귀 방지.

### M3. 보안 퍼징 확장

**작업**: 모든 D-Bus 서비스 + IPC 핸들러 + 파일 파서 퍼징.

### M4. 하드웨어 호환성 테스트

**작업**: 실제 BPI-F3 보드에서 전 서비스 동작 검증.

---

## 우선순위 타임라인

```
v0.2.0 (Phase A+B+D):  GPU + 보안 부팅 + 전력 관리
  → "실제 디바이스에서 60fps UI + 보안 부팅"

v0.3.0 (Phase C+E+J):  미디어 + 격리 + 멀티태스킹
  → "카메라/음악/동영상 + 앱 격리 완성"

v0.4.0 (Phase F+L):    앱 에코시스템 + SDK
  → "서드파티 앱 개발/배포 가능"

v0.5.0 (Phase G+H+K):  네트워크/입력/HAL
  → "VPN, 예측변환, 생체인증"

v1.0.0 (Phase I+M+N):  계정/테스트/국제화
  → "상용 출시"
```

## 의존성 그래프

```
A (GPU) ──→ B (보안부팅)
  │          │
  ├──→ C (미디어)
  │          │
  ├──→ D (전력)
  │
  └──→ J (멀티태스킹)
            │
B ──→ E (격리) ──→ F (에코시스템) ──→ I (계정)
                       │
                       └──→ L (SDK)
G (네트워크) ─────────────────────→ I (계정)
H (입력/접근성) ── 독립
K (HAL) ── 독립
M (테스트) ── 전체 의존
N (국제화) ── 독립
```
