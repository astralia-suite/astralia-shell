x11_backend_source = files('backend.cpp')

x11_sources = files(
  'app/services.cpp',
  'core/event_loop.cpp',
  'core/keyboard.cpp',
  'core/pointer.cpp',
  'core/x_connection.cpp',
  '../../modules/bar/x11/bar.cpp',
  '../../modules/bar/x11/frame.cpp',
  '../../modules/bar/x11/panel_host.cpp',
  '../../modules/bar/x11/bar_set.cpp',
  '../../modules/launcher/x11/launcher.cpp',
  '../../modules/lock/x11/lock.cpp',
  '../../modules/logout/x11/logout.cpp',
  '../../modules/notification/x11/notification.cpp',
  '../../modules/osd/x11/osd.cpp',
  '../../modules/overview/x11/overview.cpp',
  '../../modules/polkit/x11/polkit.cpp',
  '../../modules/settings/x11/settings.cpp',
  '../../modules/wallpaper/x11/wallpaper.cpp',
  'render/cairo_canvas.cpp',
  'render/app_icon.cpp',
  'render/cover_cache.cpp',
  'render/draw.cpp',
  'render/image_decode.cpp',
  'render/text.cpp',
  'render/x_window.cpp',
  '../../service/x11/output_service.cpp',
)

x11_deps = [
  dependency('xcb', include_type: 'system'),
  dependency('xcb-ewmh', include_type: 'system'),
  dependency('xcb-icccm', include_type: 'system'),
  dependency('xcb-randr', include_type: 'system'),
  dependency('xcb-shape', include_type: 'system'),
  dependency('xcb-keysyms', include_type: 'system'),
  dependency('cairo', include_type: 'system'),
  dependency('cairo-xcb', include_type: 'system'),
  dependency('pangocairo', include_type: 'system'),
  dependency('fontconfig', include_type: 'system'),
  dependency('resvg', include_type: 'system'),
  dependency('libjpeg', include_type: 'system'),
  dependency('xkbcommon', include_type: 'system'),
  dependency('xkbcommon-x11', include_type: 'system'),
]

x11_data_dir = get_option('prefix') / get_option('datadir') / 'astralia-shell'
x11_source_assets = meson.project_source_root() / 'assets'

x11_args = [
  '-DASTRALIA_CONSTELLATION_DIR="@0@"'.format(x11_data_dir / 'constellation'),
  '-DASTRALIA_SOURCE_CONSTELLATION_DIR="@0@"'.format(x11_source_assets / 'constellation'),
  '-DASTRALIA_LOGOUT_DIR="@0@"'.format(x11_data_dir / 'logout'),
  '-DASTRALIA_SOURCE_LOGOUT_DIR="@0@"'.format(x11_source_assets / 'logout'),
  '-DASTRALIA_PAM_DIR="@0@"'.format(x11_data_dir / 'pam'),
  '-DASTRALIA_POLKIT_DIR="@0@"'.format(x11_data_dir / 'polkit'),
  '-DASTRALIA_SOURCE_POLKIT_DIR="@0@"'.format(x11_source_assets / 'polkit'),
]
if get_option('native_cpu')
  x11_args += ['-march=westmere']
endif

