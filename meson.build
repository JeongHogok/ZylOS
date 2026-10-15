project('zyl-os', 'c',
  version: '0.1.0',
  # gnu11, not c11: with strict ISO mode glibc hides POSIX declarations
  # (mkstemp, lstat, symlink, strdup...) behind feature macros, which this
  # tree had been patching file by file with #define _GNU_SOURCE.
  default_options: ['c_std=gnu11', 'warning_level=2'],
)

# ═══════════════════════════════════════════════════════════
# Target selection — board (SoC) × form-factor profile
#   meson setup builddir -Dboard=bpi-f3 -Dprofile=tv
# ═══════════════════════════════════════════════════════════
zyl_profile = get_option('profile')

# ─── OS image identity — apps/system/release.js is the only source ───
_python = find_program('python3')
_release_version = run_command(_python, files('tools/release_info.py'), 'version',
                               check: true).stdout().strip()
zyl_security_patch = run_command(_python, files('tools/release_info.py'), 'securityPatch',
                                 check: true).stdout().strip()
if _release_version != meson.project_version()
  error('apps/system/release.js says version ' + _release_version +
        ' but meson.build says ' + meson.project_version() + ' — keep them equal')
endif
add_project_arguments('-DZYL_OS_VERSION="' + meson.project_version() + '"',
                      '-DZYL_SECURITY_PATCH="' + zyl_security_patch + '"',
                      language: 'c')

subdir('board')   # defines board_id / board_soc / board_arch / board_hal_backend
                  # and validates that this board supports zyl_profile

# ─── Form-factor profile → C service set ───────────────────
# Every profile needs these: display, input, power, security, logging, OTA.
zyl_core_services = [
  'logger', 'display', 'input', 'power', 'crash',
  'credential', 'user', 'accessibility', 'notification', 'updater', 'telemetry',
  'netstats', 'bootctl', 'settings', 'lock', 'device', 'storage', 'fs', 'clipboard', 'network', 'appdata',
]

# Hardware-dependent services, added per form factor.
# shortcut: cellular tablets and LTE watches must use -Dprofile=phone for now;
#           split into tablet-lte / watch-lte variants when a board needs it.
zyl_profile_extras = {
  'phone':     ['account', 'alarm', 'appstore', 'audio', 'auth', 'bluetooth',
                'camera', 'contacts', 'location', 'messaging', 'nfc', 'sensors',
                'telephony', 'usb', 'wifi'],
  'tablet':    ['account', 'alarm', 'appstore', 'audio', 'auth', 'bluetooth',
                'camera', 'contacts', 'location', 'messaging', 'sensors', 'usb',
                'wifi'],
  'tv':        ['account', 'alarm', 'appstore', 'audio', 'auth', 'bluetooth',
                'usb', 'wifi'],
  'auto':      ['account', 'alarm', 'audio', 'auth', 'bluetooth', 'camera',
                'contacts', 'location', 'messaging', 'sensors', 'telephony', 'usb',
                'wifi'],
  'watch':     ['account', 'alarm', 'audio', 'auth', 'bluetooth', 'contacts',
                'location', 'messaging', 'nfc', 'sensors', 'wifi'],
  'desktop':   ['account', 'alarm', 'appstore', 'audio', 'auth', 'bluetooth',
                'camera', 'nfc', 'usb', 'wifi'],
  'appliance': ['alarm', 'audio', 'wifi'],
  'pda':       ['account', 'alarm', 'appstore', 'audio', 'auth', 'bluetooth',
                'camera', 'contacts', 'location', 'messaging', 'nfc', 'usb', 'wifi'],
}

zyl_services = zyl_core_services + zyl_profile_extras[zyl_profile]

# Unit basename differs from the service directory name.
zyl_unit_overrides = {'crash': 'zyl-crash-handler'}

# Subprojects
subdir('compositor')

# WAM requires WebKitGTK (6.0 or 4.1 or 4.0); skip if unavailable
_wam_webkit = dependency('webkitgtk-6.0', required: false)
if not _wam_webkit.found()
  _wam_webkit = dependency('webkit2gtk-4.1', required: false)
endif
if not _wam_webkit.found()
  _wam_webkit = dependency('webkit2gtk-4.0', required: false)
endif
subdir('runtime/hal')   # before the WAM and services: both take hardware facts from it
if _wam_webkit.found()
  subdir('runtime/wam')
else
  warning('WebKitGTK not found — skipping WAM build')
endif
foreach svc : zyl_services
  subdir('runtime/services/' + svc)
endforeach

