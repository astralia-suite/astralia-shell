project(
  'astralia-shell',
  'cpp',
  version: '0.1.0',
  meson_version: '>=1.1',
  default_options: [
    'prefix=/usr',
    'cpp_std=c++23',
    'warning_level=3',
    'optimization=2',
    'debug=false',
  ],
)

sdbus_cpp_dep = dependency('sdbus-c++', include_type: 'system')
threads_dep = dependency('threads')
libpipewire_dep = dependency('libpipewire-0.3', include_type: 'system')
polkit_agent_dep = dependency('polkit-agent-1', include_type: 'system')
pam_dep = meson.get_compiler('cpp').find_library('pam')

deps = [sdbus_cpp_dep, threads_dep, libpipewire_dep, polkit_agent_dep, pam_dep]
resvg_dep = dependency('resvg', include_type: 'system')
render_deps = [
  resvg_dep,
  dependency('libjpeg', include_type: 'system'),
  dependency('cairo', include_type: 'system'),
  dependency('fontconfig', include_type: 'system'),
]

inc = include_directories('src')

add_project_arguments('-ffunction-sections', '-fdata-sections', language: 'cpp')
add_project_link_arguments('-Wl,--gc-sections', language: 'cpp')
hidden = ['-fvisibility=hidden', '-fvisibility-inlines-hidden']

data_dir = get_option('prefix') / get_option('datadir') / 'astralia-shell'
add_project_arguments(
  '-DASTRALIA_DEFAULT_WALLPAPER="@0@"'.format(data_dir / 'wallpaper' / 'default.jpg'),
  '-DASTRALIA_ASSET_DIR="@0@"'.format(data_dir),
  '-DASTRALIA_SOURCE_ASSET_DIR="@0@"'.format(meson.project_source_root() / 'assets'),
  language: 'cpp',
)

core_base = static_library(
  'astralia-core-base',
  [
    'src/app/session.cpp',
    'src/app/shell.cpp',
    'src/core/allocator.cpp',
    'src/core/animation.cpp',
    'src/core/async_process.cpp',
    'src/core/cli.cpp',
    'src/core/config_file.cpp',
    'src/core/daemon.cpp',
    'src/core/dbus.cpp',
    'src/core/deferred_call.cpp',
    'src/core/ipc.cpp',
    'src/core/json.cpp',
    'src/core/log.cpp',
    'src/core/path_home.cpp',
    'src/core/poll_reactor.cpp',
    'src/core/runtime_paths.cpp',
    'src/core/single_instance.cpp',
    'src/core/spawn.cpp',
    'src/core/wall_clock.cpp',
  ],
  include_directories: inc,
  dependencies: deps,
  pic: true,
)

