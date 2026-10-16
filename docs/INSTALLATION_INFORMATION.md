# 설치 정보 (Installation Information) — GPLv3 §6

Zyl OS 는 GPL-3.0 으로 배포됩니다. GPLv3 §6 은 이 소프트웨어를 담은 **사용자 제품**
(휴대폰·태블릿 등)을 배포할 때, 소유자가 **수정한 버전을 설치하고 실행하는 데 필요한
방법·절차·인증 키**를 함께 제공하라고 요구합니다. 검증 부팅(서명된 부트 이미지 +
dm-verity)만 두면 제조사 키로 서명한 이미지만 부팅되어 이 요구와 충돌합니다.

Zyl OS 는 **소유자가 직접 여는 부트로더 잠금 해제**로 이를 해결합니다. 잠금 해제된
기기의 U-Boot 는 OS 이미지(FIT)의 서명을 검증하지 않으므로, 소유자가 자기 키로 만든
이미지가 부팅됩니다. 그 대가로 잠금 상태가 바뀔 때마다 사용자 데이터가 지워지고, 잠금
해제 상태는 부팅마다 알려집니다.

이 문서가 대상으로 하는 보드는 BPI-F3 입니다. 결정 로직은 모든 보드가 같은 부트 정책
`board/boot-policy.txt`(슬롯 선택, 잠금 전환, `bootcmd`)와 이 보드의 상수·훅
`board/bpi-f3/u-boot-env.txt`(버튼 확인, eMMC 의 `/data` 삭제, eFuse, 부팅 영역 쓰기 보호)에
있고, `tests/test_boot_env.sh` 가 두 파일을 빌드와 같이 합쳐 그대로 실행해 검증합니다.

## 보장

| 성질 | 어떻게 |
|------|--------|
| 소유자가 허용해야 요청할 수 있다 | 설정 > 개발자 옵션 > **부트로더 잠금 해제 허용**. `bootctl` 서비스(`runtime/services/bootctl`)는 허용 없이는 잠금 해제 요청을 기록하지 않고, 부트로더도 허용 여부를 다시 확인합니다. `bootctl` 은 `system` 권한이 필요해 시스템 앱만 부릅니다 |
| 요청만으로는 바뀌지 않는다 | OS 는 요청을 부트로더 환경에 기록할 뿐입니다. 다음 부팅 때 부트로더는 요청(`zyl_unlocked`)이 지난 상태(`zyl_lock_seen`)와 다르면 **기기의 버튼 확인**을 기다리고, 확인이 없으면 요청을 취소합니다 |
| 잠금 해제로 데이터를 넘기지 않는다 | 상태가 바뀔 때마다(해제·재잠금 모두) 부트로더가 OS 를 띄우기 전에 `/data` 파티션을 지웁니다 (`zyl_wipe`). 루트는 읽기 전용이라 OS 와 앱이 남기는 상태(키체인·저장한 네트워크 포함)는 모두 `/data` 에 있습니다 ([CLAUDE.md](../CLAUDE.md) §8-2). 전환 뒤 허용 스위치는 다시 꺼집니다 |
| 사용자가 상태를 안다 | 잠금 해제 상태면 부트로더가 부팅마다 경고를 출력하고, 설정 > 개발자 옵션 > 부트로더에 "잠금 해제됨 — 검증되지 않은 소프트웨어"가 보입니다. 기록만 되고 아직 확인되지 않은 요청은 "다음 부팅 때 확인 대기"로 보입니다 |
| 잠금 해제는 OS 검증만 끈다 | 부트로더(FSBL·OpenSBI·U-Boot)는 잠금과 무관하게 검증되고, 부팅 영역 쓰기 보호도 그대로입니다. `tools/sign-image.sh` 로 만든 이미지는 잠금 해제 기기에서도 initramfs 가 rootfs 를 그 이미지 자신의 커널 인자에 든 dm-verity 값으로 엽니다 |

## 1. 잠금 해제

1. 설정 > 개발자 옵션 > **부트로더 잠금 해제 허용**을 켭니다.
2. **잠금 해제 요청**을 누르고 경고를 읽은 뒤 **요청하기**를 누릅니다.
3. 기기를 다시 시작하면서 버튼(`board/bpi-f3/u-boot-env.txt` 의 `zyl_unlock_button`)을
   누르고 있습니다. 부트로더는 확인 시간 동안 버튼을 보고, 그 안내
   (`bootloader lock change requested. ALL USER DATA WILL BE ERASED.`)를 직렬 콘솔에
   출력합니다 ([board/bpi-f3/README.md](../board/bpi-f3/README.md) 의 직렬 콘솔).
   누르지 않으면 아무것도 바뀌지 않습니다.
4. `/data` 가 지워지고 잠금이 해제됩니다.

## 2. 수정한 버전 빌드와 설치

**대응 소스**는 이 저장소 전체입니다. 빌드와 rootfs 조립은
[DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) §2–3 을 그대로 따르고, 보드는 `bpi-f3`,
교차 파일은 그 보드의 `board_cross_file`(`tools/cross/riscv64.ini`)입니다. 결과 중
`rootfs-<프로파일>.img` 가 아래의 입력입니다.

