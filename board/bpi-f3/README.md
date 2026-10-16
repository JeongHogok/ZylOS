# BPI-F3 (SpacemiT K1)

Banana Pi BPI-F3 실기기 보드입니다. 서명 부팅 체인(U-Boot FIT·dm-verity·A/B 슬롯·
부트로더 잠금)을 가진 보드이며, 키·부트로더·서명의 전체 순서는
[DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md) §5 에 있습니다. 이 문서는 이
보드에서 손으로 해야 하는 일만 적습니다.

## 계약 파일

| 파일 | 정하는 것 |
|------|-----------|
| `meson.build` | 보드 계약 — SoC, 아키텍처, HAL 백엔드, 교차 파일, 받는 프로파일 (계약 설명은 `board/meson.build`) |
| `partitions.conf` | 디스크 레이아웃 (읽는 쪽은 그 파일 머리에) |
| `u-boot-env.txt` | 부트 정책 — A/B 시도·롤백, 잠금 전환, 부팅 영역 쓰기 보호. `tests/test_boot_env.sh` 가 실행해 보고 `partitions.conf` 와 대조합니다 |
| `bootloader/` | 제조사 U-Boot 위의 Zyl 패치와 설정 조각 (`tools/build-bootloader.sh`) |
| `fit-image.its` | OS FIT 의 모양 (`tools/sign-image.sh`) |
| `keys/` | 키의 기본 위치. 비밀키는 커밋하지 않습니다 |

제조사 BSP 디바이스 트리에 얹는 오버레이(계약의 `board_dts`)는 보드 디렉터리가 아니라 저장소
루트의 [`system/dts/bpi-f3-zyl.dts`](../../system/dts/bpi-f3-zyl.dts) 에 있습니다.

## 빌드

```bash
meson setup builddir-f3 --prefix=/usr --cross-file tools/cross/riscv64.ini \
      -Dboard=bpi-f3 -Dprofile=<프로파일> -Dota_public_key=<키 디렉터리>/zylos-boot-key.pub
ninja -C builddir-f3
sudo tools/build-image.sh --builddir builddir-f3 --out out/f3
```

