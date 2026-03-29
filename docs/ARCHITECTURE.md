<!--
  Zyl OS: Architecture Document
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 시스템 아키텍처

## 개요

Zyl OS는 BPI-F3 (SpacemiT K1 RISC-V) 보드를 위한 커스텀 모바일 운영체제입니다.
Linux 커널 위에 Wayland 컴포지터 + WebKitGTK 앱 런타임으로 구성됩니다.

## 시스템 스택

```
┌─────────────────────────────────────────┐
│          Web Apps (HTML/CSS/JS)          │  ← 홈, 설정, 브라우저, 터미널 등
├─────────────────────────────────────────┤
│     WebKitGTK Web Runtime (WAM)         │  ← 앱별 WebView 프로세스
├─────────────────────────────────────────┤
│   JS-Native Bridge (postMessage IPC)    │  ← 하드웨어 접근 API
├─────────────────────────────────────────┤
│      System Services (D-Bus IPC)        │  ← 14개 시스템 서비스
├─────────────────────────────────────────┤
│  Wayland Compositor (wlroots 기반)      │  ← 윈도우/터치/제스처 관리
├─────────────────────────────────────────┤
│       HAL (Hardware Abstraction)        │  ← Linux 드라이버 ↔ 서비스 연결
├─────────────────────────────────────────┤
│   Linux Kernel (SpacemiT BSP 6.6)       │  ← DRM, GPU, 터치, 센서, 모뎀
├─────────────────────────────────────────┤
│        U-Boot + OpenSBI                 │  ← RISC-V 부트로더
└─────────────────────────────────────────┘
```

## 모듈 구조

### Compositor (compositor/)
- wlroots 0.18 기반 Wayland 컴포지터
- 모바일 전용: 풀스크린 앱 스택, 제스처, 상태바
- 모듈: main, input/gesture, output, view
- D-Bus 시그널: GoHome, GoBack, NotificationPanel, AppSwitcher

### WAM - Web Application Manager (runtime/wam/)
- WebKitGTK 기반 앱 실행 환경
- 앱별 독립 WebView 프로세스
- 모듈: manifest, lifecycle (suspend/resume/close), bridge, dbus_service
- JS Bridge: webkit.messageHandlers → D-Bus → 시스템 서비스

### System Services (runtime/services/)

| 서비스 | D-Bus 이름 | 역할 |
|--------|-----------|------|
| notification | org.zylos.Notification | 알림 생성/조회/채널 관리 |
| power | org.zylos.PowerManager | 전력 상태, 밝기, 서스펜드 |
| display | org.zylos.DisplayManager | 해상도, 회전, DPI |
| input | org.zylos.InputService | 가상 키보드, IME, 하드웨어 키 |
| sensors | org.zylos.SensorService | 가속도계, 자이로, 근접, 조도 |
| location | org.zylos.LocationService | GPS (GPSD) + GeoIP 퓨전 |
| telephony | org.zylos.Telephony | ModemManager 통합, 통화/SMS |
| usb | org.zylos.UsbManager | USB 가젯 모드 (MTP/PTP/ADB) |
| user | org.zylos.UserManager | 멀티유저, 프로필 전환 |
| credential | org.zylos.CredentialManager | 암호화 키체인 |
| appstore | (라이브러리) | 패키지 검증/설치 (.ospkg) |
| updater | (라이브러리) | OTA A/B 파티션 업데이트 |
| sandbox | (라이브러리) | seccomp + namespace + cgroup |
| dbus-utils | (라이브러리) | 비동기 D-Bus 유틸리티 |

### HAL - Hardware Abstraction Layer (runtime/hal/)
- C 인터페이스 (hal.h): WiFi, BT, Display, Audio, Battery, Storage
- 실기기 구현: wpa_supplicant, BlueZ, PipeWire, sysfs
- 에뮬레이터 구현: 브라우저 Web API (hal-browser.js)

### Apps (apps/)
- 모든 앱은 HTML/CSS/JS (WebKitGTK에서 실행)
- 공유 모듈: i18n, clock, gesture, bridge
- 에뮬레이터의 services.js를 통해 시스템 데이터 수신
- postMessage IPC로 서비스와 통신

### Emulator — 네이티브 앱 (emulator-app/)
- Tauri 2.x (Rust + WebView) 기반 독립 실행파일
- 프리부팅 설정: 디바이스 프로필, OS 이미지, 저장공간/RAM 선택
- 실제 리소스 예약: sparse 디스크 이미지 마운트, cgroup/rlimit 메모리 제한
- 호스트 연동: WiFi/BT 스캔, 배터리 상태, 파일시스템 I/O
- 설정 영속화: mount_point/settings.json에 JSON 저장
- 배포: macOS (.app/.dmg), Linux (.deb/.AppImage)

## 데이터 흐름

```
앱 (JS) → ZylBridge.notify()
  → postMessage (에뮬레이터) / webkit.messageHandlers (실기기)
  → WAM Bridge Dispatch (핸들러 레지스트리)
  → D-Bus 메서드 호출
  → 시스템 서비스 처리
  → D-Bus 시그널 응답
  → WAM → JS 콜백
```

## 보안 모델

### 5계층 샌드박싱
1. **파일시스템**: mount namespace + bind mount
2. **시스콜**: seccomp-bpf (Strict/Default/Permissive)
3. **네트워크**: network namespace (권한 없으면 차단)
4. **리소스**: cgroup v2 (메모리/CPU/PID 제한)
5. **IPC**: D-Bus 정책 XML (권한별 서비스 접근)

### 앱 서명
- RSA-2048 + SHA-256 패키지 서명
- 개발자 인증서 신뢰 저장소
- 서명 없는 앱 설치 차단 (개발자 모드 예외)

## 부팅 순서

```
U-Boot → OpenSBI → Linux Kernel → systemd
  → Plymouth 스플래시
  → zyl-compositor.service (Wayland)
  → zyl-wam.service (앱 매니저)
  → zyl-notification.service
  → zyl-power.service
  → zyl-sensors.service
  → 잠금화면 표시
  → PIN 입력 → 홈 화면
```

## OTA 업데이트

- A/B 파티션 기반 원자적 업데이트
- 비활성 파티션에 적용 → 부트로더 플래그 전환
- 부팅 3회 실패 시 자동 롤백
- 업데이트 유형: FULL, DELTA, APPS_ONLY, KERNEL
