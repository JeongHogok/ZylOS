gio = dependency('gio-2.0')
openssl = dependency('openssl', version: '>=1.1.1')
executable('zyl-lock', 'lock.c', dependencies: [gio, openssl, zyl_fscrypt_dep], install: true)
