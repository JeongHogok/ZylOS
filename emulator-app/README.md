# Zyl OS Emulator

Tauri 2.x 기반 네이티브 디바이스 에뮬레이터. IPC 라우터 + 컴포지터 역할을 담당하며, 실제 리소스 예약(디스크 이미지, 메모리 제한)을 지원합니다.

> **아키텍처 변경**: 25개 시스템 서비스의 비즈니스 로직은 OS 이미지(`apps/system/services.js`)로 이동했습니다. 에뮬레이터는 순수 IPC 라우터로서 앱↔서비스 간 메시지 전달만 담당하며, Rust 백엔드는 파일시스템 보호와 리소스 관리를 수행합니다.

## 빠른 시작

### 개발 모드
```bash
cd emulator-app
cargo tauri dev
```

### 릴리즈 빌드
```bash
cargo tauri build
# 빌드 결과 (경로는 플랫폼/아키텍처에 따라 다를 수 있음):
# macOS: target/release/bundle/macos/Zyl OS Emulator.app
#        target/release/bundle/dmg/Zyl OS Emulator_<version>_<arch>.dmg
# Linux: target/release/bundle/deb/zyl-os-emulator_<version>_<arch>.deb
#        target/release/bundle/appimage/zyl-os-emulator_<version>_<arch>.AppImage
```

## 기능

### 프리부팅 설정
- 디바이스 프로필 선택 (F3 Gesture / F3 Lite / F3 Classic)
- OS 이미지 선택 + 가져오기 (Import)
- 저장공간: 4/8/16/32 GB (sparse 디스크 이미지)
- RAM: 512MB/1/2/4 GB (cgroup v2 또는 setrlimit)

### 리소스 예약
- **디스크**: sparse 이미지 생성 → 마운트 (macOS: hdiutil, Linux: udisksctl)
- **메모리**: 프로세스 메모리 제한 (macOS: RLIMIT_AS, Linux: cgroup v2)
- **폴백**: 마운트 실패 시 디렉토리 기반 가상 FS로 자동 전환

### 호스트 연동
- WiFi: 호스트의 실제 네트워크 스캔 (macOS: airport, Linux: nmcli)
- Bluetooth: 호스트의 페어링된 디바이스 조회 (macOS: system_profiler, Linux: bluetoothctl)
- Battery: 호스트 배터리 상태 반영 (macOS: pmset, Linux: sysfs)
- Storage: 마운트된 디스크 이미지의 실제 사용량
- Location: IP 기반 위치 서비스 (ipinfo.io, Rust 백엔드)
- Camera: MediaRecorder 기반 비디오 녹화 (호스트 웹캠)

### IPC 라우터 (에뮬레이터) + OS 서비스 (25개)

에뮬레이터의 `services.js`는 순수 IPC 라우터로서 앱↔OS 서비스 간 postMessage를 전달합니다.
25개 서비스의 비즈니스 로직은 OS 이미지의 `apps/system/services.js`가 소유합니다.

**OS 서비스 목록** (apps/system/services.js):
1. **FileSystem** — 마운트 디스크 이미지 I/O
2. **Device** — 디바이스 메타데이터
3. **Storage** — 디스크 사용량 (마운트 포인트)
4. **Apps** — 설치된 앱 (OS 이미지에서 로드)
5. **Settings** — 영속 설정 JSON
6. **Terminal** — 셸 명령 실행 (22패턴 위험 명령 필터링)
7. **WiFi** — 호스트 실제 네트워크 스캔
8. **Bluetooth** — 호스트 페어링 디바이스 조회
9. **Browser** — URL 로딩/탐색
10. **Notification** — 알림 생성/조회/채널 관리
11. **Power** — 밝기 동기화, 배터리 상태
12. **Display** — 해상도/회전/DPI (상태 유지)
13. **Input** — 키보드/IME 상태 (상태 유지)
14. **Sensors** — 가속도/자이로/근접/조도 (동적 타임스탬프 + 마이크로 노이즈)
15. **Location** — Rust 백엔드 IP 기반 geolocation
16. **Telephony** — settings 기반 SIM/신호 정보
17. **USB** — 가젯 모드 상태 (상태 유지)
18. **User** — Rust 백엔드 사용자 정보
19. **Credential** — Rust 백엔드 영속 저장소
20. **App Store** — 실제 설치/제거 + 영속성
21. **Updater** — 버전 비교
22. **Sandbox** — 앱별 보안 정책
23. **Logger** — 인메모리 로그 저장소
24. **Accessibility** — settings 기반 접근성 설정
25. **Audio** — 볼륨 키, OSD, 알림 사운드, 진동

**OS 보안 컴포넌트**:
- `apps/system/permissions.js` — ZylPermissions: 앱 권한 실시간 시행
- `apps/system/security.js` — 파일시스템 보호, 자격증명 격리

**Rust 백엔드 보호**:
- `settings.json`, `.credentials/`, `.system/` 파일 접근을 Rust 레벨에서 차단

