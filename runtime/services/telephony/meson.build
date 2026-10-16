gio = dependency('gio-2.0')

executable('zyl-telephony',
  'telephony.c',
  'cbs.c',
  'calllog.c',
  dependencies: [gio, zyl_settings_follow_dep],
  install: true,
)
