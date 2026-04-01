gio = dependency('gio-2.0')

hal_sources = files(
  'hal_wifi.c',
  'hal_bt.c',
  'hal_display.c',
  'hal_audio.c',
  'hal_battery.c',
  'hal_storage.c',
  'hal_cpu.c',
  'hal_linux.c',
)

zyl_hal = static_library('zyl-hal', hal_sources,
  dependencies: [gio],
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
