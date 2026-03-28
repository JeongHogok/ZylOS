project('bpi-compositor', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

# wlroots 의존성 (0.18+)
wlroots = dependency('wlroots-0.18', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: '>=0.18.0')
endif

wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')

# Wayland 스캐너
wayland_scanner = find_program('wayland-scanner')

deps = [
  wlroots,
  wayland_server,
  xkbcommon,
  pixman,
]

# wlroots에 필요한 정의
add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  language: 'c',
)

executable('bpi-compositor',
  'src/main.c',
  dependencies: deps,
  install: true,
)
