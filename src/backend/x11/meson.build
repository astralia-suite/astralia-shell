x11_backend_source = files('backend.cpp')

x11_sources = files(
  'app/active_target.cpp',
  'app/services.cpp',
  'core/event_loop.cpp',
  'core/keyboard.cpp',
  'core/pointer.cpp',
  'core/x_clipboard.cpp',
  'core/x_connection.cpp',
  '../../modules/bamboo/x11/bamboo.cpp',
  '../../modules/bar/x11/bar.cpp',
  '../../modules/bar/x11/panel_host.cpp',
  '../../modules/bar/x11/bar_set.cpp',
  '../../modules/dashboard/x11/dashboard.cpp',
  '../../modules/idle/x11/idle.cpp',
  '../../modules/launcher/x11/launcher.cpp',
  '../../modules/lock/x11/lock.cpp',
  '../../modules/logout/x11/logout.cpp',
  '../../modules/notification/x11/notification.cpp',
  '../../modules/osd/x11/osd.cpp',
  '../../modules/overview/x11/overview.cpp',
  '../../modules/polkit/x11/polkit.cpp',
  '../../modules/rain/x11/rain.cpp',
  '../../modules/settings/x11/settings.cpp',
  '../../modules/visualizer/x11/visualizer.cpp',
  '../../modules/wallpaper/x11/wallpaper.cpp',
  '../../modules/wallpaper_select/x11/wallpaper_select.cpp',
  'render/cairo_canvas.cpp',
  'render/app_icon.cpp',
  'render/cover_cache.cpp',
  'render/draw.cpp',
  'render/image_decode.cpp',
  'render/text.cpp',
  'render/wallpaper_paint.cpp',
  'render/x_window.cpp',
  '../../service/x11/output_service.cpp',
)

x11_deps = [
  dependency('xcb', include_type: 'system'),
  dependency('xcb-ewmh', include_type: 'system'),
  dependency('xcb-icccm', include_type: 'system'),
  dependency('xcb-randr', include_type: 'system'),
  dependency('xcb-shape', include_type: 'system'),
  dependency('xcb-cursor', include_type: 'system'),
  dependency('xcb-screensaver', include_type: 'system'),
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

x11_args = [
  '-DASTRALIA_PAM_DIR="@0@"'.format(x11_data_dir / 'pam'),
]
if get_option('native_cpu')
  x11_args += ['-march=westmere']
endif

