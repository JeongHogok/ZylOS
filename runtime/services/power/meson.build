gio = dependency('gio-2.0')

# hal_cpu.c is compiled directly into power service
# (HAL static library is for compositor/WAM, not daemon services)
executable('zyl-power',
  ['power.c', 'power_backlight.c', 'power_cpu.c', 'power_display.c', 'power_wakelock.c', '../../hal/hal_cpu.c'],
  dependencies: [gio],
  include_directories: include_directories('../../hal'),
  install: true,
)
