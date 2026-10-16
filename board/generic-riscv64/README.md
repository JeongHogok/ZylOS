# Generic RISC-V 64 (QEMU virt)

보드가 없을 때 **실기기와 같은 아키텍처(riscv64)와 같은 HAL 백엔드(`linux`)** 로
OS 를 돌려 보기 위한 보드입니다. `board/emulator` 는 호스트 아키텍처에서 HAL
에뮬레이터 백엔드로 돌기 때문에, 기기 코드가 riscv64 에서 처음 겪는 문제를
잡지 못합니다.

## 쓰는 법

교차 빌드 → 이미지 → QEMU 부팅 + 자기점검:

```bash
meson setup builddir-rv64 --prefix=/usr \
    --cross-file tools/cross/riscv64.ini \
    -Dboard=generic-riscv64 -Dprofile=phone
ninja -C builddir-rv64
tools/build-image.sh --builddir builddir-rv64 --out out/rv64 --with-kernel
tools/run-qemu.sh --image out/rv64/zyl-os-generic-riscv64-phone.img \
    --builddir builddir-rv64 --check
```

필요한 것: `crossbuild-essential-riscv64`, `:riscv64` 개발 라이브러리(multiarch),
`qemu-user-static` + binfmt(2단계 부트스트랩), `qemu-system-misc`.
정확한 패키지 목록은 CI 의 `qemu-boot-riscv64` 잡이 단일 출처입니다.

## 이 보드가 검증하는 것 / 못 하는 것

**검증**: riscv64 교차 빌드, riscv64 rootfs 조립, 실기기 HAL 백엔드(`linux`)
빌드·기동, 서비스·버스·화면 체인.

**검증 못 함**: SoC 고유 경로 — GPU(Imagination), 모뎀, ISP, eFuse, U-Boot 정책.
그것은 `board/bpi-f3` 과 실기기의 일입니다.
