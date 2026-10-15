# 설치 정보 (Installation Information) — GPLv3 §6

Zyl OS 는 GPL-3.0 으로 배포됩니다. GPLv3 §6 은 이 소프트웨어를 담은 **사용자 제품**
(휴대폰·태블릿 등)을 배포할 때, 소유자가 **수정한 버전을 설치하고 실행하는 데 필요한
방법·절차·인증 키**를 함께 제공하라고 요구합니다. 검증 부팅(서명된 부트 이미지 +
dm-verity)만 두면 제조사 키로 서명한 이미지만 부팅되어 이 요구와 충돌합니다.

Zyl OS 는 **소유자가 직접 여는 부트로더 잠금 해제**로 이를 해결합니다. 잠금 해제된
기기는 서명을 검증하지 않으므로 소유자가 빌드한 이미지가 부팅됩니다. 그 대가로
기기의 사용자 데이터는 지워지고, 이후 부팅마다 경고가 표시됩니다.

## 보안 성질

| 성질 | 구현 |
|------|------|
| 소유자만 요청할 수 있다 | 설정 > 개발자 옵션 > "부트로더 잠금 해제 허용"(화면 잠금 해제 상태의 시스템 설정 앱만 호출 가능한 `bootctl` 서비스) |
| 원격으로는 풀 수 없다 | 실제 전환은 다음 부팅 때 부트로더가 **기기 버튼을 10초 안에 누르고 있는지** 확인해야 일어난다 |
| 도난 기기에서 데이터를 빼낼 수 없다 | 잠금 상태가 바뀔 때마다(해제·재잠금 모두) 부트로더가 `/data` 파티션을 지운다 — 새 OS 가 뜨기 전에 |
| 사용자가 상태를 안다 | 잠금 해제 상태에서는 부팅마다 경고, 설정 > 개발자 옵션 > 부트로더에 "잠금 해제됨" |

결정 로직은 `board/bpi-f3/u-boot-env.txt` 한 곳에 있고 `tests/test_boot_env.sh` 가
그 파일을 그대로 실행해 검증합니다.

## 1. 잠금 해제

1. 설정 > 개발자 옵션 > **부트로더 잠금 해제 허용**을 켭니다.
2. **잠금 해제 요청**을 누르고 경고를 읽은 뒤 **요청하기**를 누릅니다.
3. 기기를 다시 시작합니다. 부트로더가
   `bootloader lock change requested. ALL USER DATA WILL BE ERASED` 를 표시하면
   **user 버튼을 누른 채로** 10초 안에 확인합니다. 누르지 않으면 아무것도 바뀌지 않습니다.
4. `/data` 가 지워지고 잠금이 해제됩니다. 첫 부팅은 공장 초기화 상태입니다.

## 2. 수정한 버전 빌드와 설치

```bash
# 소스: 이 저장소 전체 (GPL-3.0 대응 소스)
meson setup builddir-riscv --cross-file board/bpi-f3/cross-riscv64.ini -Dboard=bpi-f3
ninja -C builddir-riscv
# rootfs 준비는 docs/DEPLOYMENT_GUIDE.md §4

# 자신의 서명 키 (잠금 해제 기기는 검증하지 않지만, 이미지 무결성 확인용으로 권장)
tools/gen-boot-keys.sh ~/zyl-keys

# 슬롯 A 용 서명 이미지 — 커널 인자(슬롯의 파티션, dm-verity 루트 해시, zyl.slot)가
# 서명되는 DTB 안에 들어간다
ZYL_KERNEL=vmlinuz ZYL_DTB=bpi-f3-zyl.dtb ZYL_INITRAMFS=initramfs.cpio.gz \
  tools/sign-image.sh --slot a --key-dir ~/zyl-keys rootfs.img out/
```

기록 (eMMC 기준, 파티션은 `board/bpi-f3/README.md`):

| 파일 | 대상 |
|------|------|
| `out/fit-image-a.itb` | 부트 파티션 p1 (FAT) 의 `fit-image-a.itb` |
| `rootfs.img` | `/dev/mmcblk0p2` |
| `out/verity-hashtree-a.img` | `/dev/mmcblk0p3` |

슬롯 B 는 `--slot b` 로 만들어 p4·p5 에 씁니다. 다른 방법으로 기록할 필요가 없도록
OTA 업데이터도 같은 파일 이름과 슬롯 변수를 씁니다.

## 3. 다시 잠그기

다시 잠그면 서명 검증이 돌아오므로 **먼저 두 슬롯 모두에 공식 서명 이미지를** 설치해야
합니다. 소유자 이미지가 남아 있으면 그 슬롯은 부팅되지 않습니다.