core_shared = static_library(
  'astralia-core-shared',
  [
    'src/modules/bar/paint/frame.cpp',
    'src/modules/bar/model/items.cpp',
    'src/modules/bar/model/layout.cpp',
    'src/modules/bar/model/model.cpp',
    'src/modules/bar/model/motion.cpp',
    'src/modules/bar/paint/chips.cpp',
    'src/modules/bar/model/zones.cpp',
    'src/modules/bar/panel/content/battery_panel.cpp',
    'src/modules/bar/panel/content/bluetooth_panel.cpp',
    'src/modules/bar/panel/content/brightness_panel.cpp',
    'src/modules/bar/panel/content/date_panel.cpp',
    'src/modules/bar/panel/content/time_panel.cpp',
    'src/modules/bar/panel/content/media_panel.cpp',
    'src/modules/bar/panel/content/network_panel.cpp',
    'src/modules/bar/panel/core/panel.cpp',
    'src/modules/bar/panel/core/panel_set.cpp',
    'src/modules/bar/panel/content/resource_panel.cpp',
    'src/modules/bar/panel/content/tray_panel.cpp',
    'src/modules/bar/panel/content/volume_panel.cpp',
    'src/modules/bar/panel/core/widgets.cpp',
    'src/modules/bar/paint/style.cpp',
    'src/modules/bar/paint/view.cpp',
    'src/modules/idle/bounce.cpp',
    'src/modules/idle/clock.cpp',
    'src/modules/launcher/apps.cpp',
    'src/modules/launcher/desktop_entry.cpp',
    'src/modules/launcher/entrance.cpp',
    'src/modules/launcher/files.cpp',
    'src/modules/launcher/launch.cpp',
    'src/modules/launcher/model.cpp',
    'src/modules/launcher/search.cpp',
    'src/modules/launcher/submenu.cpp',
    'src/modules/launcher/view.cpp',
    'src/modules/launcher/visits.cpp',
    'src/modules/lock/layout.cpp',
    'src/modules/lock/model.cpp',
    'src/modules/lock/pam.cpp',
    'src/modules/lock/slash.cpp',
    'src/modules/lock/transition_view.cpp',
    'src/modules/lock/view.cpp',
    'src/modules/logout/layout.cpp',
    'src/modules/logout/model.cpp',
    'src/modules/logout/view.cpp',
    'src/modules/notification/model.cpp',
    'src/modules/notification/view.cpp',
    'src/modules/osd/model.cpp',
    'src/modules/osd/view.cpp',
    'src/modules/overview/model.cpp',
    'src/modules/overview/paging.cpp',
    'src/modules/overview/view.cpp',
    'src/modules/visualizer/audio_capture.cpp',
    'src/modules/visualizer/bars.cpp',
    'src/modules/visualizer/cpu_stages.cpp',
    'src/modules/visualizer/fft.cpp',
    'src/modules/bamboo/grow.cpp',
    'src/modules/bamboo/random.cpp',
    'src/modules/rain/matrix.cpp',
    'src/modules/rain/random.cpp',
    'src/modules/rain/stiletto.cpp',
    'src/modules/settings/model.cpp',
    'src/modules/settings/view.cpp',
    'src/modules/settings/wallpaper_columns.cpp',
    'src/modules/polkit/layout.cpp',
    'src/modules/polkit/model.cpp',
    'src/modules/polkit/view.cpp',
    'src/modules/wallpaper/model.cpp',
    'src/modules/wallpaper/view.cpp',
    'src/modules/wallpaper_select/model.cpp',
    'src/modules/wallpaper_select/view.cpp',
    'src/service/audio_service.cpp',
    'src/service/battery_service.cpp',
    'src/service/bluetooth_service.cpp',
    'src/service/brightness_service.cpp',
    'src/service/compositor_service.cpp',
    'src/service/dock_service.cpp',
    'src/service/hyprland_service.cpp',
    'src/service/i3_service.cpp',
    'src/service/icon_service.cpp',
    'src/service/media_service.cpp',
    'src/service/network_service.cpp',
    'src/service/notification_service.cpp',
    'src/service/palette_export.cpp',
    'src/service/polkit_service.cpp',
    'src/service/telemetry_service.cpp',
    'src/service/theme_service.cpp',
    'src/service/tray_service.cpp',
    'src/service/settings_service.cpp',
    'src/service/user_service.cpp',
    'src/service/wallpaper_service.cpp',
    'src/render/app_fonts.cpp',
    'src/render/decode.cpp',
    'src/render/field_view.cpp',
    'src/render/marquee.cpp',
    'src/render/module_base.cpp',
    'src/render/palette_extract.cpp',
    'src/render/scroll.cpp',
    'src/render/scroll_sequence.cpp',
    'src/render/sticker.cpp',
    'src/render/theme.cpp',
    'src/render/text_field.cpp',
  ],
  include_directories: inc,
  dependencies: deps + render_deps,
  cpp_args: hidden,
  pic: true,
)

dl_dep = meson.get_compiler('cpp').find_library('dl', required: false)

backend_dir = get_option('prefix') / get_option('libdir') / 'astralia'

astralia_exe = executable(
  'astralia',
  'src/main.cpp',
  include_directories: inc,
  link_whole: core_base,
  dependencies: deps + [dl_dep],
  export_dynamic: true,
  cpp_args: ['-DASTRALIA_BACKEND_DIR="@0@"'.format(backend_dir)],
  install: true,
)

backend_targets = []

