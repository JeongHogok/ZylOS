# Zyl OS Emulator

Tauri 2.x 기반 네이티브 디바이스 에뮬레이터. 실제 리소스 예약(디스크 이미지, 메모리 제한)을 지원합니다.

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
│   │   ├── services.js     서비스 라우터 (Tauri invoke 연동)
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