1. 설정 > 개발자 옵션 > **다시 잠그기 요청** → **요청하기**
2. 다시 시작해 버튼으로 확인 — `/data` 가 지워지고 검증 부팅이 다시 켜집니다.

## 제품 출하용 부트로더

이 보장은 부트로더가 부트 정책을 내장하고 바깥에서 바꿀 수 없어야 성립합니다. 하나라도
빠지면 환경 저장소를 쓸 수 있는 누구나 `bootcmd` 를 바꿔 검증을 끌 수 있습니다.
`tools/build-bootloader.sh` 가 고정한 제조사 U-Boot·OpenSBI 에 `board/bpi-f3/bootloader`
(패치·설정 조각)를 얹어 그렇게 빌드하고, 결과를 스스로 검증합니다.

| 설정 | 이유 |
|------|------|
| `CONFIG_(SPL_)FIT_SIGNATURE` + `(SPL_)RSA`, 공개키를 DTB 에 `required = "conf"` 로 | SPL 이 OpenSBI·U-Boot 를, U-Boot 가 OS 를 검증 (제조사 설정은 RSA 가 빠져 있어 아무것도 검증하지 않았다) |
| `CONFIG_USE_DEFAULT_ENV_FILE` = `board/bpi-f3/u-boot-env.txt` | 부트 로직은 바이너리에 고정 |
| `CONFIG_ENV_WRITEABLE_LIST` — 그 파일의 "Runtime state" 변수만 | 저장소에서는 상태 변수만 읽는다 |
| `CONFIG_BOOTDELAY=-2`, fastboot 환경·설정 접근 없음, 레거시·Android 이미지 없음 | 셸도, USB 로 환경을 바꾸거나 서명 없는 이미지를 띄우는 길도 없다 |
| `CONFIG_ZYL_OS` (패치) | 제조사의 bootfs/네트워크 환경 가져오기·SD 자동 플래싱·요청 셸을 끔, 칩 ID 로 기기 ID |
| `CONFIG_CMD_BUTTON` + DT 에 `zyl_unlock_button` 라벨의 버튼 | 기기 앞의 확인 |
| `CONFIG_CMD_PART`, `CONFIG_CMD_MMC`(erase, wp) | 데이터 삭제, 부팅 영역 쓰기 보호 |

잠금 해제는 OS 이미지의 검증만 끕니다. 부트로더(FSBL·OpenSBI·U-Boot)는 잠금과 무관하게
제조사 키로 검증되며, 잠금 해제한 소유자도 수정한 Zyl OS 를 설치·실행할 수 있으므로
GPLv3 §6 의 설치 정보 요건과 충돌하지 않습니다 (U-Boot 는 GPLv2, OpenSBI 는 BSD).

## eFuse — 루트 키 해시 굽기 (빌드 선택)

Boot ROM 은 eFuse 의 루트 공개키 해시(ROTPKH, bank 4)가 비어 있으면 FSBL 의 서명을 보지
않습니다. 그 상태에서는 기기를 손에 쥔 사람이 SD 카드나 USB(FEL)로 자기 FSBL 을 띄울 수
있습니다. 해시를 구우면 그때부터 Boot ROM 은 `zylos-rotpk` 로 서명된 FSBL 만 실행합니다.

```bash
tools/build-bootloader.sh --key-dir <keys> --burn-rotpk out/
```

- 이렇게 빌드한 U-Boot 만 굽습니다 (`zyl_rotpk_burn=1`). 기본 빌드에는 굽는 코드가 없습니다
- **잠긴 기기에서, eMMC boot0 의 FSBL 이 바로 그 빌드의 FSBL 일 때만** (바이트 단위 SHA-256
  비교) 굽고, bank 4 를 잠급니다. eFuse 가 이미 같은 값이면 아무것도 하지 않고, 다른 값이면
  거부합니다. 빌드는 그 FSBL 의 루트 키·`rotpk_hash`·두 서명을 미리 검증합니다
- **되돌릴 수 없습니다.** 루트 키(`zylos-rotpk.key`)를 잃으면 그 기기의 부트로더는 더 이상
  바꿀 수 없습니다
- **실기기에서 검증되지 않았습니다.** 제조사는 Boot ROM 이 기대하는 해시 형식을 문서화하지
  않았습니다. Zyl OS 는 제조사 도구가 FSBL 안에 넣는 `rotpk_hash`(RSA 모듈러스 256바이트의
  SHA-256)를 그대로 굽습니다. 형식이 다르면 그 보드는 어떤 FSBL 도 부팅하지 못합니다.
  **버려도 되는 보드로 먼저 확인하십시오.** 제조사 U-Boot 는 bank 8 미만 쓰기를 막아 두었고,
  Zyl 패치가 bank 4 에 한해 그 경로를 엽니다 (`board/bpi-f3/bootloader`)
