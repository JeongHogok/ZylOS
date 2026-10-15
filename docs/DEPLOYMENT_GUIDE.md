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
meson setup builddir-riscv --cross-file board/bpi-f3/cross-riscv64.ini
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
# 부팅 체인의 RSA-2048 키 — 없는 것만 만들고 있는 키는 그대로 둔다
./tools/gen-boot-keys.sh board/bpi-f3/keys

# 출력 (*.key 는 비밀키 — 절대 배포 금지!):
#   zylos-boot-key.{key,crt,pub}   OS FIT (U-Boot 가 검증)
#   zylos-fw-key.{key,crt,pub}     OpenSBI·U-Boot FIT (SPL 이 검증)
#   zylos-spl-key.{key,pub}        FSBL 안의 SPL (Boot ROM 이 검증)
#   zylos-rotpk.{key,pub}          루트 키 — 해시가 eFuse 에 (선택)
```

### 3.1.1 부트로더 빌드
```bash
# 고정한 제조사 U-Boot·OpenSBI + Zyl 부트 정책, 서명하고 결과를 스스로 검증
./tools/build-bootloader.sh --key-dir board/bpi-f3/keys build/bootloader
# eFuse 에 루트 키 해시를 굽는 부트로더 (되돌릴 수 없음): --burn-rotpk
# 설치 위치: board/bpi-f3/README.md "Bootloader"
```

### 3.2 FIT 이미지 서명 + dm-verity 생성
```bash
# FIT 이미지 서명 (커널+DTB+램디스크) + rootfs dm-verity 해시 트리
./tools/sign-image.sh rootfs.img build/signed

# 출력 (슬롯 a, --slot b 면 b):
#   build/signed/fit-image-a.itb        ← 서명된 FIT (커널 인자가 서명되는 DTB 안에)
#   build/signed/verity-hashtree-a.img  ← dm-verity 해시 트리 (verity 파티션용)
#   build/signed/cmdline-a.txt          ← 그 커널 인자
#   build/signed/verity-params-a.txt    ← dm-verity 파라미터
#   build/signed/MANIFEST.sha256
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

### 글꼴 패키지 (필수)

Bianbu 기본 이미지에는 CJK·컬러 이모지 글꼴이 없습니다. 설치하지 않으면
한국어·일본어·중국어 UI 와 이모지가 전부 두부(▯)로 표시됩니다 — 5개 언어
번역이 있어도 보이지 않습니다.

```bash
sudo systemd-nspawn -D $ROOTFS apt-get install -y \
  fonts-noto-core fonts-noto-mono fonts-noto-cjk fonts-noto-color-emoji
# Arch 계열 rootfs: pacman -S noto-fonts noto-fonts-cjk noto-fonts-emoji
```

`ninja install` 이 `/etc/fonts/conf.d/50-zyl-fonts.conf` 를 설치해 기본 글꼴과
언어별 한자 자형(한·중·일은 같은 코드포인트를 다르게 그린다)을 정합니다.

### 네트워크·모뎀 패키지 (필수)

모바일 데이터, 데이터 한도, 재난문자는 NetworkManager·ModemManager 위에서
동작합니다. APN 은 통신사별 표를 OS 에 두지 않고 `mobile-broadband-provider-info`
데이터베이스에서 자동으로 고릅니다 (`gsm.auto-config`).

```bash
sudo systemd-nspawn -D $ROOTFS apt-get install -y \
  network-manager modemmanager mobile-broadband-provider-info
# Arch 계열 rootfs: pacman -S networkmanager modemmanager mobile-broadband-provider-info
```

| 구성요소 | 최소 버전 | 없을 때 |
|---------|----------|--------|
| ModemManager | 1.24 | 재난문자(Cell Broadcast) 수신 불가 |
| NetworkManager | 1.22 | APN 자동 설정 불가 |
| NetworkManager | 1.58 | IPv6 전용망에서 IPv4 앱 불가 (464XLAT CLAT) — 이중 스택은 동작 |

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

