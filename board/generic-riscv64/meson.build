# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Board Descriptor
#
# 역할: 일반 riscv64 기기 (QEMU virt 포함) 보드 디스크립터
# 수행범위: arch/HAL 백엔드/크로스파일/지원 폼팩터 선언
# 의존방향: 없음 (순수 선언)
# SOLID: SRP — 이 보드의 하드웨어 사실만 선언, 빌드 로직 없음
# ──────────────────────────────────────────────────────────
#
# 보드가 없어도 실기기와 같은 아키텍처·HAL 백엔드를 돌려 보기 위한 보드다.
# QEMU `-machine virt` 가 디바이스 트리를 직접 주므로 DTS 가 없고, 디스크는
# virtio(/dev/vda)다. SoC 고유 경로(GPU·모뎀·eFuse)는 여기 없다 — 그것은
# board/bpi-f3 의 일이다.

board_id          = 'generic-riscv64'
board_name        = 'Generic RISC-V 64 (QEMU virt)'
board_soc         = 'riscv64 (generic)'
board_arch        = 'riscv64'
board_hal_backend = 'linux'
board_dts         = ''               # QEMU·펌웨어가 DT 를 준다
board_cross_file  = 'tools/cross/riscv64.ini'

# 워치 제외: 워치 전력 예산(수십 mW 아이들)을 일반 riscv64 SBC 급 부품으로
# 보장할 수 없다. bpi-f3 와 같은 이유다.
board_form_factors = ['phone', 'tablet', 'tv', 'auto', 'desktop', 'appliance', 'pda']