install_data(
  'assets/fonts/tabler-icons.ttf',
  'assets/fonts/ComicShannsMono-Regular.otf',
  'assets/fonts/YujiMai.ttf',
  install_dir: data_dir / 'fonts',
)
install_data('assets/stellar-restoration.svg', 'assets/stiletto.svg', install_dir: data_dir)
install_data('assets/wallpaper/default.jpg', install_dir: data_dir / 'wallpaper')
install_data('assets/bamboo/bamboo.png', install_dir: data_dir / 'bamboo')
configure_file(
  input: 'assets/bamboo/astralia-shell-bamboo.desktop.in',
  output: 'astralia-shell-bamboo.desktop',
  configuration: {'icon': data_dir / 'bamboo' / 'bamboo.png'},
  install: true,
  install_dir: get_option('prefix') / get_option('datadir') / 'applications',
)
install_data('assets/logout/logo.png', install_dir: data_dir / 'logout')
install_data('assets/polkit/electro.png', install_dir: data_dir / 'polkit')
install_data('assets/pam/astralia-shell', install_dir: data_dir / 'pam')
install_data(
  'assets/constellation/C1.png',
  'assets/constellation/C2.png',
  'assets/constellation/C3.png',
  'assets/constellation/C4.png',
  'assets/constellation/C5.png',
  'assets/constellation/C6.png',
  install_dir: data_dir / 'constellation',
)

wayland_enabled = 'wayland' in get_option('backends')

