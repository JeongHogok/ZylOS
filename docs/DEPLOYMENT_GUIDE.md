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
  u-boot-tools device-tree-compiler
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

### 빌드 결과물
```
builddir-riscv/
├── compositor/zyl-compositor    # Wayland 컴포지터
├── runtime/wam/zyl-wam          # 웹 앱 매니저
├── runtime/services/
│   ├── notification/zyl-notification
│   ├── power/zyl-power
│   ├── sensors/zyl-sensors
│   ├── location/zyl-location
│   ├── telephony/zyl-telephony
│   ├── display/zyl-display
│   ├── input/zyl-input
│   ├── usb/zyl-usb
│   ├── user/zyl-user
│   ├── credential/zyl-credential
│   ├── accessibility/zyl-accessibility
│   └── logger/zyl-logger
```

---

## 3. 루트파일시스템 준비

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

# systemd 서비스
sudo cp system/*.service system/*.target $ROOTFS/usr/lib/systemd/system/

# 디바이스 트리 오버레이
sudo cp system/dts/bpi-f3-zyl.dts $ROOTFS/boot/

# Plymouth 부트 스플래시
sudo bash system/plymouth/install.sh

# 서비스 활성화
sudo systemd-nspawn -D $ROOTFS systemctl enable zyl-os.target
```

---

## 4. 파티션 레이아웃

### A/B OTA 지원 파티션
```
디바이스: /dev/mmcblk0 (eMMC) 또는 /dev/mmcblk1 (SD)

파티션    크기      마운트        용도
──────────────────────────────────────────
p1       256MB    /boot         부트로더 + 커널 + DTB
p2       4GB      /             루트파일시스템 (슬롯 A)
p3       4GB      (inactive)    루트파일시스템 (슬롯 B)
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

## 5. U-Boot 설정

### 환경 변수
```bash
# A/B 슬롯 관리
fw_setenv zyl_active_slot a
fw_setenv zyl_slot_verified 1
fw_setenv zyl_boot_count 0

# 부팅 스크립트
fw_setenv bootcmd 'run zyl_boot'
fw_setenv zyl_boot '
  if test "${zyl_active_slot}" = "a"; then
    setenv bootpart 2;
  else
    setenv bootpart 3;
  fi;
  load mmc 0:1 ${kernel_addr_r} Image;
  load mmc 0:1 ${fdt_addr_r} k1-bpi-f3.dtb;
  setenv bootargs "root=/dev/mmcblk0p${bootpart} rootfstype=ext4 rw console=ttyS0,115200 zyl.slot=${zyl_active_slot}";
  booti ${kernel_addr_r} - ${fdt_addr_r};
'
```

---

## 6. 첫 부팅

1. SD 카드를 BPI-F3에 삽입
2. 전원 연결
3. Plymouth 부트 스플래시 표시 (10~15초)
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

---

## 7. OTA 업데이트

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
3. SHA-256 해시 검증
4. 비활성 파티션에 적용
5. U-Boot 플래그 변경
6. 재부팅 → 새 슬롯에서 부팅
7. 헬스체크 통과 시 슬롯 확정
8. 실패 시 자동 롤백

---

## 8. 개발자 모드

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

## 라이선스

Zyl OS는 MIT 라이선스 하에 배포됩니다.
