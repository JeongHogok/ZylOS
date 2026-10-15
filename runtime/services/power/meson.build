gio = dependency('gio-2.0')
executable('zyl-power',
  ['power.c', 'power_cpu.c', 'power_display.c', 'power_wakelock.c'],
  dependencies: [gio, zyl_hal_dep],
  install: true,
)
