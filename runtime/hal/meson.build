gio = dependency('gio-2.0')

hal_sources = files(
  'hal_boot.c',
  'hal_audio.c',
  'hal_vibrator.c',
  'hal_wifi.c',
  'hal_bt.c',
  'hal_display.c',
  'hal_battery.c',
  'hal_storage.c',
  'hal_cpu.c',
)

json_glib = dependency('json-glib-1.0')
cc = meson.get_compiler('c')
zyl_hal = static_library('zyl-hal', hal_sources,
  dependencies: [gio, json_glib, cc.find_library('m'), dependency('threads')],
  install: false,
)

# 에뮬레이터: gio 의존 없음 — Linux/macOS 모두 빌드 가능
threads_dep = dependency('threads')

zyl_hal_emu = static_library('zyl-hal-emu',
  files('hal_emulator.c'),
  include_directories: include_directories('.'),
  dependencies: [threads_dep],
  install: false,
)

# The backend the board contract names (board/<id>/meson.build: board_hal_backend).
# Services take hardware only through this — never nmcli/wpctl/sysfs themselves.
zyl_hal_dep = declare_dependency(
  link_with: board_hal_backend == 'emulator' ? zyl_hal_emu : zyl_hal,
  dependencies: board_hal_backend == 'emulator' ? [threads_dep] : [gio, json_glib, cc.find_library('m'), threads_dep],
  include_directories: include_directories('.'),
)
