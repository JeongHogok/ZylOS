gio = dependency('gio-2.0')
threads = dependency('threads')
math = meson.get_compiler('c').find_library('m', required: true)

# 선택적 의존성: GPSD
libgps = dependency('libgps', required: false)
location_c_args = []
location_deps = [gio, threads, math]

if libgps.found()
  location_deps += libgps
  location_c_args += '-DHAVE_GPSD'
endif

# 선택적 의존성: libcurl (GeoIP 폴백)
libcurl = dependency('libcurl', required: false)
if libcurl.found()
  location_deps += libcurl
  location_c_args += '-DHAVE_CURL'
endif

executable('zyl-location',
  'location.c',
  c_args: location_c_args,
  dependencies: location_deps,
  install: true,
)
