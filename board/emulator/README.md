# Emulator / Development Board

실기기가 아닌 **에뮬레이션·개발·CI 전용** 타겟입니다.
호스트 아키텍처로 빌드하며 디바이스 트리와 크로스 컴파일이 없습니다.

## 왜 있는가

실기기 보드는 하드웨어 사실에 따라 지원 폼팩터를 제한합니다
(`bpi-f3`는 워치 전력 예산을 못 맞추고, `generic-x86_64`는 모뎀·CAN이 없습니다).
그래서 하드웨어가 정해지기 전에는 일부 폼팩터를 **빌드조차 할 수 없었습니다**.

이 보드는 그 제약이 없으므로 8개 폼팩터 전부를 빌드·테스트할 수 있습니다.
런타임(폼팩터 정책·포커스 내비게이션·앱 집합)을 하드웨어 선택과 분리해
검증하는 경로입니다.

```bash
# 워치 프로파일을 실기기 없이 빌드
meson setup builddir-watch -Dboard=emulator -Dprofile=watch
ninja -C builddir-watch

# 8개 프로파일 전부 configure 되는지 확인
for p in phone tablet tv auto watch desktop appliance pda; do
  meson setup "bd-$p" -Dboard=emulator -Dprofile=$p >/dev/null && echo "ok $p"
done
```

## 쓰지 말아야 할 때

**실기기에 플래시하지 마십시오.** `board_dts`가 비어 있어 디바이스 트리
오버레이가 설치되지 않고, HAL 백엔드가 `emulator`입니다. 실기기용 이미지는
해당 보드 디스크립터(`board/bpi-f3` 등)로 빌드해야 합니다.

실제 워치를 지원하려면 워치급 SoC의 보드 디스크립터를 추가하고
`board_form_factors`에 `watch`를 선언하면 됩니다. 런타임 쪽은 변경이 필요 없습니다.
