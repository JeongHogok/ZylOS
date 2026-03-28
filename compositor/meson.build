project('bpi-compositor', 'c',
  version: '0.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
  ],
)

# wlroots dependency (0.18+)
wlroots = dependency('wlroots-0.18', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: '>=0.18.0')
endif

wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')

# Wayland scanner
wayland_scanner = find_program('wayland-scanner')

deps = [
  wlroots,
  wayland_server,
  xkbcommon,
  pixman,
]

# wlroots requires this define
add_project_arguments(
  '-DWLR_USE_UNSTABLE',
  language: 'c',
)

bpi_compositor_sources = files(
  'src/main.c',
  'src/input/gesture.c',
  'src/output/output.c',
  'src/view/view.c',
)

bpi_compositor_inc = include_directories(
  'include',
  'src',
)

executable('bpi-compositor',
  bpi_compositor_sources,
  include_directories: bpi_compositor_inc,
  dependencies: deps,
  install: true,
)