# Install app files
install_subdir('apps/home',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/lockscreen', install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/statusbar',  install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/settings',   install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/browser',    install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/files',      install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/camera',     install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/keyboard',   install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/calc',        install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/clock',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/contacts',    install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/gallery',     install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/messages',    install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/music',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/notes',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/oobe',        install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/phone',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/store',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/weather',     install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/alerts',      install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/shared',      install_dir: get_option('datadir') / 'zyl-os' / 'apps')

# Install HAL header
install_headers('runtime/hal/hal.h', subdir: 'zyl-os')

# ─── systemd units — only those the profile actually builds ───
zyl_units = ['zyl-compositor', 'zyl-wam', 'zyl-apparmor']
foreach svc : zyl_services
  zyl_units += zyl_unit_overrides.get(svc, 'zyl-' + svc)
endforeach

_systemd_dir = get_option('prefix') / 'lib' / 'systemd' / 'system'

foreach unit : zyl_units
  install_data('system/' + unit + '.service', install_dir: _systemd_dir)
endforeach

# zyl-os.target is generated so Wants= can never drift from the built unit set:
# a watch build that installs 15 units must not pull in 22.
_target_wants = ['systemd-timesyncd.service']
foreach unit : zyl_units
  if unit != 'zyl-compositor' and unit != 'zyl-wam'
    _target_wants += unit + '.service'
  endif
endforeach

configure_file(
  input: 'system/zyl-os.target.in',
  output: 'zyl-os.target',
  configuration: configuration_data({
    'ZYL_PROFILE': zyl_profile,
    'ZYL_BOARD':   board_id,
    'ZYL_WANTS':   ' '.join(_target_wants),
  }),
  install: true,
  install_dir: _systemd_dir,
)

install_data('system/timesyncd.conf', install_dir: get_option('sysconfdir') / 'systemd')
# Settings defaults — read by zyl-settings on the device and built into the emulator
install_data('system/settings-defaults.json', install_dir: get_option('datadir') / 'zyl-os')
# Which store the app store uses and the key its catalog and packages are signed
# with (empty: no store) — read by zyl-appstore and built into the emulator
install_data('system/store.json', install_dir: get_option('sysconfdir') / 'zyl-os')
# Service users and the /data directories units write to. Without them the
# WAM (User=zyl-wam) and every ProtectSystem=strict unit with a /data path
# fail before they start.
install_data('system/sysusers.d/zyl-wam.conf',
             install_dir: get_option('prefix') / 'lib' / 'sysusers.d')
install_data('system/tmpfiles.d/zyl-wam.conf', 'system/tmpfiles.d/zyl-data.conf',
             install_dir: get_option('prefix') / 'lib' / 'tmpfiles.d')

configure_file(
  input: 'system/zyl-release.in',
  output: 'zyl-release',
  configuration: configuration_data({
    'ZYL_OS_VERSION':     meson.project_version(),
    'ZYL_SECURITY_PATCH': zyl_security_patch,
    'ZYL_BOARD':          board_id,
    'ZYL_PROFILE':        zyl_profile,
  }),
  install: true,
  install_dir: get_option('sysconfdir'),
)

# Native font defaults + per-language CJK glyph selection (web apps use
# apps/shared/fonts.css). Requires the Noto packages listed in DEPLOYMENT_GUIDE.
install_data('system/fonts/50-zyl-fonts.conf',
             install_dir: get_option('sysconfdir') / 'fonts' / 'conf.d')

# Install recovery scripts
install_data('system/recovery/recovery.sh',       install_dir: get_option('datadir') / 'zyl-os' / 'recovery', install_mode: 'rwxr-xr-x')
install_data('system/recovery/factory-reset.sh',  install_dir: get_option('datadir') / 'zyl-os' / 'recovery', install_mode: 'rwxr-xr-x')
install_data('system/recovery/recovery.service',  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')

# AppArmor profiles. zyl-apparmor.service loads every /etc/apparmor.d/zyl-* and
# stops the boot on a parser error, so a profile whose binary this profile does
# not build must not be installed.
_apparmor = ['system/apparmor/zyl-wam',
             'system/apparmor/zyl-compositor',
             'system/apparmor/zyl-app-default']
foreach svc : ['audio', 'bluetooth', 'camera', 'credential', 'telephony']
  if zyl_services.contains(svc)
    _apparmor += 'system/apparmor/zyl-' + svc
  endif
endforeach
install_data(_apparmor, install_dir: get_option('sysconfdir') / 'apparmor.d')

# Tests
subdir('tests')
