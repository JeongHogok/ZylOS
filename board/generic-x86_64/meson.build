# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Board Descriptor
#
# 역할: 범용 x86_64 보드 디스크립터 — PC/VM/미니PC/산업용 보드
# 수행범위: SoC/arch/HAL 백엔드/지원 폼팩터 선언 (크로스 컴파일 불필요)
# 의존방향: 없음 (순수 선언)
# SOLID: SRP — 이 보드의 하드웨어 사실만 선언, 빌드 로직 없음
# ──────────────────────────────────────────────────────────

board_id          = 'generic-x86_64'
board_name        = 'Generic x86_64'
board_soc         = 'x86_64 (generic)'
board_arch        = 'x86_64'
board_hal_backend = 'linux'
board_dts         = ''               # ACPI/UEFI — 디바이스 트리 없음
board_cross_file  = ''               # 네이티브 빌드

# 폰 제외: 셀룰러 모뎀/배터리 PMIC가 없다. 워치 제외: 전력 예산 불가.
# 자동차 제외: CAN 버스/차량 전원 제어가 없다.
board_form_factors = ['tv', 'desktop', 'appliance', 'pda', 'tablet']