if wayland_enabled
  add_languages('c', native: false, required: true)

  wayland_backend_source = 'src/backend/wayland/backend.cpp'

  wayland_sources = [
    'src/backend/wayland/app/config.cpp',
    'src/backend/wayland/app/ipc.cpp',
    'src/backend/wayland/app/key_dispatch.cpp',
    'src/backend/wayland/app/module_registry.cpp',
    'src/backend/wayland/app/monitor_output.cpp',
    'src/backend/wayland/app/service_registry.cpp',
    'src/backend/wayland/app/user_info.cpp',
    'src/backend/wayland/app/wayland_registry.cpp',
    'src/backend/wayland/core/log.cpp',
    'src/backend/wayland/core/poll_source.cpp',
    'src/modules/bamboo/wayland/bamboo.cpp',
    'src/modules/bar/wayland/bar.cpp',
    'src/modules/bar/wayland/panel_surface.cpp',
    'src/modules/dashboard/wayland/dashboard.cpp',
    'src/modules/idle/wayland/idle.cpp',
    'src/modules/launcher/wayland/launcher.cpp',
    'src/modules/lock/wayland/lock.cpp',
    'src/modules/lock/wayland/wallpaper.cpp',
    'src/modules/logout/wayland/logout.cpp',
    'src/modules/notification/wayland/notification.cpp',
    'src/modules/osd/wayland/osd.cpp',
    'src/modules/overview/wayland/overview.cpp',
    'src/modules/polkit/wayland/polkit.cpp',
    'src/modules/rain/wayland/rain.cpp',
    'src/modules/settings/wayland/settings.cpp',
    'src/modules/visualizer/wayland/visualizer.cpp',
    'src/modules/visualizer/wayland/audio_stages.cpp',
    'src/modules/visualizer/wayland/bars.cpp',
    'src/modules/visualizer/wayland/sphere.cpp',
    'src/modules/visualizer/wayland/shaders.cpp',
    'src/modules/wallpaper/wayland/wallpaper.cpp',
    'src/modules/wallpaper_select/wayland/wallpaper_select.cpp',
    'src/backend/wayland/render/arc_gauge.cpp',
    'src/backend/wayland/render/gl.cpp',
    'src/backend/wayland/render/gl_canvas.cpp',
    'src/backend/wayland/render/icon.cpp',
    'src/backend/wayland/render/image.cpp',
    'src/backend/wayland/render/layer_surface.cpp',
    'src/backend/wayland/render/overlay_panel.cpp',
    'src/backend/wayland/render/popup_window.cpp',
    'src/backend/wayland/render/renderer.cpp',
    'src/backend/wayland/render/text.cpp',
    'src/backend/wayland/render/texture.cpp',
    'src/backend/wayland/render/texture_cache.cpp',
    'src/backend/wayland/render/thunder.cpp',
    'src/backend/wayland/render/toplevel_window.cpp',
    'src/backend/wayland/render/video_texture.cpp',
    'src/backend/wayland/render/wallpaper_column.cpp',
    'src/service/wayland/capture_service.cpp',
    'src/service/wayland/clipboard_service.cpp',
    'src/service/wayland/frame_service.cpp',
    'src/service/wayland/idle_service.cpp',
    'src/service/wayland/input_service.cpp',
    'src/service/wayland/media_service.cpp',
    'src/service/wayland/output_service.cpp',
    'src/service/wayland/settings_service.cpp',
    'src/service/wayland/text_input_service.cpp',
    'src/modules/lock/wayland/transition.cpp',
  ]

  libdrm_dep = dependency('libdrm')

  wayland_deps = [
    dependency('wayland-client'),
    dependency('wayland-egl'),
    dependency('egl'),
    dependency('glesv2'),
    dependency('cairo'),
    dependency('pangocairo'),
    dependency('xkbcommon'),
    dependency('libjpeg'),
    resvg_dep,
    libdrm_dep,
    dependency('glib-2.0'),
    dependency('gio-2.0'),
    dependency('gobject-2.0'),
    dependency('polkit-gobject-1'),
    meson.get_compiler('cpp').find_library('pam'),
  ]

  wayland_args = [
    '-DASTRALIA_SHELL_SHADER_DIR="@0@"'.format(data_dir / 'shaders'),
    '-DASTRALIA_SHELL_DEFAULT_WALLPAPER="@0@"'.format(meson.project_source_root() / 'assets' / 'wallpaper' / 'default.jpg'),
    '-DASTRALIA_SHELL_PAM_DIR="@0@"'.format(data_dir / 'pam'),
    '-DASTRALIA_SHELL_MEDIA_PLUGIN="@0@"'.format(data_dir / 'plugins' / 'libastralia-shell-media.so'),
  ]

  wayland_scanner = find_program('wayland-scanner')
  wl_proto_dir = dependency('wayland-protocols').get_variable(pkgconfig: 'pkgdatadir')

  wayland_protocol_defs = [
    ['xdg-shell', wl_proto_dir / 'stable/xdg-shell/xdg-shell.xml', false],
    ['wlr-layer-shell-unstable-v1', 'src/backend/wayland/protocols/wlr-layer-shell-unstable-v1.xml', true],
    ['ext-idle-notify-v1', wl_proto_dir / 'staging/ext-idle-notify/ext-idle-notify-v1.xml', false],
    ['cursor-shape-v1', wl_proto_dir / 'staging/cursor-shape/cursor-shape-v1.xml', false],
    ['tablet-unstable-v2', wl_proto_dir / 'unstable/tablet/tablet-unstable-v2.xml', false],
    ['hyprland-toplevel-export-v1', 'src/backend/wayland/protocols/hyprland-toplevel-export-v1.xml', false],
    ['wlr-foreign-toplevel-management-unstable-v1', 'src/backend/wayland/protocols/wlr-foreign-toplevel-management-unstable-v1.xml', false],
    ['text-input-unstable-v3', 'src/backend/wayland/protocols/text-input-unstable-v3.xml', false],
    ['ext-session-lock-v1', wl_proto_dir / 'staging/ext-session-lock/ext-session-lock-v1.xml', false],
  ]

  wayland_protocol_sources = []
  wayland_protocol_headers = []
  foreach p : wayland_protocol_defs
    fixup = p[2] ? ' && sed -i s/namespace/name_space/g "$3"' : ''
    wayland_protocol_headers += custom_target(
      p[0] + '-client-header',
      input: p[1],
      output: p[0] + '-client-protocol.h',
      command: ['sh', '-c', '"$1" client-header "$2" "$3"' + fixup, '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'],
    )
    wayland_protocol_sources += custom_target(
      p[0] + '-client-source',
      input: p[1],
      output: p[0] + '-client-protocol.c',
      command: ['sh', '-c', '"$1" private-code "$2" "$3"' + fixup, '_', wayland_scanner.full_path(), '@INPUT@', '@OUTPUT@'],
    )
  endforeach

  wayland_media_plugin_sources = [
    'src/backend/wayland/plugin/media_plugin.cpp',
    'src/backend/wayland/core/log.cpp',
  ]
  wayland_media_deps = [
    dependency('libavformat'),
    dependency('libavcodec'),
    dependency('libavfilter'),
    dependency('libavutil'),
    libdrm_dep,
  ]

  install_subdir('assets/shaders', install_dir: data_dir)

  wayland_include = [inc, include_directories('src/backend')]
  wayland_lib = static_library(
    'astralia-wayland-lib',
    wayland_sources,
    wayland_protocol_sources,
    wayland_protocol_headers,
    include_directories: wayland_include,
    dependencies: deps + wayland_deps,
    cpp_args: wayland_args + hidden,
    pic: true,
  )
  backend_targets += shared_module(
    'astralia-wayland',
    wayland_backend_source,
    wayland_protocol_headers,
    name_prefix: 'lib',
    include_directories: wayland_include,
    dependencies: deps + wayland_deps,
    cpp_args: wayland_args + hidden,
    link_whole: wayland_lib,
    link_with: core_shared,
    install: true,
    install_dir: backend_dir,
  )
  backend_targets += shared_module(
    'astralia-shell-media',
    wayland_media_plugin_sources,
    include_directories: wayland_include,
    dependencies: wayland_media_deps,
    install: true,
    install_dir: data_dir / 'plugins',
  )
