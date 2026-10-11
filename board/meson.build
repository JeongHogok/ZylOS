# ──────────────────────────────────────────────────────────
# [Clean Architecture] Infrastructure Layer - Board Dispatch
#
# 역할: -Dboard=<id> → board/<id>/meson.build 디스패치 + 폼팩터 호환 검증
# 수행범위: 보드 디스크립터 로드, 보드가 선언하지 않은 프로파일 조합 거부
# 의존방향: board/<id>/meson.build (board_* 변수 제공)
# SOLID: DIP — 루트 빌드는 개별 보드가 아닌 board_* 계약에 의존
# ──────────────────────────────────────────────────────────

subdir(get_option('board'))

# 보드가 지원하지 않는 폼팩터 조합은 configure 단계에서 실패시킨다.
# (워치용 전력 예산이 없는 SoC에 -Dprofile=watch 같은 조합을 미리 차단)
if not board_form_factors.contains(zyl_profile)
  error('Board "' + board_id + '" does not support profile "' + zyl_profile
        + '" — supported: ' + ', '.join(board_form_factors))
endif

message('Zyl OS target: board=' + board_id + ' soc=' + board_soc
        + ' arch=' + board_arch + ' profile=' + zyl_profile
        + ' hal=' + board_hal_backend)