# systemd 서비스 + 세션 타겟
# zyl-os.target 은 -Dprofile 에 따라 빌드 디렉토리에 생성된다 (system/ 에는
# .target.in 템플릿만 있다). 프로파일이 빌드하지 않은 서비스를 Wants= 에
# 넣지 않으려면 반드시 생성된 파일을 쓸 것.
sudo cp system/*.service $ROOTFS/usr/lib/systemd/system/
sudo cp builddir/zyl-os.target $ROOTFS/usr/lib/systemd/system/

# 서비스 사용자 + /data 디렉터리 — 없으면 WAM(User=zyl-wam)과 /data 를 쓰는
# 서비스들이 기동 전에 실패한다
sudo cp system/sysusers.d/*.conf $ROOTFS/usr/lib/sysusers.d/
sudo cp system/tmpfiles.d/*.conf $ROOTFS/usr/lib/tmpfiles.d/

# 또는 전체를 프로파일에 맞게 한 번에 설치 (권장)
#   sudo DESTDIR=$ROOTFS ninja -C builddir install

# 디바이스 트리 오버레이
sudo cp system/dts/bpi-f3-zyl.dts $ROOTFS/boot/

# Plymouth 부트 스플래시
sudo bash system/plymouth/install.sh

# 서비스 활성화
sudo systemd-nspawn -D $ROOTFS systemctl enable zyl-os.target
```

---

## 5. 파티션 레이아웃

A/B OTA 와 dm-verity 를 위한 배치입니다. 부트 스크립트(`board/bpi-f3/u-boot-env.txt`),
서명 도구(`tools/sign-image.sh`), 업데이터가 모두 이 번호를 씁니다 — 단일 출처는
`board/bpi-f3/README.md` 의 표입니다.

```
p1  boot       256MB   FAT   fit-image-a.itb, fit-image-b.itb (슬롯별 서명 이미지)
p2  rootfs_a   4GB           루트파일시스템 슬롯 A (dm-verity)
p3  verity_a   256MB         슬롯 A 해시 트리
p4  rootfs_b   4GB           루트파일시스템 슬롯 B (dm-verity)
p5  verity_b   256MB         슬롯 B 해시 트리
p6  userdata   나머지  ext4  /data — 부트로더 잠금 상태가 바뀌면 지워진다
```

```bash
sudo parted /dev/mmcblk0 mklabel gpt
sudo parted /dev/mmcblk0 mkpart boot fat32 1MiB 257MiB
sudo parted /dev/mmcblk0 mkpart rootfs_a ext4 257MiB 4353MiB
sudo parted /dev/mmcblk0 mkpart verity_a 4353MiB 4609MiB
sudo parted /dev/mmcblk0 mkpart rootfs_b ext4 4609MiB 8705MiB
sudo parted /dev/mmcblk0 mkpart verity_b 8705MiB 8961MiB
sudo parted /dev/mmcblk0 mkpart userdata ext4 8961MiB 100%
```

`/data` 는 지워진 뒤 빈 파티션이 되므로 rootfs 의 `/etc/fstab` 에서 처음 마운트할 때
포맷되게 둡니다:

```
PARTLABEL=userdata  /data  ext4  defaults,noatime,x-systemd.makefs,x-systemd.growfs  0 2
```

---

## 6. U-Boot 설정

부트 로직(A/B 시도·롤백, 부트로더 잠금)은 `board/bpi-f3/u-boot-env.txt` 가 유일한
출처입니다. 문서에 스크립트를 따로 적지 않습니다 — 출하용은 U-Boot 기본 환경으로
빌드에 넣고(`CONFIG_USE_DEFAULT_ENV_FILE`), 개발 보드에서는 환경 이미지로 씁니다:

```bash
mkenvimage -s 0x20000 -o uboot.env board/bpi-f3/u-boot-env.txt
```

슬롯별 이미지 서명과 소유자 빌드 설치 절차, 출하용 U-Boot 필수 설정은
[INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) 를 보세요.

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

### 패키지 만들기
```bash
# 두 슬롯(a·b)용 verity 트리와 서명 FIT 를 만들고 rootfs 와 함께 묶어 서명
./tools/build-ota.sh --url https://update.example.com/zylos-0.2.0.pkg 0.2.0 rootfs.img build/ota
#   build/ota/zylos-0.2.0.pkg   ← tar.gz: rootfs.img, verity-hashtree-{a,b}.img, fit-image-{a,b}.itb
#   build/ota/zylos-0.2.0.json  ← 서버가 /check 에 돌려줄 응답 (sha256·size·signature 포함)
```
패키지는 부트 서명 키(`board/bpi-f3/keys/zylos-boot-key.key`)로 서명합니다.
기기는 그 공개키를 `/etc/zyl-os/ota-signing-key.pem` 으로 가져야 하며, 빌드 때
`meson setup ... -Dota_public_key=board/bpi-f3/keys/zylos-boot-key.pub` 로 넣습니다.
공개키가 없으면 기기는 업데이트를 받지 않습니다.

### 서버
```bash
echo "https://update.example.com/v1" > /etc/zyl-os/update-server
```
업데이터는 `GET <서버>/check?version=<현재>&arch=riscv64&slot=<a|b>` 를 보내고,
새 버전이 있으면 `zylos-<버전>.json` 내용을, 없으면 `{"available": false}` 를 받습니다.

### 업데이트 흐름 (`zyl-updater`, 설정 > 소프트웨어 업데이트)
1. 확인 → 서버 응답(매니페스트)
2. 다운로드 → SHA-256 확인 → RSA-SHA256 서명 확인 (서명 없는 패키지 거부)
3. 비활성 슬롯에만 기록: rootfs → verity 트리 → FIT (FIT 는 임시 이름으로 쓰고 교체)
4. 모든 기록 후에만 U-Boot 에 새 슬롯 지정 (`zyl_next_slot`, `zyl_boot_count=0`)
5. 재시작 → U-Boot 가 새 슬롯의 FIT 서명을 확인하고 부팅 → dm-verity 가 rootfs 확인
6. 부팅 60초 뒤 컴포지터·WAM 이 정상이면 슬롯 확정, 아니면 재시작
7. 새 슬롯이 3번 안에 확정되지 않으면 U-Boot 가 이전 슬롯으로 돌아감

전체 이미지만 설치합니다 (delta·앱만·커널만 패키지는 dm-verity rootfs 와 맞출 수 없어 거부).

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
