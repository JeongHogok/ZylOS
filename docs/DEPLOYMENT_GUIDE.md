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
  libssl-dev libzip-dev libcurl4-openssl-dev \
  u-boot-tools device-tree-compiler veritysetup \
  debootstrap fdisk e2fsprogs systemd dbus
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
│   ├── alarm/zyl-alarm
│   ├── audio/zyl-audio
│   ├── bluetooth/zyl-bluetooth
│   ├── camera/zyl-camera
│   ├── credential/zyl-credential
│   ├── display/zyl-display
│   ├── input/zyl-input
│   ├── location/zyl-location
│   ├── logger/zyl-logger
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

## 4. 이미지 만들기

소스에서 **설치 가능한 디스크 이미지 한 장**이 나옵니다. 손으로 파일을 복사하는
절차는 없습니다 — 무엇이 들어가는지는 `ninja install` 이, 어디에 들어가는지는
`board/<id>/partitions.conf` 가, 어떤 패키지가 필요한지는 `system/packages.list`
가 정합니다.

```bash
# 리눅스(또는 컨테이너)에서, root 로
meson setup builddir --prefix=/usr -Dboard=bpi-f3 -Dprofile=phone
ninja -C builddir
sudo tools/build-image.sh --builddir builddir --out out/image
```

출력:

```
out/image/zyl-os-bpi-f3-phone.img   디스크 이미지 (GPT, 슬롯 A 기록, /data 포맷)
out/image/rootfs-phone.img          rootfs 단독 — tools/sign-image.sh 의 입력
```

- `--prefix=/usr` 가 아니면 거부합니다 (유닛의 `ExecStart` 가 `/usr/bin` 을 가리킴)
- 다른 아키텍처면 2단계 부트스트랩을 합니다 — `qemu-user-static` 과 binfmt 등록이 필요합니다
- 슬롯 B·verity 는 비워 둡니다. 채우는 쪽은 `tools/sign-image.sh` 와 OTA 입니다
- `--rootfs-only` 로 rootfs 이미지만 만들 수 있습니다 (서명·verity 작업용)

### 보드 없이 검증 (QEMU)

```bash
sudo tools/run-qemu.sh --image out/image/zyl-os-emulator-phone.img \
     --builddir builddir --check
```

`--check` 는 커널 인자로 자기점검(`zyl-selftest`)을 켜고, 그 결과를 종료 코드로
돌려줍니다. 검증 범위: systemd 유닛 기동, 시스템 버스와 정책, 서비스 이름,
앱 디렉터리·`/data`, 화면 체인(`/dev/dri` → Wayland 소켓 → WAM 이 띄운 앱).
**검증하지 않는 것**: U-Boot·서명 FIT·dm-verity·A/B 전환
(부트로더 빌드와 `tools/sign-image.sh`·`tests/test_boot_env.sh` 소관), SoC 고유
경로(GPU·모뎀·ISP). CI 의 `qemu-boot` 잡이 매 커밋 같은 일을 합니다.

기기에서도 같은 자기점검을 손으로 돌릴 수 있습니다: `systemctl start zyl-selftest`.

### 실행 시점 패키지

이미지에 들어가는 패키지는 `system/packages.list` 한 곳에 있습니다. 줄마다
없을 때 무엇이 깨지는지 적혀 있고, 아래 설명은 그중 눈여겨볼 것들입니다.

### 글꼴 패키지 (필수)

최소 rootfs 에는 CJK·컬러 이모지 글꼴이 없습니다. 넣지 않으면
한국어·일본어·중국어 UI 와 이모지가 전부 두부(▯)로 표시됩니다 — 5개 언어
번역이 있어도 보이지 않습니다.

`system/packages.list` 의 `fonts-noto-*` 네 줄이 이것입니다
(Arch 계열 rootfs 를 쓸 때는 `noto-fonts noto-fonts-cjk noto-fonts-emoji`).

`ninja install` 이 `/etc/fonts/conf.d/50-zyl-fonts.conf` 를 설치해 기본 글꼴과
언어별 한자 자형(한·중·일은 같은 코드포인트를 다르게 그린다)을 정합니다.

### 앱 격리 패키지 (필수)

앱 코드가 도는 WebKitGTK 웹 프로세스는 WebKit 이 bubblewrap 으로 격리합니다. 이 두
패키지가 없으면 샌드박스가 **조용히 꺼진 채** 앱이 정상 동작하므로 눈치채기 어렵습니다.

`system/packages.list` 의 `bubblewrap`·`xdg-dbus-proxy` 두 줄이 이것입니다.

확인: 앱을 띄운 뒤 `ps -o args= -C WebKitWebProcess` 에 `bwrap` 이 보여야 합니다.

### 네트워크·모뎀 패키지 (필수)

모바일 데이터, 데이터 한도, 재난문자는 NetworkManager·ModemManager 위에서
동작합니다. APN 은 통신사별 표를 OS 에 두지 않고 `mobile-broadband-provider-info`
데이터베이스에서 자동으로 고릅니다 (`gsm.auto-config`).

`system/packages.list` 의 `network-manager`·`modemmanager`·
`mobile-broadband-provider-info` 세 줄이 이것입니다.

| 구성요소 | 최소 버전 | 없을 때 |
|---------|----------|--------|
| ModemManager | 1.24 | 재난문자(Cell Broadcast) 수신 불가 |
| NetworkManager | 1.22 | APN 자동 설정 불가 |
| NetworkManager | 1.58 | IPv6 전용망에서 IPv4 앱 불가 (464XLAT CLAT) — 이중 스택은 동작 |

## 5. 파티션 레이아웃

A/B OTA 와 dm-verity 를 위한 배치입니다. **단일 출처는
`board/<id>/partitions.conf`** 이고, 이미지 빌더(`tools/build-image.sh`)·서명
도구(`tools/sign-image.sh`)·부트 정책(`board/bpi-f3/u-boot-env.txt` 의
`zyl_boot_part`·`zyl_data`)이 모두 그 파일을 읽습니다.
`tests/test_boot_env.sh` 가 부트 정책과 레이아웃이 어긋나면 실패시킵니다.

```
p1  boot       256 MiB   슬롯별 서명 FIT (fit-image-a.itb, fit-image-b.itb)
p2  rootfs_a  4096 MiB   루트파일시스템 슬롯 A (dm-verity)
p3  verity_a   256 MiB   슬롯 A 해시 트리
p4  rootfs_b  4096 MiB   슬롯 B (OTA 가 받는 쪽)
p5  verity_b   256 MiB   슬롯 B 해시 트리
p6  userdata   나머지    /data — 부트로더 잠금 상태가 바뀌면 지워진다
```

첫 파티션 앞 4 MiB 는 부트로더 영역(FSBL·OpenSBI·U-Boot·env)으로 비워 둡니다.
`parted` 로 손으로 나눌 필요는 없습니다 — 이미지 빌더가 이 표대로 만들고,
만든 순서가 선언과 어긋나면 실패합니다.

`/data` 는 지워진 뒤 빈 파티션이 되므로 처음 마운트할 때 포맷되게 둘 수 있습니다:

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
