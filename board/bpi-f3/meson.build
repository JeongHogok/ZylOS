# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Board Descriptor
#
# 역할: Banana Pi BPI-F3 (SpacemiT K1, RISC-V) 보드 디스크립터
# 수행범위: SoC/arch/HAL 백엔드/DTS/크로스파일/지원 폼팩터 선언
# 의존방향: 없음 (순수 선언)
# SOLID: SRP — 이 보드의 하드웨어 사실만 선언, 빌드 로직 없음
# ──────────────────────────────────────────────────────────

board_id          = 'bpi-f3'
board_name        = 'Banana Pi BPI-F3'
board_soc         = 'SpacemiT K1'
board_arch        = 'riscv64'
board_hal_backend = 'linux'
board_dts         = 'bpi-f3-zyl.dts'
board_cross_file  = 'tools/cross/riscv64.ini'

# 워치 제외: K1은 8코어 2GHz SBC로 워치 전력 예산(수십 mW 아이들)을 만족하지 못한다.
board_form_factors = ['phone', 'tablet', 'tv', 'auto', 'desktop', 'appliance', 'pda']
