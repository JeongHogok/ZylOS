# emulator — 에뮬레이션·개발·CI 보드

실기기가 아닌 보드입니다. 호스트 아키텍처로 빌드하고 HAL 은 `emulator` 백엔드
(`runtime/hal/hal_emulator.c`)를 씁니다. 하드웨어 제약이 없으므로 모든 프로파일을
받습니다 — 폼팩터 정책·앱 집합·서비스 집합을 하드웨어 선택과 떼어 검증하는 자리입니다.
실기기 보드는 하드웨어가 감당하는 프로파일만 `board_form_factors` 에 선언하므로,
그 보드가 받지 않는 프로파일도 여기서는 빌드됩니다.

## 쓰는 곳

- 이미지 레인: 조립 → QEMU 부팅 → 자기점검 (`.github/workflows/image.yml` 의 `qemu-boot`)
- 프로파일마다 이미지가 성립하는지: `tests/test_image_profiles.sh`

## 쓰는 법

```bash
meson setup builddir --prefix=/usr -Dboard=emulator -Dprofile=<프로파일>
ninja -C builddir
sudo tools/build-image.sh --builddir builddir --out out/image --with-kernel
tools/run-qemu.sh --image out/image/zyl-os-emulator-<프로파일>.img --builddir builddir --check
```

호스트 아키텍처의 QEMU 가 필요합니다. 맥에서는 `tools/ci-local.sh qemu-boot` 가 같은
레인을 컨테이너에서 돌립니다. 자기점검이 보는 것과 보지 않는 것은
[DEPLOYMENT_GUIDE.md](../../docs/DEPLOYMENT_GUIDE.md) §4.

## 실기기에 쓰지 않는다

서비스가 하드웨어를 만지지 않는 HAL 백엔드이고, 디스크 노드는 virtio 이며, 부트로더와
서명 경로가 없습니다. 실기기 이미지는 그 기기의 보드로 만듭니다
([board/bpi-f3](../bpi-f3/README.md)).

## 계약 파일

- `meson.build` — 보드 계약 (설명: `board/meson.build`)
- `partitions.conf` — 디스크 레이아웃. 실기기와 같은 파티션 번호를 쓰고 디스크 노드와
  크기만 다릅니다 — 이미지 조립 코드가 보드마다 갈라지지 않게 하려는 것입니다
