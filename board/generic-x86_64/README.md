# Generic x86_64 Board Support

PC / 가상머신 / 미니PC / 산업용 x86 보드용 보드 디스크립터입니다.
BPI-F3 외의 SoC에서도 Zyl OS가 빌드·동작함을 보장하는 **이식성 기준선**이며,
동시에 TV·데스크톱·키오스크 빌드를 실제 하드웨어 없이 검증하는 경로입니다.

## 하드웨어 전제

| 항목 | 내용 |
|---|---|
| arch | `x86_64` (네이티브 빌드, 크로스 파일 불필요) |
| 부팅 | UEFI + ACPI — 디바이스 트리 없음 |
| 그래픽 | i915 / amdgpu / nouveau — DRM/KMS 표준 경로 |
| 지원 폼팩터 | `tv`, `desktop`, `appliance`, `pda`, `tablet` |

## 지원하지 않는 폼팩터와 이유

- **`phone`** — 셀룰러 모뎀과 배터리 PMIC가 없습니다.
- **`watch`** — 전력 예산을 만족할 수 없습니다.
- **`auto`** — CAN 버스와 차량 전원 제어(ACC/IGN)가 없습니다.

`board/generic-x86_64/meson.build` 의 `board_form_factors` 가 이를 선언하며,
지원하지 않는 조합은 `meson setup` 단계에서 실패합니다.

## 빌드

```bash
# TV 빌드
meson setup builddir-tv -Dboard=generic-x86_64 -Dprofile=tv
ninja -C builddir-tv

# 데스크톱 빌드
meson setup builddir-desktop -Dboard=generic-x86_64 -Dprofile=desktop
ninja -C builddir-desktop
```

## BPI-F3와의 차이

DTS가 없으므로 `system/dts/` 오버레이를 설치하지 않습니다.
Verified Boot는 U-Boot FIT 대신 UEFI Secure Boot + shim 경로를 써야 하며,
`tools/sign-image.sh` 의 dm-verity 부분은 그대로 재사용 가능합니다.