endif

x11_enabled = 'x11' in get_option('backends')

if x11_enabled
  x11_backend_source = 'src/backend/x11/backend.cpp'

  x11_sources = [
    'src/backend/x11/app/active_target.cpp',
    'src/backend/x11/app/services.cpp',
    'src/backend/x11/core/event_loop.cpp',
    'src/backend/x11/core/keyboard.cpp',
    'src/backend/x11/core/pointer.cpp',
    'src/backend/x11/core/x_clipboard.cpp',
    'src/backend/x11/core/x_connection.cpp',
    'src/modules/bamboo/x11/bamboo.cpp',
    'src/modules/bar/x11/bar.cpp',
    'src/modules/bar/x11/panel_host.cpp',
    'src/modules/bar/x11/bar_set.cpp',
    'src/modules/dashboard/x11/dashboard.cpp',
    'src/modules/idle/x11/idle.cpp',
    'src/modules/launcher/x11/launcher.cpp',
    'src/modules/lock/x11/lock.cpp',
    'src/modules/logout/x11/logout.cpp',
    'src/modules/notification/x11/notification.cpp',
    'src/modules/osd/x11/osd.cpp',
    'src/modules/overview/x11/overview.cpp',
    'src/modules/polkit/x11/polkit.cpp',
    'src/modules/rain/x11/rain.cpp',
    'src/modules/settings/x11/settings.cpp',
    'src/modules/visualizer/x11/visualizer.cpp',
    'src/modules/wallpaper/x11/wallpaper.cpp',
    'src/modules/wallpaper_select/x11/wallpaper_select.cpp',
    'src/backend/x11/render/cairo_canvas.cpp',
    'src/backend/x11/render/app_icon.cpp',
    'src/backend/x11/render/cover_cache.cpp',
    'src/backend/x11/render/draw.cpp',
    'src/backend/x11/render/image_decode.cpp',
    'src/backend/x11/render/text.cpp',
    'src/backend/x11/render/wallpaper_paint.cpp',
    'src/backend/x11/render/x_window.cpp',
    'src/service/x11/output_service.cpp',
  ]

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
    resvg_dep,
    dependency('libjpeg', include_type: 'system'),
    dependency('xkbcommon', include_type: 'system'),
    dependency('xkbcommon-x11', include_type: 'system'),
  ]

  x11_args = ['-DASTRALIA_PAM_DIR="@0@"'.format(data_dir / 'pam')]
  if get_option('native_cpu')
    x11_args += ['-march=westmere']
  endif

  x11_include = [inc, include_directories('src/backend/x11')]
  x11_lib = static_library(
    'astralia-x11-lib',
    x11_sources,
    include_directories: x11_include,
    dependencies: deps + x11_deps,
    cpp_args: x11_args + hidden,
    pic: true,
  )
  backend_targets += shared_module(
    'astralia-x11',
    x11_backend_source,
    name_prefix: 'lib',
    include_directories: x11_include,
    dependencies: deps + x11_deps,
    cpp_args: x11_args + hidden,
    link_whole: x11_lib,
    link_with: core_shared,
    install: true,
    install_dir: backend_dir,
  )
endif

