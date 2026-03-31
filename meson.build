project('zyl-os', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

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
if _wam_webkit.found()
  subdir('runtime/wam')
else
  warning('WebKitGTK not found — skipping WAM build')
endif
subdir('runtime/services/notification')
subdir('runtime/services/appstore')
subdir('runtime/services/updater')
subdir('runtime/services/power')
subdir('runtime/services/sandbox')
subdir('runtime/services/sensors')
subdir('runtime/services/location')
subdir('runtime/services/telephony')
subdir('runtime/services/display')
subdir('runtime/services/dbus')
subdir('runtime/services/usb')
subdir('runtime/services/input')
subdir('runtime/services/user')
subdir('runtime/services/credential')
subdir('runtime/services/accessibility')
subdir('runtime/services/logger')
subdir('runtime/services/camera')
subdir('runtime/services/audio')
subdir('runtime/services/bluetooth')
subdir('runtime/services/wifi')
subdir('runtime/services/account')
subdir('runtime/hal')
subdir('runtime/services/crash')
subdir('runtime/services/telemetry')

# Install app files
install_subdir('apps/home',       install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/lockscreen', install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/statusbar',  install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/settings',   install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/browser',    install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/files',      install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/terminal',   install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/camera',     install_dir: get_option('datadir') / 'zyl-os' / 'apps')
install_subdir('apps/shared',     install_dir: get_option('datadir') / 'zyl-os' / 'apps')

# Install HAL header
install_headers('runtime/hal/hal.h', subdir: 'zyl-os')

# Install systemd service files
install_data('system/zyl-compositor.service', install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-wam.service',        install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-notification.service', install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-power.service',     install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-sensors.service',   install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-location.service',  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-telephony.service', install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-display.service',  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-usb.service',      install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-input.service',   install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-user.service',       install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-credential.service',    install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-accessibility.service', install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-logger.service',        install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-camera.service',        install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-audio.service',         install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-bluetooth.service',     install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-wifi.service',          install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-account.service',       install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-os.target',             install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/zyl-apparmor.service',      install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
install_data('system/timesyncd.conf',            install_dir: get_option('sysconfdir') / 'systemd')
install_data('system/zyl-crash-handler.service', install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')

# Install recovery scripts
install_data('system/recovery/recovery.sh',       install_dir: get_option('datadir') / 'zyl-os' / 'recovery', install_mode: 'rwxr-xr-x')
install_data('system/recovery/factory-reset.sh',  install_dir: get_option('datadir') / 'zyl-os' / 'recovery', install_mode: 'rwxr-xr-x')
install_data('system/recovery/recovery.service',  install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')

# Install AppArmor profiles
install_data('system/apparmor/zyl-wam',           install_dir: get_option('sysconfdir') / 'apparmor.d')
install_data('system/apparmor/zyl-compositor',    install_dir: get_option('sysconfdir') / 'apparmor.d')
install_data('system/apparmor/zyl-app-default',   install_dir: get_option('sysconfdir') / 'apparmor.d')

# Tests
subdir('tests')
