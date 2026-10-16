# generic-riscv64 — 일반 riscv64 (QEMU virt)

보드가 없을 때 **실기기와 같은 아키텍처(riscv64), 같은 HAL 백엔드(`linux`), 같은 부팅
체인**으로 OS 를 돌려 보는 보드이자 검증 부팅 레인의 보드입니다. `board/emulator` 는 호스트
아키텍처와 HAL `emulator` 백엔드로 커널을 직접 띄우기 때문에, 기기 코드가 riscv64 에서 처음
겪는 문제(교차 빌드, riscv64 rootfs, 실기기 HAL)와 부팅 체인을 보지 못합니다.

부팅은 기기와 같은 길입니다: OpenSBI → 부트 정책(`board/boot-policy.txt` + 이 보드의
`u-boot-env.txt`)을 가진 메인라인 U-Boot → 서명 FIT → initramfs 가 여는 dm-verity 루트 →
A/B 업데이트·롤백. 기기와 다른 점:

- QEMU `-machine virt` 가 디바이스 트리를 주므로 DTS 가 없습니다. 그 DTB 가 FIT 에 들어가고,
  OS 공개키를 넣은 같은 DTB 를 QEMU 가 U-Boot 에 넘깁니다 — 키가 부트로더에 빌드되지 않습니다.
- OpenSBI·U-Boot 는 디스크가 아니라 QEMU 가 펌웨어로 싣고, 검증되지 않습니다.
- U-Boot 환경은 부트 파티션의 파일입니다 (자리·크기는 `partitions.conf` 의 `ZYL_ENV_*`).
- 버튼이 없어 잠금 전환은 확인되지 않고, 아무것도 지워지지 않습니다. 굽거나 쓰기 금지할
  부팅 영역도 없습니다.

## 쓰는 법

`--cross-file tools/cross/riscv64.ini -Dboard=generic-riscv64 -Dprofile=<프로파일>` 로
설정하고(meson 은 교차 파일을 보드에서 끌어오지 않습니다), 빌드·이미지 조립(`--with-kernel`)은
[DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md) §2–3, 부트로더 빌드와 검증 부팅은
[§4 검증 부팅 레인](../../docs/DEPLOYMENT_GUIDE.md#검증-부팅-레인-testsverified_bootsh)을
따릅니다. 그대로 도는 명령 순서와 패키지 목록은 `.github/workflows/riscv64.yml` 의
`qemu-boot-riscv64` 잡입니다.

이 보드에서 더 챙길 것:

- 컨테이너에서 조립한다면 binfmt 를 등록할 권한이 있어야 하고(같은 잡이 privileged 로 도는
  이유), 등록이 됐는지는 riscv64 바이너리가 실제로 실행되는지로 확인합니다.
- 에뮬레이션이라 느립니다. `run-qemu.sh --timeout` 을 넉넉히 주십시오.

레인이 보지 못하는 것([DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md#레인이-보지-않는-것))은
[board/bpi-f3](../bpi-f3/README.md) 와 실기기의 일입니다.

## 계약 파일

- `meson.build` — 보드 계약 (설명: `board/meson.build`)
- `partitions.conf` — 디스크 레이아웃, U-Boot 환경의 자리, 커널 콘솔. 실기기와 같은 파티션
  번호이고 노드·크기·환경의 자리가 다릅니다(부트 파티션은 두 슬롯의 FIT 가 들어갈 만큼)
- `u-boot-env.txt` — 부트 정책의 이 보드 몫: 부트 장치·파티션(virtio), 확인되지 않는 잠금 전환
- `bootloader/uboot.config` — 메인라인 U-Boot(`qemu-riscv64_smode`) 위의 설정 조각
  (`tools/build-bootloader.sh --board generic-riscv64`)
- `fit-image.its` — OS FIT 의 모양 (`tools/sign-image.sh --board generic-riscv64`)
- `tools/cross/riscv64.ini` — 교차 툴체인 (`board_cross_file`)