```bash
# 자신의 키 — 잠금 해제 기기는 OS FIT 의 서명을 보지 않지만, 서명 도구는 키로 FIT 를 만든다
tools/gen-boot-keys.sh ~/zyl-keys

# 슬롯마다 서명 이미지: 커널 인자(슬롯의 파티션, dm-verity 루트 해시, zyl.slot)가
# 서명되는 DTB 안에 들어간다
ZYL_KERNEL=<vmlinuz> ZYL_DTB=<보드.dtb> ZYL_INITRAMFS=<initramfs.cpio.gz> \
  tools/sign-image.sh --slot a --key-dir ~/zyl-keys <outdir>/rootfs-<프로파일>.img out/a
ZYL_KERNEL=<vmlinuz> ZYL_DTB=<보드.dtb> ZYL_INITRAMFS=<initramfs.cpio.gz> \
  tools/sign-image.sh --slot b --key-dir ~/zyl-keys <outdir>/rootfs-<프로파일>.img out/b
```

커널·DTB·initramfs 의 요건은 [DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) §5 의 "OS 이미지
서명"에 있습니다.

**기록할 것** — 파티션 번호는 `board/bpi-f3/partitions.conf` 가 정합니다.

| 파일 | 대상 |
|------|------|
| `out/<슬롯>/fit-image-<슬롯>.itb` | 부트 파티션(`ZYL_PART_BOOT`)의 같은 이름의 파일 — U-Boot 가 `fit-image-<슬롯>.itb` 를 읽습니다 |
| `rootfs-<프로파일>.img` | 그 슬롯의 rootfs 파티션 (`ZYL_PART_ROOT_A` / `ZYL_PART_ROOT_B`) |
| `out/<슬롯>/verity-hashtree-<슬롯>.img` | 그 슬롯의 verity 파티션 (`ZYL_PART_VERITY_A` / `ZYL_PART_VERITY_B`) |

- 한 슬롯의 세 파일은 같은 rootfs 로 만든 한 벌이어야 합니다 — 루트 해시가 그 바이트에 묶입니다.
- U-Boot 는 시험 중인 업데이트가 없으면 마지막으로 확정된 슬롯(`zyl_active_slot`)을 부팅하므로, 두 슬롯 모두 쓰면
  어느 쪽이 활성이든 소유자 이미지가 뜹니다.
- 기기 안에는 셸이 없으므로 처음 기록은 기기 밖에서 합니다. 매체별 방법은
  [board/bpi-f3/README.md](../board/bpi-f3/README.md) 의 기록.
- 그 뒤로 OTA 로 소유자 이미지를 받으려면, 기기에서 도는 이미지가 소유자의 공개키를
  가져야 합니다: `-Dota_public_key=<~/zyl-keys/zylos-boot-key.pub>` 로 빌드하고
  `tools/build-ota.sh --key-dir ~/zyl-keys` 로 패키지를 만듭니다
  ([DEPLOYMENT_GUIDE.md](DEPLOYMENT_GUIDE.md) §6).

## 3. 다시 잠그기

다시 잠그면 U-Boot 가 제조사 키로 OS FIT 를 검증합니다. 잠긴 기기는 검증되는 슬롯을
부팅하지 못하면 셸 없이 재시작을 반복하고, OS 가 뜨지 않으니 잠금 해제를 다시 요청할
수도 없습니다. 그래서 **먼저 두 슬롯 모두에 공식 서명 이미지를** 2장과 같은 방법으로
기록합니다.

1. 설정 > 개발자 옵션 > **다시 잠그기 요청** → **요청하기** (허용 스위치는 필요 없습니다)
2. 다시 시작하며 버튼으로 확인 — `/data` 가 지워지고 검증 부팅이 다시 켜집니다.

## 부트로더가 지켜야 하는 것

위 보장은 부트 정책이 부트로더에 내장되어 바깥에서 바꿀 수 없을 때만 성립합니다. 하나라도
빠지면 환경 저장소를 쓸 수 있는 누구나 `bootcmd` 를 바꿔 검증을 끌 수 있습니다.

- 부트 정책은 U-Boot 의 기본 환경으로 내장되고, 저장소에서는 `board/boot-policy.txt` 의
  "Runtime state" 절 변수만 읽습니다 (쓰기 허용 목록 — 빌드가 그 절에서 만듭니다)
- U-Boot 셸·자동 부팅 중단·fastboot 의 환경·설정 접근이 없고, 서명 없는 이미지 형식
  (레거시·Android)을 부팅하지 않습니다
- 제조사 U-Boot 의 bootfs·네트워크 환경 가져오기, SD 카드 자동 플래싱, 요청 셸을 끕니다
- 잠긴 기기는 검증되는 슬롯을 부팅하지 못하면 셸 없이 재시작합니다. U-Boot 명령 줄이
  직렬 콘솔에 남는 것은 **잠금 해제된 기기에서, 시험 중인 업데이트(`zyl_next_slot`)가
  없을 때 슬롯의 FIT 를 읽거나 부팅하지 못한 경우**뿐입니다 (`board/boot-policy.txt` 의 `bootcmd`)
