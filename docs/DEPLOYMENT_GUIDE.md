# Zyl OS 배포 가이드

소스에서 동작하는 기기까지 가는 길입니다. 여기에는 순서와 지켜야 할 규칙만 적습니다.
목록·표·크기는 그 사실을 가진 파일을 가리킵니다 ([CLAUDE.md](../CLAUDE.md) §8-1).

## 원칙

- **이미지 한 장 = 보드 × 프로파일.** `meson setup` 한 번이 둘을 고르고, 그 빌드
  디렉터리에서 나오는 이미지는 그 조합 하나입니다 (`zyl-os-<보드>-<프로파일>.img`).
- **손으로 파일을 복사하는 절차는 없습니다.** 이미지에 들어갈 파일은 `ninja install` 이
  정합니다.
- **기기에는 셸이 없습니다.** 이미지 빌드가 로그인 콘솔(getty)을 막습니다. 진단은
  직렬 콘솔 출력과 자기점검(`zyl-selftest`)으로 합니다.
- **보드 없이 확인할 수 있는 것은 보드 없이 확인합니다.** QEMU 레인이 매 커밋 이미지
  조립과 부팅을 돌리고, 실기기에서만 볼 수 있는 것은 따로 적어 둡니다.

## 1. 준비

리눅스(또는 리눅스 컨테이너)가 필요합니다. 이미지 조립은 root 로 돕니다 (debootstrap).
호스트 도구는 `tools/setup-toolchain.sh` 가 설치하고, 정확한 패키지 목록은 CI 잡의
설치 단계가 기준입니다 (`.github/workflows/image.yml`, `riscv64.yml`, `bootloader.yml`).
맥에서는 `tools/ci-local.sh qemu-boot`(또는 `qemu-boot-riscv64`)로 같은 레인을
컨테이너에서 돌릴 수 있습니다 ([CONTRIBUTING.md](../CONTRIBUTING.md#로컬-ci--푸시-전-관문)).

## 2. 설정과 빌드

```bash
meson setup <builddir> --prefix=/usr -Dboard=<보드> -Dprofile=<프로파일> \
      [--cross-file tools/cross/<arch>.ini] [-Dota_public_key=<공개키.pem>]
ninja -C <builddir>
```

- 고를 수 있는 보드·프로파일은 `meson_options.txt`, 보드가 받는 프로파일은
  `board/<id>/meson.build` 의 `board_form_factors` 에 있습니다. 보드가 받지 않는
  조합은 `meson setup` 에서 실패합니다. 보드 계약 전체는 `board/meson.build`.
- `--prefix=/usr` 가 아니면 이미지 조립이 거부합니다 (유닛의 `ExecStart` 가 `/usr/bin`).
- **교차 빌드**: meson 은 교차 파일을 보드에서 끌어오지 않습니다. 보드의
  `board_arch` 가 호스트와 다르면 그 보드의 `board_cross_file` 을 `--cross-file` 로
  직접 넘기십시오. `:riscv64` 개발 라이브러리(multiarch)는 호스트 개발 패키지와 섞으면
  충돌하므로 전용 컨테이너에 깝니다 — 목록은 `riscv64.yml` 의 `qemu-boot-riscv64` 잡.
- `-Dota_public_key` 는 OTA 를 받을 기기에만 필요합니다 (§6). 경로는 소스 루트 기준
  또는 절대 경로입니다.

## 3. 이미지 조립 — `tools/build-image.sh`

```bash
sudo tools/build-image.sh --builddir <builddir> --out <outdir> [--with-kernel]
```

하는 일, 순서대로:

1. `ninja install` 을 staging 디렉터리로
2. 최소 rootfs 부트스트랩 + `system/packages.list` (`--suite`·`--mirror` 로 배포판을
   바꿀 수 있음). 다른 아키텍처면 2단계 부트스트랩이라 `qemu-user-static` 과 binfmt
   등록이 필요합니다
3. 링크 검사 — 설치한 `zyl-*` 바이너리가 요구하는 공유 라이브러리가 rootfs 에 없으면
   실패합니다. 그 라이브러리를 주는 패키지를 `system/packages.list` 에 더하십시오
4. 부팅 설정 — fstab(슬롯 A 루트, `/data`), 기본 타겟 → `zyl-os.target`, 서비스
   사용자·디렉터리, 로그인 콘솔 차단
5. rootfs ext4 → 보드 레이아웃 그대로 GPT 디스크를 만들고 슬롯 A 에 rootfs, `/data`
   포맷. 만들어진 파티션 순서가 선언과 다르면 실패합니다

`<outdir>` 에 남는 것:

| 파일 | 쓰임 |
|------|------|
| `zyl-os-<보드>-<프로파일>.img` | 디스크 이미지 |
| `rootfs-<프로파일>.img` | rootfs 단독 — 디스크 슬롯 A 와 같은 바이트. 서명(§5)과 OTA(§6)의 입력 |
| `vmlinuz`, `initrd.img` | `--with-kernel` 일 때만 |

`--rootfs-only` 는 rootfs 이미지까지만 만듭니다. 슬롯 B·verity·부트 파티션은 비어
있습니다 — 실기기에서 채우는 쪽은 서명 도구와 OTA 입니다.

**`--with-kernel`** 은 배포판 커널과 그 모듈을 rootfs 에 넣고 커널·initrd 를 꺼내 둡니다.
QEMU 레인에서 켭니다 — 게스트가 이미지 자신의 커널로 떠야 모듈이 맞아 DRM·입력이
올라옵니다. 실기기 이미지는 끕니다. 커널은 서명 FIT 으로 옵니다.

## 4. 보드 없이 부팅 — `tools/run-qemu.sh`

```bash
tools/run-qemu.sh --image <outdir>/zyl-os-<보드>-<프로파일>.img --builddir <builddir> --check
```

- 아키텍처는 `zyl-board.conf`, 루트 장치는 그 보드 `partitions.conf` 의 슬롯 A 입니다.
- 커널·initrd 는 `--kernel`/`--initrd` 로 준 것, 없으면 이미지 옆의 `vmlinuz`·`initrd.img`
  (`--with-kernel`), 그것도 없으면 호스트 `/boot` 의 최신 것을 씁니다.
- `--check` 는 커널 인자 `systemd.wants=zyl-selftest.service` 로 자기점검을 켜고,
  결과(PASS/FAILED)를 종료 코드로 돌려줍니다. `--timeout`·`--mem` 으로 조정합니다.

QEMU 레인과 그 명령 순서의 기준이 되는 워크플로:

| 보드 | 무엇을 보나 | 워크플로 |
|------|-------------|----------|
| [`emulator`](../board/emulator/README.md) | 호스트 아키텍처, HAL `emulator` 백엔드 | `image.yml` (`qemu-boot`) |
| [`generic-riscv64`](../board/generic-riscv64/README.md) | 실기기와 같은 아키텍처·HAL `linux` 백엔드 | `riscv64.yml` (`qemu-boot-riscv64`) |

### 자기점검 (`tools/zyl-selftest`)

기대 목록을 스스로 갖지 않고 설치된 유닛에서 끌어냅니다. 프로파일마다 서비스 집합이
달라도 같은 점검이 맞습니다.

- 설치된 `zyl-*.service` 가 활성인가 (하드웨어 조건이 맞지 않아 건너뛴 유닛은 실패가 아님)
- 그 유닛의 `BusName=` 이 시스템 버스에 있는가
- 앱 디렉터리·WAM 브리지·`/data` 마운트·`/etc/zyl-release`
- 화면 체인: DRM 장치 → Wayland 소켓 → WAM 이 띄운 앱 (DRM 이 없으면 건너뜀)

**보지 않는 것**: U-Boot·서명 FIT·dm-verity·A/B 전환 — 이것은
`tools/build-bootloader.sh` 의 자체 검증과 `tests/test_boot_env.sh`·`test_sign_image.sh`·
`test_build_ota.sh`·`test_updater.c` 의 몫입니다. SoC 고유 경로(GPU·모뎀·ISP·eFuse)는
실기기에서만 확인됩니다.

기기에는 셸이 없으므로 같은 커널 인자 `systemd.wants=zyl-selftest.service` 로 돌립니다
(공장 검사). 결과는 직렬 콘솔에 남습니다.

## 5. 실기기 — 키, 부트로더, 서명, 기록

서명 부팅 도구(`tools/gen-boot-keys.sh`, `tools/build-bootloader.sh`,
`tools/sign-image.sh`)는 BPI-F3 의 U-Boot 체인을 대상으로 합니다. 체인이 무엇을
검증하는지는 [SECURITY.md](SECURITY.md#9-검증-부팅-verified-boot) 에 있습니다.

### 운영자가 가져야 할 키

| 키 | 쓰임 | 만드는 법 · 자세히 |
|----|------|--------------------|
| 부팅 체인 키 (`zylos-rotpk`, `zylos-spl-key`, `zylos-fw-key`, `zylos-boot-key`) | FSBL·OpenSBI·U-Boot·OS FIT 서명. `zylos-boot-key` 는 OTA 패키지도 서명 | `tools/gen-boot-keys.sh <키 디렉터리>` — 없는 키만 만들고 있는 키는 그대로 둔다. [SECURITY.md](SECURITY.md) |
| 루트 키 `zylos-rotpk` | eFuse 에 해시를 구우면 그 기기와 평생 묶임 | [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) |
| 스토어 키 | 카탈로그·앱 패키지·개발자 인증서 서명. 공개키와 주소는 `system/store.json` | `tools/build-store.py`, [APPSTORE_API.md](APPSTORE_API.md) |

비밀키(`*.key`)는 저장소에 넣지 않습니다 (`gen-boot-keys.sh` 가 `.gitignore` 를 둡니다).
오프라인에 두고 도구에는 `--key-dir` 로 넘기십시오. 소유자가 자기 키로 같은 절차를
밟는 방법(GPLv3 §6)은 [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) 에 있습니다.

### 부트로더

```bash
tools/build-bootloader.sh --key-dir <키 디렉터리> <출력 디렉터리>
```

고정한 제조사 U-Boot·OpenSBI 위에 `board/bpi-f3/bootloader`(패치·설정 조각)를 얹고,
부트 정책 `board/bpi-f3/u-boot-env.txt` 를 기본 환경으로 내장해 빌드·서명합니다. 결과는
스스로 검증합니다. 부트로더가 무엇을 막는지와 eFuse 를 굽는 `--burn-rotpk` 는
[INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md#부트로더가-지켜야-하는-것).

### OS 이미지 서명

1. §2–3 으로 이미지를 만듭니다: `-Dboard=bpi-f3`, `--cross-file tools/cross/riscv64.ini`,
   `-Dota_public_key=<키 디렉터리>/zylos-boot-key.pub`, `--with-kernel` 없이.
2. 커널·DTB·initramfs 는 이 저장소가 만들지 않습니다. 보드 커널 빌드에서 가져오되:
   - 커널은 gzip 압축 (`board/bpi-f3/fit-image.its`), dm-verity 와 `dm-mod.create=`
     (CONFIG_DM_INIT)를 내장 — 서명된 커널 인자가 그것으로 루트를 세웁니다
   - rootfs 에는 그 커널의 모듈이 들어가지 않으므로 필요한 드라이버는 커널에 내장합니다
   - DTB 는 보드 전체 DTB 에 `system/dts/bpi-f3-zyl.dts` 오버레이를 적용한 것
3. 슬롯 A 서명:

   ```bash
   ZYL_KERNEL=<vmlinuz> ZYL_DTB=<보드.dtb> ZYL_INITRAMFS=<initramfs.cpio.gz> \
     tools/sign-image.sh --slot a --key-dir <키 디렉터리> <outdir>/rootfs-<프로파일>.img <서명 출력>
   ```

   rootfs 의 verity 트리를 만들고, 그 루트 해시·슬롯의 파티션·`zyl.slot` 을 담은 커널
   인자를 DTB `/chosen` 에 넣은 뒤 FIT 로 묶어 서명합니다. 커널 인자는 환경이 아니라
   서명되는 DTB 안에 있으므로 바꿀 수 없습니다.
   - 서명할 rootfs 는 **디스크 이미지 슬롯 A 에 들어간 바로 그 파일**이어야 합니다 —
     루트 해시가 그 바이트에 묶입니다.
   - `fit_check_sign` 이 없으면 서명 확인을 건너뛰고 경고만 냅니다. 있는 환경에서
     서명하십시오.
   - 출력의 `u-boot.dtb` 는 확인용입니다. 기기의 U-Boot DTB 에는 `build-bootloader.sh`
     가 같은 공개키를 넣습니다.
4. 기록: [board/bpi-f3/README.md](../board/bpi-f3/README.md).

## 6. OTA

```bash
ZYL_KERNEL=… ZYL_DTB=… ZYL_INITRAMFS=… \
  tools/build-ota.sh --key-dir <키 디렉터리> --url https://<호스트>/zylos-<버전>.pkg \
      <버전> <outdir>/rootfs-<프로파일>.img <출력 디렉터리>
```

두 슬롯용 verity 트리와 서명 FIT 을 `sign-image.sh` 로 만들어 rootfs 와 함께 묶고,
패키지의 SHA-256 에 `zylos-boot-key` 로 RSA-SHA256 서명합니다. 출력은 패키지
`zylos-<버전>.pkg` 와 서버가 업데이트 확인에 돌려줄 응답 `zylos-<버전>.json` 입니다.
버전은 숫자와 점만, URL 은 https 만 받습니다. 요청 형식은
`runtime/services/updater/updater.c` 가 정합니다.

기기 쪽(`zyl-updater`, 설정 > 소프트웨어 업데이트)에 이미지가 갖춰야 할 것:

- 공개키 — `-Dota_public_key` 로 빌드하면 `/etc/zyl-os/ota-signing-key.pem` 으로 들어갑니다.
- 서버 주소 — `/etc/zyl-os/update-server` 한 줄, 없으면 업데이터의 기본 주소.
  rootfs 는 읽기 전용이므로 이미지 안에 있어야 합니다.

기기가 무엇을 확인하고 어떻게 설치·전환·롤백하는지는
[SECURITY.md](SECURITY.md#10-ota).

## 관련 문서

- [INSTALLATION_INFORMATION.md](INSTALLATION_INFORMATION.md) — 부트로더 잠금 해제, 수정한 빌드 설치, eFuse
- [SECURITY.md](SECURITY.md) — 부팅 체인과 OTA 검증
- [APPSTORE_API.md](APPSTORE_API.md) — 스토어 키·카탈로그·개발자 인증서
- [PUSH_SERVER.md](PUSH_SERVER.md) — 푸시 서버(`system/push.json` 의 주소) 운영
- `board/<id>/README.md` — 보드별 손 작업
- [CONTRIBUTING.md](../CONTRIBUTING.md) — 검증과 CI