`--with-kernel` 은 쓰지 않습니다. 커널·DTB·initramfs 는 서명 FIT 으로 오고, 그 요구
사항은 DEPLOYMENT_GUIDE §5 에 있습니다. QEMU 레인이 보지 못하는 것
([DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md#자기점검-toolszyl-selftest))은 이 보드에서만
확인됩니다.

## 직렬 콘솔

`ttyS0`, 115200 (서명된 커널 인자의 `console=`). 여기에 나오는 것:

- U-Boot 메시지 — 잠금 전환 확인 안내(user 버튼), 슬롯 롤백, 서명 검증 실패
- 커널·systemd 출력, 그리고 출력을 콘솔로도 보내는 유닛(`StandardOutput=journal+console` —
  컴포지터·WAM·자기점검)의 로그

로그인 셸은 없습니다 (이미지가 getty 를 막습니다). U-Boot 명령 줄이 언제 남는지는
[INSTALLATION_INFORMATION.md](../../docs/INSTALLATION_INFORMATION.md#부트로더가-지켜야-하는-것).

## 기록

### 한 매체를 가리킬 것

OS 디스크를 가리키는 값은 모두 같은 매체여야 합니다.

- `partitions.conf` 의 `ZYL_DISK_NODE` — 리눅스가 보는 장치 (fstab, 서명된 커널 인자,
  업데이터가 슬롯을 쓰는 Boot HAL — `runtime/hal/meson.build` 가 여기서 읽는다)
- `u-boot-env.txt` 의 `zyl_mmc` — U-Boot 가 FIT 을 읽고 잠금 전환 때 `/data` 를 지우는 장치

FSBL 이 있는 eMMC 는 `zyl_emmc` 이고, 그 boot0 는 OS 가 도는 동안 쓰기 금지입니다
([INSTALLATION_INFORMATION.md](../../docs/INSTALLATION_INFORMATION.md#부트로더가-지켜야-하는-것)).

### 순서

디스크 이미지의 앞 예약 영역(`ZYL_RESERVED_HEAD_MIB`)은 비어 있으므로 **디스크 이미지를
먼저 쓰고 부트로더를 그 뒤에** 씁니다.

1. **디스크 이미지** `zyl-os-bpi-f3-<프로파일>.img` 를 OS 매체 전체에 씁니다. 이미지의
   GPT 는 이미지 크기에 맞춰져 있고 `/data` 는 최소 크기입니다. `partitions.conf` 의
   `ZYL_SIZE_DATA_MIB=0`(남은 공간 전부)을 실제로 만들려면 GPT 를 매체 끝으로 옮기고
   `/data` 파티션과 그 ext4 를 늘립니다.
2. **부트로더 영역의 GPT 항목**을 더합니다. SPL 은 `opensbi`·`uboot` 를 GPT 이름으로
   찾는데, 이미지의 GPT 에는 OS 파티션만 있습니다. 이 항목들을 OS 파티션 뒤 번호
   (`env`, `opensbi`, `uboot` 순)로, 위치는 예약 영역 안에 둡니다 — OS 파티션 번호는
   바뀌지 않습니다.
3. **부트로더** (`tools/build-bootloader.sh` 의 출력, 자리는 제조사 배치):

   | 파일 | 자리 |
   |------|------|
   | `bootinfo_emmc.bin`, `FSBL.bin` | eMMC boot0 의 0 과 0x200 (제조사 `bootinfo_emmc.json`) |
   | (DDR 학습 데이터) | 사용자 영역 raw 64 KiB — U-Boot 가 씁니다. 겹치지 말 것 |
   | `u-boot-env-default.bin` | GPT `env` — 384 KiB 에서 64 KiB (U-Boot `CONFIG_ENV_OFFSET`) |
   | `fw_dynamic.itb` | GPT `opensbi` — 448 KiB 에서 384 KiB |
   | `u-boot.itb` | GPT `uboot` — 832 KiB 에서 2 MiB |

   SD 카드에서 부팅할 때는 FSBL 이 raw 128 KiB, `bootinfo_sd.bin` 이 섹터 0 의 앞
   80바이트입니다 (제조사 `bootinfo_sd.json`). boot0 는 위의 쓰기 보호 때문에 OS 에서
   바꿀 수 없고 제조사 USB 다운로드 모드(fastboot)로만 씁니다. 제조사 안내의 순서를
   따릅니다: `fastboot stage FSBL.bin`, `fastboot continue`, `fastboot stage u-boot.itb`,
   `fastboot continue`, 그다음 파티션마다 `fastboot flash`.
4. **부트 파티션**(`ZYL_PART_BOOT`)을 FAT 로 포맷하고 `fit-image-a.itb` 를 넣습니다.
   U-Boot 는 여기서 `fit-image-<슬롯>.itb` 를 읽고, 업데이터는 `/boot` 로 씁니다.
5. **`verity-hashtree-a.img`** 를 슬롯 A 의 verity 파티션(`ZYL_PART_VERITY_A`)에 씁니다.

슬롯 B 는 OTA 가 채웁니다. 손으로 채우는 방법(`--slot b`)은
[INSTALLATION_INFORMATION.md](../../docs/INSTALLATION_INFORMATION.md) 에 있습니다.

## eFuse

기본 부트로더는 eFuse 를 굽지 않습니다. 굽는 빌드와 굽기 전 확인은 **되돌릴 수 없으므로**
[INSTALLATION_INFORMATION.md](../../docs/INSTALLATION_INFORMATION.md#efuse--루트-키-해시-굽기-빌드-선택)
를 따르십시오.

## 잠금 해제·소유자 빌드

설정에서 허용 → 재시작 → user 버튼으로 확인 → `/data` 삭제. 절차와 보장은
[INSTALLATION_INFORMATION.md](../../docs/INSTALLATION_INFORMATION.md) 에 있습니다.
