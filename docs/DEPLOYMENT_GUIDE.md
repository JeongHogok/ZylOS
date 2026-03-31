<!--
  Zyl OS: Deployment Guide
  Copyright (c) 2026 Zyl OS Project
  SPDX-License-Identifier: MIT
-->

# Zyl OS 배포 및 플래싱 가이드

BPI-F3 (SpacemiT K1 RISC-V) 보드에 Zyl OS를 빌드하고 설치하는 방법입니다.

---

## 1. 요구사항

### 호스트 PC
- Ubuntu 24.04+ 또는 Debian 13+
- 최소 50GB 디스크 여유 공간
- 16GB+ RAM 권장

### 대상 디바이스
- Banana Pi BPI-F3 (SpacemiT K1)
- 4GB/8GB/16GB RAM 모델
- microSD 카드 (최소 16GB) 또는 eMMC

### 필수 도구
```bash
sudo apt install -y \
  gcc-riscv64-linux-gnu g++-riscv64-linux-gnu \
  meson ninja-build pkg-config cmake git \
  qemu-system-riscv64 qemu-user-static \
  libwayland-dev wayland-protocols \
  libwlroots-dev libxkbcommon-dev \
  libpixman-1-dev libinput-dev libdrm-dev \
  libegl-dev libgles2-mesa-dev libgbm-dev \
  libgtk-4-dev libwebkitgtk-6.0-dev libjson-glib-dev \
  libssl-dev libseccomp-dev libzip-dev libcurl4-openssl-dev \
  u-boot-tools device-tree-compiler veritysetup
```

또는 자동 설치:
```bash
./tools/setup-toolchain.sh
```

---

## 2. 소스 빌드

### 네이티브 빌드 (호스트 테스트)
```bash
meson setup builddir
ninja -C builddir
```

### 크로스 컴파일 (RISC-V 타겟)
```bash
meson setup builddir-riscv --cross-file tools/riscv64-cross.ini
ninja -C builddir-riscv
```

### Docker 재현 가능 빌드
```bash
docker build -t zylos-build .
docker run --rm -v $(pwd)/output:/output zylos-build
```

Docker 빌드는 호스트 환경에 무관하게 동일한 바이너리를 생성합니다.

### 빌드 결과물
```
builddir-riscv/
├── compositor/zyl-compositor         # Wayland 컴포지터
├── runtime/wam/zyl-wam               # 웹 앱 매니저
├── runtime/services/
│   ├── accessibility/zyl-accessibility
│   ├── account/zyl-account
│   ├── alarm/zyl-alarm
│   ├── audio/zyl-audio
│   ├── auth/zyl-auth
│   ├── bluetooth/zyl-bluetooth
│   ├── camera/zyl-camera
│   ├── crash/zyl-crash-handler
│   ├── credential/zyl-credential
│   ├── display/zyl-display
│   ├── input/zyl-input
│   ├── location/zyl-location
│   ├── logger/zyl-logger
│   ├── nfc/zyl-nfc
│   ├── notification/zyl-notification
│   ├── power/zyl-power
│   ├── sensors/zyl-sensors
│   ├── telemetry/zyl-telemetry
│   ├── telephony/zyl-telephony
│   ├── usb/zyl-usb
│   ├── user/zyl-user
│   └── wifi/zyl-wifi
│   (라이브러리: appstore, dbus, sandbox, updater — 별도 데몬 없음)
```

---

## 3. Verified Boot 키 생성 및 이미지 서명

프로덕션 배포 전 반드시 수행해야 합니다.

### 3.1 부트 키 생성
```bash
# RSA-2048 부트 서명 키 쌍 생성
./tools/gen-boot-keys.sh board/bpi-f3/keys

# 출력:
#   board/bpi-f3/keys/zylos-boot-key.key     ← 비밀키 (절대 배포 금지!)
#   board/bpi-f3/keys/zylos-boot-key.crt     ← 자체 서명 인증서
#   board/bpi-f3/keys/zylos-boot-key.pub     ← PEM 공개키
```