### OOBE 완료 체크 + 격리
- 부팅 시 `settings.json`의 `oobe_completed` 플래그 확인
- 미완료 시 OOBE 앱을 먼저 실행, 완료 후 홈 화면으로 진입
- OOBE 격리: 최근 앱(Recents)에서 제외, 네비게이션(홈/백) 차단, 전원 토글 시 잠금 없음

### 가상 키보드
- OS 이미지의 `apps/keyboard/`를 에뮬레이터 컴포지터가 마운트
- postMessage를 통해 키 입력을 앱에 전달
- 시스템 앱으로 OS 이미지 내에 포함

### 에뮬레이터 i18n (`emu-i18n.js`)
- 에뮬레이터 컴포지터 UI 전용 번역 (QS 패널, 알림 패널, 최근 앱)
- `data-emu-i18n` DOM 바인딩
- OS 앱과 별도 — 컴포지터가 렌더링하는 시스템 UI에 적용

### 설정 영속성
- Settings 앱에서 변경한 설정은 마운트 포인트의 `settings.json`에 저장
- 재부팅/재시작 시에도 설정 유지

## 디렉토리 구조

```
emulator-app/
├── src/                    Rust 백엔드
│   ├── main.rs             Tauri 엔트리포인트 (커맨드 등록)
│   ├── state.rs            앱 상태 (DeviceConfig, AppState)
│   ├── commands/
│   │   ├── config.rs       디바이스 프로필/설정 관리
│   │   ├── boot.rs         부팅/셧다운 오케스트레이션
│   │   ├── resource.rs     리소스 예약/해제
│   │   ├── filesystem.rs   가상 FS 읽기/쓰기
│   │   ├── os_image.rs     OS 이미지 관리 (list/import/delete)
│   │   ├── settings.rs     설정 영속화
│   │   └── network.rs      WiFi/BT 호스트 조회
│   ├── resource/
│   │   ├── disk_image.rs   디스크 이미지 생성/마운트
│   │   └── memory_limit.rs 메모리 제한 (cgroup/rlimit)
│   └── platform/
│       ├── linux.rs        Linux 전용 (sysfs)
│       └── macos.rs        macOS 전용 (pmset)
├── ui/                     프론트엔드
│   ├── index.html          SPA 엔트리 (설정→부팅→에뮬레이터)
│   ├── css/                config.css, boot.css, emulator.css
│   ├── js/
│   │   ├── config-ui.js    프리부팅 설정 UI
│   │   ├── boot-sequence.js 부팅 애니메이션
│   │   ├── emulator.js     에뮬레이터 코어 (앱 라우팅, 네비게이션)
│   │   ├── services.js     IPC 라우터 — 앱↔OS 서비스 메시지 전달 (비즈니스 로직은 apps/system/)
│   │   ├── emu-i18n.js     에뮬레이터 컴포지터 i18n (QS, 알림, 최근앱)
│   │   ├── hal-tauri.js    Tauri HAL (invoke 기반)
│   │   └── hal-browser.js  브라우저 HAL (Web API 폴백)
│   └── apps/               번들된 시스템 앱 (apps/ 복사)
├── tauri.conf.json         Tauri 앱 설정
├── Cargo.toml              Rust 의존성
└── icons/                  앱 아이콘
```

## OS 이미지 관리

### 저장 위치
```
# macOS: ~/Library/Application Support/zyl-emulator/
# Linux: ~/.local/share/zyl-emulator/

zyl-emulator/
├── os-images/              OS 이미지 (.img 파일)
│   ├── 0.1.0.img           디스크 이미지 (HFS+/ext4)
│   └── 0.1.0.json          이미지 메타데이터
├── images/                 디바이스별 스토리지 이미지
├── mnt/                    스토리지 마운트 포인트
├── vfs/                    마운트 실패 시 폴백 디렉토리
└── devices/                저장된 디바이스 설정 (.json)
```

### OS 이미지 빌드
```bash
# apps/를 .img 디스크 이미지로 패키징
bash scripts/build-image.sh 0.1.0 64
# 결과: ~/Library/Application Support/zyl-emulator/os-images/0.1.0.img
```

### OS 이미지 스캔
설정 화면 Step 2에서 스캔 경로의 `.img` 파일이 자동 리스팅됩니다.
경로 변경 버튼으로 다른 디렉토리를 스캔할 수 있습니다.

## 플랫폼 지원

| 기능 | macOS | Linux |
|------|-------|-------|
| 디스크 이미지 | hdiutil (APFS) | fallocate + ext4 + udisksctl |
| 메모리 제한 | setrlimit (best-effort) | cgroup v2 memory.max |
| WiFi 스캔 | airport -s | nmcli dev wifi list |
| BT 디바이스 | system_profiler | bluetoothctl |
| 배터리 | pmset -g batt | /sys/class/power_supply |

## 라이선스

MIT License
