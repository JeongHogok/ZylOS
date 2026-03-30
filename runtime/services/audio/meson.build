gio = dependency('gio-2.0')
cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: true)
executable('zyl-audio', 'audio.c', dependencies: [gio, m_dep], install: true)
