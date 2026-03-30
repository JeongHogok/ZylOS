gio = dependency('gio-2.0')

hal_sources = files(
  'hal_wifi.c',
  'hal_bt.c',
  'hal_display.c',
  'hal_audio.c',
  'hal_battery.c',
  'hal_storage.c',
  'hal_linux.c',
)

zyl_hal = static_library('zyl-hal', hal_sources,
  dependencies: [gio],
  install: false,
)
