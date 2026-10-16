# generic-x86_64 — 범용 x86_64

PC·VM·미니PC·산업용 x86 보드입니다. riscv64 가 아닌 아키텍처에서도 HAL `linux`
백엔드로 빌드되는지 보는 **이식성 기준선**이고, PC 급 하드웨어로 돌릴 프로파일을
빌드하는 자리입니다. 네이티브 빌드라 교차 파일이 없고, UEFI·ACPI 라 디바이스 트리가
없습니다.

## 쓰는 법

```bash
meson setup builddir-x86 --prefix=/usr -Dboard=generic-x86_64 -Dprofile=<프로파일>
ninja -C builddir-x86
```

받는 프로파일과 나머지를 빼는 이유(모뎀·배터리 PMIC·CAN·전력 예산)는
`board/generic-x86_64/meson.build` 에 있습니다. 받지 않는 조합은 `meson setup` 이
거부합니다.

## 이미지와 부팅

`tools/build-image.sh` 는 보드의 `partitions.conf` 를 요구하고, 서명 부팅 도구
(`tools/sign-image.sh`, `tools/build-bootloader.sh`)는 BPI-F3 의 U-Boot 체인 전용입니다.
이 보드로 설치 가능한 이미지를 내려면 `board/generic-x86_64/partitions.conf` 와 UEFI
부팅 경로(커널을 싣는 방법과 그 검증)를 먼저 더해야 합니다. 보드 계약 전체는
`board/meson.build`, 새 보드의 규칙은 [CLAUDE.md](../../CLAUDE.md) §11.
