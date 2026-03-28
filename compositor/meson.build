# compositor/meson.build — Zyl OS Wayland Compositor
# Layer: System / Compositor
# Role: Build the wlroots-based Wayland compositor
# Scope: Compile compositor sources, link wlroots + Wayland deps
# Dependency direction: depends on wlroots, wayland-server, xkbcommon, pixman
# SOLID: Single-responsibility — only compositor build logic

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

compositor_deps = [
  wlroots,
  wayland_server,
  xkbcommon,
  pixman,
]

compositor_sources = files(
  'src/main.c',
  'src/input/gesture.c',
  'src/output/output.c',
  'src/view/view.c',
)

compositor_inc = include_directories(
  'include',
  'src',
)

executable('zyl-compositor',
  compositor_sources,
  include_directories: compositor_inc,
  dependencies: compositor_deps,
  c_args: ['-DWLR_USE_UNSTABLE'],
  install: true,
)