unit = executable(
  'astralia-test',
  [
    'test/main.cpp',
    'test/core/test_animation.cpp',
    'test/core/test_async_process.cpp',
    'test/core/test_config_file.cpp',
    'test/core/test_deferred_call.cpp',
    'test/core/test_input.cpp',
    'test/core/test_invocation.cpp',
    'test/core/test_json.cpp',
    'test/core/test_path_home.cpp',
    'test/core/test_reactor.cpp',
    'test/core/test_spawn.cpp',
    'test/render/test_module_base.cpp',
    'test/render/test_oklab.cpp',
    'test/render/test_palette_extract.cpp',
    'test/render/test_scroll.cpp',
    'test/render/test_scroll_sequence.cpp',
    'test/render/test_text_cursor.cpp',
    'test/render/test_theme.cpp',
    'test/render/test_tokens.cpp',
    'test/modules/bar/test_bar.cpp',
    'test/modules/bar/test_panel.cpp',
    'test/modules/launcher/test_entrance.cpp',
    'test/modules/launcher/test_launcher.cpp',
    'test/modules/launcher/test_model.cpp',
    'test/modules/lock/test_lock.cpp',
    'test/modules/lock/test_layout.cpp',
    'test/modules/lock/test_slash.cpp',
    'test/modules/logout/test_layout.cpp',
    'test/modules/logout/test_logout.cpp',
    'test/modules/notification/test_notification.cpp',
    'test/modules/osd/test_osd.cpp',
    'test/modules/settings/test_settings.cpp',
    'test/modules/wallpaper/test_menu.cpp',
    'test/modules/wallpaper_select/test_wallpaper_select.cpp',
    'test/modules/overview/test_overview.cpp',
    'test/modules/overview/test_paging.cpp',
    'test/modules/polkit/test_layout.cpp',
    'test/modules/polkit/test_polkit.cpp',
    'test/modules/idle/test_idle.cpp',
    'test/modules/rain/test_rain_buffer.cpp',
    'test/modules/bamboo/test_grow.cpp',
    'test/modules/visualizer/test_cpu_stages.cpp',
    'test/modules/visualizer/test_fft.cpp',
    'test/app/test_session.cpp',
    'test/app/test_shell.cpp',
    'test/service/test_compositor.cpp',
    'test/service/test_services.cpp',
    'test/service/test_settings.cpp',
    'test/service/test_wallpaper.cpp',
    'test/service/test_telemetry.cpp',
    'test/service/test_brightness.cpp',
    'test/service/test_theme_service.cpp',
    'test/service/test_palette_export.cpp',
  ],
  include_directories: [inc, include_directories('test')],
  link_with: [core_base, core_shared],
  dependencies: deps + render_deps + [dl_dep],
)

test('unit', unit)

if wayland_enabled
  wayland_unit = executable(
    'astralia-wayland-test',
    [
      'test/backend/wayland/test_wayland.cpp',
      'test/backend/wayland/app/test_config.cpp',
      'test/backend/wayland/core/test_poll_source.cpp',
      'test/backend/wayland/render/test_media_decode.cpp',
      'test/backend/wayland/render/test_image_decode.cpp',
      'test/backend/wayland/render/test_marquee_scroll.cpp',
      'test/backend/wayland/render/test_palette.cpp',
      'test/backend/wayland/service/test_active_output.cpp',
      'test/backend/wayland/service/test_dock.cpp',
      'test/backend/wayland/service/test_input_convert.cpp',
      'test/backend/wayland/service/test_keyboard.cpp',
    ],
    wayland_protocol_headers,
    include_directories: [wayland_include, include_directories('test')],
    link_with: [wayland_lib, core_shared, core_base],
    dependencies: deps + wayland_deps + [dl_dep],
    cpp_args: wayland_args,
  )
  test('wayland-unit', wayland_unit)
endif

if x11_enabled
  x11_unit = executable(
    'astralia-x11-test',
    'test/backend/x11/test_x11.cpp',
    include_directories: [x11_include, include_directories('test')],
    link_with: [x11_lib, core_shared, core_base],
    dependencies: deps + x11_deps + [dl_dep],
    cpp_args: x11_args,
  )
  test('x11-unit', x11_unit)
endif
test('backend-smoke', find_program('bash'), args: [files('test/backend_smoke.sh'), astralia_exe], depends: backend_targets)
