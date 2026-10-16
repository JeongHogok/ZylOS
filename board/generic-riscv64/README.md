# generic-riscv64 — 일반 riscv64 (QEMU virt)

보드가 없을 때 **실기기와 같은 아키텍처(riscv64)와 같은 HAL 백엔드(`linux`)** 로 OS 를
돌려 보는 보드입니다. `board/emulator` 는 호스트 아키텍처와 HAL `emulator` 백엔드로 돌기
때문에, 기기 코드가 riscv64 에서 처음 겪는 문제(교차 빌드, riscv64 rootfs, 실기기 HAL)를
보지 못합니다. QEMU `-machine virt` 가 디바이스 트리를 주므로 DTS 가 없고, 디스크는
virtio 입니다.

## 쓰는 법

`--cross-file tools/cross/riscv64.ini -Dboard=generic-riscv64 -Dprofile=<프로파일>` 로
설정하고(meson 은 교차 파일을 보드에서 끌어오지 않습니다), 빌드·이미지 조립(`--with-kernel`)·
QEMU 부팅은 [DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md) §2–4 를 따릅니다. 그대로
도는 명령 순서와 패키지 목록은 `.github/workflows/riscv64.yml` 의 `qemu-boot-riscv64` 잡입니다.

이 보드에서 더 챙길 것:

- 컨테이너에서 조립한다면 binfmt 를 등록할 권한이 있어야 하고(같은 잡이 privileged 로 도는
  이유), 등록이 됐는지는 riscv64 바이너리가 실제로 실행되는지로 확인합니다.
- 에뮬레이션이라 느립니다. `run-qemu.sh --timeout` 을 넉넉히 주십시오.

레인이 보지 못하는 것([DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md#자기점검-toolszyl-selftest))은
[board/bpi-f3](../bpi-f3/README.md) 와 실기기의 일입니다.

## 계약 파일

- `meson.build` — 보드 계약 (설명: `board/meson.build`)
- `partitions.conf` — 디스크 레이아웃. 실기기와 같은 파티션 번호, 노드와 크기만 다름
- `tools/cross/riscv64.ini` — 교차 툴체인 (`board_cross_file`)