### 3.2 FIT 이미지 서명 + dm-verity 생성
```bash
# FIT 이미지 서명 (커널+DTB+램디스크) + rootfs dm-verity 해시 트리
./tools/sign-image.sh rootfs.img build/signed

# 출력:
#   build/signed/zylos.fit             ← 서명된 FIT 이미지
#   build/signed/rootfs.img            ← dm-verity 해시 트리 임베드된 rootfs
#   build/signed/rootfs.verity         ← dm-verity 해시 트리 (별도 파티션용)
#   build/signed/verity-params.txt     ← dm-verity 파라미터 (부팅 인자 포함)
```

### 3.3 dm-verity rootfs 마운트 인자
```
verity-params.txt 내용 예시:
  root-hash=<sha256-root-hash>
  data-blocks=<N>
  hash-offset=<offset>

U-Boot bootargs에 추가:
  dm-mod.create="vroot none rw 0, 0 <data-blocks> verity V1 PARTUUID=<rootfs-uuid> PARTUUID=<hash-uuid> 4096 4096 <data-blocks> <hash-offset> sha256 <root-hash> <salt>"
```

---

## 4. 루트파일시스템 준비

### Bianbu OS 기반 (권장)
1. [SpacemiT 공식 사이트](https://bianbu-linux.spacemit.com)에서 Bianbu OS 이미지 다운로드
2. microSD에 플래싱:
   ```bash
   sudo dd if=bianbu-desktop.img of=/dev/sdX bs=4M conv=fsync status=progress
   ```
3. SD 카드 마운트 후 Zyl OS 파일 설치

### 파일 설치
```bash
ROOTFS=/mnt/sdcard/rootfs

# 바이너리 설치
sudo cp builddir-riscv/compositor/zyl-compositor $ROOTFS/usr/bin/
sudo cp builddir-riscv/runtime/wam/zyl-wam $ROOTFS/usr/bin/
sudo cp builddir-riscv/runtime/services/*/zyl-* $ROOTFS/usr/bin/

# 앱 설치
sudo mkdir -p $ROOTFS/usr/share/zyl-os/apps
sudo cp -r apps/* $ROOTFS/usr/share/zyl-os/apps/

# WAM Bridge JS
sudo mkdir -p $ROOTFS/usr/share/zyl-os/wam
sudo cp runtime/wam/src/bridge/bridge.js $ROOTFS/usr/share/zyl-os/wam/

# systemd 서비스 (25개)
sudo cp system/*.service system/*.target $ROOTFS/usr/lib/systemd/system/

# 디바이스 트리 오버레이
sudo cp system/dts/bpi-f3-zyl.dts $ROOTFS/boot/

# Plymouth 부트 스플래시
sudo bash system/plymouth/install.sh

# 서비스 활성화
sudo systemd-nspawn -D $ROOTFS systemctl enable zyl-os.target
```

---

## 5. 파티션 레이아웃

### A/B OTA 지원 파티션
```
디바이스: /dev/mmcblk0 (eMMC) 또는 /dev/mmcblk1 (SD)

파티션    크기      마운트        용도
──────────────────────────────────────────
p1       256MB    /boot         부트로더 + FIT 이미지 (서명된 커널+DTB)
p2       4GB      /             루트파일시스템 슬롯 A (dm-verity 보호)
p3       4GB      (inactive)    루트파일시스템 슬롯 B (dm-verity 보호)
p4       256MB    /recovery     복구 모드 이미지
p5       나머지    /data         사용자 데이터 + 앱
```

### 파티션 생성
```bash
sudo parted /dev/mmcblk0 mklabel gpt
sudo parted /dev/mmcblk0 mkpart boot fat32 1MiB 257MiB
sudo parted /dev/mmcblk0 mkpart rootfs_a ext4 257MiB 4353MiB
sudo parted /dev/mmcblk0 mkpart rootfs_b ext4 4353MiB 8449MiB
sudo parted /dev/mmcblk0 mkpart recovery ext4 8449MiB 8705MiB
sudo parted /dev/mmcblk0 mkpart userdata ext4 8705MiB 100%
```

---

## 6. U-Boot 설정

### 환경 변수
```bash
# A/B 슬롯 관리
fw_setenv zyl_active_slot a
fw_setenv zyl_slot_verified 1
fw_setenv zyl_boot_count 0

# Verified Boot 활성화
fw_setenv zyl_verified_boot 1

# 부팅 스크립트 (Verified Boot + dm-verity 포함)
fw_setenv bootcmd 'run zyl_boot'
fw_setenv zyl_boot '
  if test "${zyl_active_slot}" = "a"; then
    setenv bootpart 2;
    setenv hashpart 2;
  else
    setenv bootpart 3;
    setenv hashpart 3;
  fi;
  load mmc 0:1 ${kernel_addr_r} zylos.fit;
  bootm ${kernel_addr_r};
'
```

### dm-verity 부트 인자
`verity-params.txt`에서 생성된 root-hash를 `bootargs`에 포함:
```bash
fw_setenv bootargs "... dm-mod.create=\"vroot ...\" root=/dev/dm-0"
```

---

## 7. 첫 부팅

1. SD 카드를 BPI-F3에 삽입
2. 전원 연결
3. U-Boot → Verified Boot 검증 → Plymouth 부트 스플래시 (10~15초)
4. 잠금화면 표시
5. PIN 입력 (기본: 0000)
6. 홈 화면 진입

### 부팅 문제 해결

| 증상 | 원인 | 해결 |
|------|------|------|
| 화면 안 나옴 | GPU 드라이버 누락 | Bianbu OS의 img-gpu-powervr 패키지 설치 |
| 터치 안 됨 | I2C 터치 드라이버 | dmesg 확인, DT overlay 수정 |
| 컴포지터 크래시 | EGL/GBM 호환성 | `WLR_RENDERER=pixman` 환경변수로 소프트웨어 렌더링 |
| WiFi 안 됨 | SDIO 드라이버 | `modprobe` 확인, firmware 경로 확인 |
| Verified Boot 실패 | FIT 서명 불일치 | `tools/sign-image.sh`로 재서명 후 플래싱 |
| dm-verity 패닉 | rootfs 변조 감지 | 슬롯 B로 롤백 또는 rootfs 재설치 |

---

## 8. OTA 업데이트

### 서버 설정
```bash
# 업데이트 서버 URL 설정
echo "https://update.zyl-os.com/v1" > /etc/zyl-os/update-server

# 수동 업데이트 확인
zyl-updater --check
```

### 업데이트 흐름
1. `zyl-updater --check` → 서버에서 매니페스트 확인
2. `zyl-updater --download` → 비활성 슬롯에 다운로드
3. **SHA-256 해시 무결성 검증**
4. **RSA-2048+SHA-256 서명 검증** (OpenSSL EVP)
5. 비활성 파티션에 적용
6. dm-verity 해시 트리 재생성
7. U-Boot 플래그 변경
8. 재부팅 → 새 슬롯에서 부팅 → Verified Boot 검증
9. 헬스체크 통과 시 슬롯 확정
10. 실패 시 자동 롤백 (부팅 3회 실패 감지)

---

## 9. 개발자 모드

```bash
# 개발자 모드 활성화 (서명 없는 앱 설치 허용)
dbus-send --session --dest=org.zylos.WebAppManager \
  /org/zylos/WebAppManager \
  org.zylos.WebAppManager.SetDevMode \
  boolean:true

# WebKit Inspector 활성화
export WEBKIT_INSPECTOR_SERVER=0.0.0.0:8090
systemctl restart zyl-wam
# 호스트 PC Chrome에서: chrome://inspect
```

---

## 10. CI/CD 파이프라인

GitHub Actions 자동 빌드/검증:

| 잡 | 설명 |
|----|------|
| build-native | Meson + Ninja 네이티브 빌드 |
| build-docker | Docker 재현 가능 빌드 |
| lint-html | HTML DOCTYPE 검증 |
| lint-js | ES5 호환성 + 아키텍처 규칙 |
| test-js | JavaScript 단위 테스트 |
| audit-licenses | 라이선스 호환성 검사 |
| fuzz | libFuzzer 기반 퍼징 |
| test-integration | 통합 테스트 |

---

## 라이선스

Zyl OS는 GNU General Public License v3.0 (GPL-3.0) 하에 배포됩니다.
