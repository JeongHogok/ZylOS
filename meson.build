project('zyl-os', 'c',
  version: '0.1.0',
  default_options: ['c_std=c11', 'warning_level=2'],
)

# Subprojects
subdir('compositor')
subdir('runtime/wam')
subdir('runtime/services/notification')
subdir('runtime/services/appstore')
subdir('runtime/services/updater')
subdir('runtime/services/power')
subdir('runtime/services/sandbox')
subdir('runtime/services/sensors')
subdir('runtime/services/location')
subdir('runtime/services/telephony')

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
install_data('system/zyl-os.target',         install_dir: get_option('prefix') / 'lib' / 'systemd' / 'system')