- OS 로 넘기기 전에 FSBL 이 있는 eMMC boot0 를 다음 전원 차단까지 쓰기 금지합니다 —
  침해된 OS 가 부트로더를 바꿀 수 없습니다
- 기기 ID 는 칩 ID 에서 옵니다
- 커널 인자는 환경이 아니라 서명되는 DTB 안에 있습니다

`tools/build-bootloader.sh` 가 고정한 제조사 U-Boot·OpenSBI 커밋에
`board/bpi-f3/bootloader`(패치와 설정 조각 `uboot.config`)를 얹어 그렇게 빌드하고, 설정이
결과에 남았는지와 서명을 스스로 검증합니다. 설정 항목과 그 이유는 `uboot.config` 와 패치에
적혀 있습니다.

잠금 해제는 OS 이미지의 검증만 끕니다. 부트로더는 잠금과 무관하게 제조사 키로 검증되지만,
잠금을 해제한 소유자는 수정한 Zyl OS 를 설치·실행할 수 있으므로 GPLv3 §6 의 설치 정보
요건과 충돌하지 않습니다 (U-Boot 는 GPL-2.0+, OpenSBI 는 BSD-2-Clause).

## eFuse — 루트 키 해시 굽기 (빌드 선택)

Boot ROM 은 eFuse 의 루트 공개키 해시(ROTPKH, bank 4)가 비어 있으면 FSBL 의 서명을 보지
않습니다. 그 상태에서는 기기를 손에 쥔 사람이 SD 카드나 USB 로 자기 FSBL 을 띄울 수
있습니다. 해시를 구우면 그때부터 Boot ROM 은 `zylos-rotpk` 로 서명된 FSBL 만 실행합니다.
OS 의 설치 가능성(위 1–3장)은 이것과 무관합니다.

```bash
tools/build-bootloader.sh --key-dir <keys> --burn-rotpk out/
```

- 이렇게 빌드한 U-Boot 만 굽습니다 (`zyl_rotpk_burn=1`). 기본 빌드에는 굽는 코드가 없습니다
- **잠긴 기기의 부팅 중에, eMMC boot0 의 FSBL 이 바로 그 빌드의 FSBL 일 때만**(바이트
  단위 SHA-256 비교) 굽고 bank 4 를 잠급니다. eFuse 가 이미 같은 값이면 아무것도 하지 않고,
  다른 값이면 거부합니다. 빌드는 굽든 굽지 않든 자기가 만든 FSBL 의 루트 키·`rotpk_hash`·두 서명을 검증하고
  구울 해시를 `rotpk-sha256.txt` 로 남깁니다
- **되돌릴 수 없습니다.** 루트 키(`zylos-rotpk.key`)를 잃으면 그 기기의 부트로더는 더 이상
  바꿀 수 없습니다
- **제조사가 형식을 문서화하지 않았습니다.** Zyl OS 는 제조사 도구가 FSBL 안에 넣는
  `rotpk_hash`(FSBL 앞 256바이트 RSA 모듈러스의 SHA-256)를 그대로 굽습니다. Boot ROM 이 다른
  형식을 기대하면 그 보드는 어떤 FSBL 도 부팅하지 못합니다. **버려도 되는 보드로 먼저
  확인하십시오.** 제조사 eFuse 드라이버는 bank 8 미만 쓰기를 막아 두었고, Zyl 패치가
  bank 4 에 한해 그 경로를 엽니다 (`board/bpi-f3/bootloader`)

### 굽기 전에 — `zyl_rotpk show`

모든 Zyl 빌드에 **읽기 전용** 명령 `zyl_rotpk show` 가 있어, 되돌릴 수 없는 쓰기 전에
형식을 맞춰 볼 수 있습니다. 명령 줄이 남는 경우는 [부트로더가 지켜야 하는
것](#부트로더가-지켜야-하는-것)에 있습니다.

```
=> zyl_rotpk show
zyl_rotpk: bank 4 (ROTPKH) now:  <bank 4 의 32바이트>
zyl_rotpk: bank 6 (locks) now:   <bank 6 의 32바이트>
zyl_rotpk: a burn would write:   <eMMC boot0 FSBL 앞 256바이트의 SHA-256>
zyl_rotpk: this build cannot burn (no --burn-rotpk)
```

확인할 것 세 가지 — 하나라도 다르면 굽지 마십시오:

1. bank 4 가 ROTPKH 인지 — 제조사 도구로 구운 보드의 bank 4 가 그 보드의 `a burn would write` 값과 같아야 함
2. 바이트 순서 — 제조사 도구 출력과 같은 순서로 보이는지
3. bank 6 word 0 의 잠금 비트 위치 (bit 4 = bank 4) — 제조사 도구로 잠근 보드와 대조

세 항목이 맞는 것을 **버려도 되는 보드에서** 확인한 뒤에만 `--burn-rotpk` 빌드로 넘어가십시오.
