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

## 제품 출하용 U-Boot 요건

이 보장은 부트로더가 다음과 같이 빌드되어야 성립합니다. 하나라도 빠지면 환경 저장소를
쓸 수 있는 누구나 `bootcmd` 를 바꿔 검증을 끌 수 있습니다.

| 설정 | 이유 |
|------|------|
| `CONFIG_FIT_SIGNATURE`, 공개키를 제어 DTB 에 `required = "conf"` 로 | 잠긴 기기의 서명 강제 (`tools/sign-image.sh` 가 키를 `u-boot.dtb` 에 기록) |
| `CONFIG_USE_DEFAULT_ENV_FILE` = `board/bpi-f3/u-boot-env.txt` | 부트 로직은 바이너리에 고정 |
| `CONFIG_ENV_WRITEABLE_LIST` 에 `zyl_active_slot zyl_next_slot zyl_boot_count zyl_unlocked zyl_lock_seen zyl_oem_unlock_allowed` 만 | 저장소에서는 상태 변수만 읽는다 |
| `CONFIG_CMD_BUTTON` + DT 에 `zyl_unlock_button` 라벨의 버튼 | 기기 앞의 확인 |
| `CONFIG_CMD_PART`, `CONFIG_CMD_MMC`(erase) | 데이터 삭제 |

부트 ROM → SPL → U-Boot 의 서명 체인(SoC 의 보안 부팅 퓨즈)은 보드 공급사 절차를 따르며,
GPLv3 의 대상인 Zyl OS 이미지 설치와는 별개입니다.
