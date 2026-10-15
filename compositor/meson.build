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

# wlroots' xdg-shell headers include the protocol's server header: generated
# here from wayland-protocols, not expected in the system include path
wayland_scanner = find_program('wayland-scanner')
xdg_shell_protocol_h = custom_target('xdg-shell-protocol.h',
  input: wayland_protos.get_variable('pkgdatadir') / 'stable/xdg-shell/xdg-shell.xml',
  output: 'xdg-shell-protocol.h',
  command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
)

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
  compositor_sources, xdg_shell_protocol_h,
  include_directories: compositor_inc,
  dependencies: compositor_deps,
  c_args: ['-DWLR_USE_UNSTABLE', '-D_POSIX_C_SOURCE=200809L', '-Wno-error'],
  install: true,
)
