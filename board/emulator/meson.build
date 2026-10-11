# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Board Descriptor
#
# 역할: 에뮬레이션/개발 전용 보드 — 호스트 아키텍처에서 모든 폼팩터 빌드
# 수행범위: arch/HAL 백엔드/지원 폼팩터 선언 (실기기 아님)
# 의존방향: 없음 (순수 선언)
# SOLID: SRP — 이 타겟의 사실만 선언, 빌드 로직 없음
# ──────────────────────────────────────────────────────────

board_id          = 'emulator'
board_name        = 'Emulator / development host'
board_soc         = 'host'
board_arch        = 'native'
board_hal_backend = 'emulator'
board_dts         = ''               # 가상 기기 — 디바이스 트리 없음
board_cross_file  = ''               # 호스트 네이티브 빌드

# 모든 폼팩터 지원. 실기기 보드와 달리 전력 예산·모뎀·CAN 같은 물리 제약이
# 없으므로 제외할 근거가 없다. 이 보드로 만든 이미지는 에뮬레이터와 VM 전용이며
# 실기기에 플래시하는 용도가 아니다.
board_form_factors = ['phone', 'tablet', 'tv', 'auto', 'watch',
                      'desktop', 'appliance', 'pda']
